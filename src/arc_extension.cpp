#include "arcdps_api.h"
#include "boons.h"
#include "shared_state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <unordered_set>
#include <mutex>
#include <unordered_map>

namespace {

struct TrackedStack {
    std::uint64_t expires_at_ms = 0;
};

using StackMap = std::unordered_map<std::uint32_t, TrackedStack>;
std::array<StackMap, boonpanel::kBoonCount> g_tracked;
std::mutex g_tracker_mutex;
boonpanel::SharedState g_shared;
bool g_feed_seen = false;
std::uint32_t g_synthetic_id = 1;

std::uint32_t TrackId(const ArcCbtEvent& ev) noexcept {
    std::uint32_t id = 0;
    std::memcpy(&id, &ev.pad61, sizeof(id));
    return id;
}

bool IsSelf(const ArcAgent* agent) noexcept {
    return agent != nullptr && agent->self != 0;
}

// We display the boon state on the local character, regardless of who applied it.
// In a combat_local callback the local agent is normally one side of the event;
// for applications the recipient is dst.
bool IsLocalRecipient(const ArcAgent* dst) noexcept {
    return IsSelf(dst);
}

void ApplyStack(StackMap& stacks, std::uint32_t track_id, std::int32_t duration,
                std::uint64_t now) {
    if (duration <= 0) return;
    const std::uint32_t key = track_id != 0
        ? track_id
        : (0x80000000u | (g_synthetic_id++ & 0x7fffffffu));
    stacks[key] = { now + static_cast<std::uint32_t>(duration) };
}

void RemoveOneStack(StackMap& stacks) {
    if (stacks.empty()) return;
    // Classic EVTC buff removals have no per-stack id. Remove the stack
    // nearest to expiry, matching the oldest stack in the common case.
    auto oldest = stacks.begin();
    for (auto it = stacks.begin(); it != stacks.end(); ++it) {
        if (it->second.expires_at_ms < oldest->second.expires_at_ms) oldest = it;
    }
    stacks.erase(oldest);
}

void ClearExpired(std::uint64_t now) {
    for (auto& boon_stacks : g_tracked) {
        for (auto it = boon_stacks.begin(); it != boon_stacks.end();) {
            if (it->second.expires_at_ms <= now) {
                it = boon_stacks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void PublishSnapshot(std::uint64_t now) {
    std::array<std::uint32_t, boonpanel::kBoonCount> track_counts{};
    std::array<std::uint64_t,
        boonpanel::kBoonCount * boonpanel::kMaxTrackedStacks> expiries{};

    ClearExpired(now);
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        std::size_t slot = 0;
        for (const auto& item : g_tracked[i]) {
            if (slot >= boonpanel::kMaxTrackedStacks) {
                break;
            }
            expiries[i * boonpanel::kMaxTrackedStacks + slot] = item.second.expires_at_ms;
            ++slot;
        }
        track_counts[i] = static_cast<std::uint32_t>(slot);
    }

    g_shared.Publish(track_counts.data(), expiries.data(), g_feed_seen);
}

boonpanel::SharedDiagnostics g_diag{};
std::unordered_set<std::uint64_t> g_seen_event_ids;
std::deque<std::uint64_t> g_seen_order;

void CopyAgentName(char (&out)[48], const ArcAgent* agent) {
    out[0] = '\0';
    if (!agent || !agent->name) return;
    strncpy_s(out, agent->name, _TRUNCATE);
}

bool IsLocalTarget(const ArcCbtEvent* ev, const ArcAgent* dst) noexcept {
    if (ev && g_diag.local_agent_id != 0 && ev->dst_agent == g_diag.local_agent_id)
        return true;
    // Fallback is useful on Arc builds which populate self on the event agent.
    return dst && dst->self != 0;
}

bool IsLocalSource(const ArcCbtEvent* ev, const ArcAgent* src) noexcept {
    if (ev && g_diag.local_agent_id != 0 && ev->src_agent == g_diag.local_agent_id)
        return true;
    return src && src->self != 0;
}

bool AlreadyProcessed(std::uint64_t event_id) {
    if (event_id == 0) return false;
    if (g_seen_event_ids.contains(event_id)) return true;
    g_seen_event_ids.insert(event_id);
    g_seen_order.push_back(event_id);
    if (g_seen_order.size() > 4096) {
        g_seen_event_ids.erase(g_seen_order.front());
        g_seen_order.pop_front();
    }
    return false;
}

void OnCombatEvent(ArcCbtEvent* ev, ArcAgent* src, ArcAgent* dst,
                   std::uint64_t event_id, bool local_callback) {
    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const std::uint64_t now = boonpanel::TickNowMs();
    if (local_callback) ++g_diag.local_callbacks;
    else ++g_diag.area_callbacks;

    if (!ev) {
        ++g_diag.null_events;
        // ArcDPS agent-add notifications carry the local-player marker in dst->self.
        if (src && dst && dst->self != 0 && src->id != 0) {
            g_diag.local_agent_id = static_cast<std::uint64_t>(src->id);
        }
        g_diag.bridge_open = g_shared.IsOpen() ? 1 : 0;
        g_feed_seen = true;
        g_shared.PublishDiagnostics(g_diag);
        PublishSnapshot(now);
        return;
    }

    g_diag.last_skill_id = ev->skillid;
    g_diag.last_value = ev->value;
    g_diag.last_statechange = ev->is_statechange;
    g_diag.last_buff = ev->buff;
    g_diag.last_buffremove = ev->is_buffremove;
    g_diag.last_src_self = src ? static_cast<std::uint8_t>(src->self != 0) : 0;
    g_diag.last_dst_self = dst ? static_cast<std::uint8_t>(dst->self != 0) : 0;
    CopyAgentName(g_diag.last_src_name, src);
    CopyAgentName(g_diag.last_dst_name, dst);
    g_diag.bridge_open = g_shared.IsOpen() ? 1 : 0;
    g_feed_seen = true; // distinguish a loaded callback from a dead bridge

    if (AlreadyProcessed(event_id)) {
        g_shared.PublishDiagnostics(g_diag);
        PublishSnapshot(now);
        return;
    }

    if (ev->is_statechange == CBTS_MAPCHANGE) {
        for (auto& boon_stacks : g_tracked) boon_stacks.clear();
        g_shared.PublishDiagnostics(g_diag);
        PublishSnapshot(now);
        return;
    }

    const int index = boonpanel::IndexForSkillId(ev->skillid);
    if (index < 0) {
        g_shared.PublishDiagnostics(g_diag);
        PublishSnapshot(now);
        return;
    }

    auto& stacks = g_tracked[static_cast<std::size_t>(index)];
    const std::uint32_t track_id = TrackId(*ev);
    const bool target_is_local = IsLocalTarget(ev, dst);
    const bool source_is_local = IsLocalSource(ev, src);

    if (target_is_local) ++g_diag.local_target_matches;
    ++g_diag.boon_events;

    // Classic CBTS_COMBAT buff events: buff=1, value is applied duration;
    // is_buffremove distinguishes removal bookkeeping.
    if (ev->is_statechange == 0 && ev->buff != 0) {
        if (ev->is_buffremove == 0 && ev->value > 0 && target_is_local) {
            ApplyStack(stacks, 0, ev->value, now);
        } else if (ev->is_buffremove == 1 && source_is_local) {
            stacks.clear();
        } else if (ev->is_buffremove == 2 && source_is_local) {
            RemoveOneStack(stacks);
        }
        g_shared.PublishDiagnostics(g_diag);
        PublishSnapshot(now);
        return;
    }

    switch (ev->is_statechange) {
    case CBTS_BUFFAPPLY:
        if (target_is_local && ev->value > 0) ApplyStack(stacks, track_id, ev->value, now);
        break;
    case CBTS_BUFFINITIAL:
        if (target_is_local) {
            const std::int32_t duration = ev->value > 0 ? ev->value : ev->buff_dmg;
            ApplyStack(stacks, track_id, duration, now);
        }
        break;
    case CBTS_BUFFACTIVE: {
        if (source_is_local) {
            const std::uint32_t active_track_id = static_cast<std::uint32_t>(ev->dst_agent);
            if (active_track_id != 0 && ev->value > 0)
                stacks[active_track_id] = { now + static_cast<std::uint32_t>(ev->value) };
        }
        break;
    }
    case CBTS_BUFFDEACTIVE:
        if (source_is_local && track_id != 0) {
            if (ev->value <= 0) stacks.erase(track_id);
            else stacks[track_id] = { now + static_cast<std::uint32_t>(ev->value) };
        }
        break;
    case CBTS_BUFFCHANGE:
        if (target_is_local && track_id != 0) {
            if (ev->overstack_value == 0) stacks.erase(track_id);
            else stacks[track_id] = { now + ev->overstack_value };
        }
        break;
    case CBTS_BUFFREMOVE_SINGLE:
        if (source_is_local && track_id != 0) stacks.erase(track_id);
        break;
    case CBTS_BUFFREMOVE_ALL:
        if (source_is_local) stacks.clear();
        break;
    default:
        break;
    }

    g_shared.PublishDiagnostics(g_diag);
    PublishSnapshot(now);
}

void OnCombatLocal(ArcCbtEvent* ev, ArcAgent* src, ArcAgent* dst,
                   char* /*skillname*/, std::uint64_t id, std::uint64_t /*revision*/) {
    OnCombatEvent(ev, src, dst, id, true);
}

void OnCombatArea(ArcCbtEvent* ev, ArcAgent* src, ArcAgent* dst,
                  const char* /*skillname*/, std::uint64_t id, std::uint64_t /*revision*/) {
    OnCombatEvent(ev, src, dst, id, false);
}

ArcDpsExports g_arc_exports{};

ArcDpsExports* ArcModInit() {
    const bool bridge_ok = g_shared.Open();
    {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        for (auto& boon_stacks : g_tracked) {
            boon_stacks.clear();
        }
        g_feed_seen = false;
        g_diag = {};
        g_diag.bridge_open = bridge_ok ? 1 : 0;
        g_seen_event_ids.clear();
        g_seen_order.clear();
        PublishSnapshot(boonpanel::TickNowMs());
    }

    g_arc_exports = {};
    g_arc_exports.size = sizeof(ArcDpsExports);
    g_arc_exports.sig = 0x42504E31u; // "BPN1" - choose a unique ID if you fork this project.
    g_arc_exports.imguivers = 19270; // ArcDPS currently uses ImGui 1.92.7; no Arc UI callback is used.
    g_arc_exports.out_name = "BoonPanel data bridge";
    g_arc_exports.out_build = "0.1.0";
    g_arc_exports.combat = reinterpret_cast<void*>(static_cast<ArcCombatArea>(&OnCombatArea));
    g_arc_exports.combat_local = reinterpret_cast<void*>(static_cast<ArcCombatLocal>(&OnCombatLocal));
    return &g_arc_exports;
}

void ArcModRelease() {
    {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        for (auto& boon_stacks : g_tracked) {
            boon_stacks.clear();
        }
        g_feed_seen = false;
        g_diag = {};
        g_seen_event_ids.clear();
        g_seen_order.clear();
        PublishSnapshot(boonpanel::TickNowMs());
    }
    g_shared.Close();
}

} // namespace

extern "C" __declspec(dllexport) void* get_init_addr(
    char* /*arcversionstr*/, void* /*imguicontext*/, void* /*id3dptr*/,
    HANDLE /*arcdll*/, void* /*mallocfn*/, void* /*freefn*/, std::uint32_t /*imguiversion*/) {
    return reinterpret_cast<void*>(&ArcModInit);
}

extern "C" __declspec(dllexport) void* get_release_addr(std::uint32_t /*reason*/) {
    return reinterpret_cast<void*>(&ArcModRelease);
}
