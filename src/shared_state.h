#pragma once

#include <Windows.h>
#include <cstdint>

#include "boons.h"

namespace boonpanel {

inline constexpr std::uint32_t kSharedMagic = 0x42504E4Cu; // "BPNL"
inline constexpr std::uint32_t kSharedVersion = 3;
inline constexpr std::size_t kMaxTrackedStacks = 128;

// Shared between the Nexus-loaded copy and the ArcDPS-loaded copy of this DLL.
// Sequence is a Windows interlocked seqlock counter: odd means writer active.
struct SharedDiagnostics {
    std::uint64_t local_callbacks = 0;
    std::uint64_t area_callbacks = 0;
    std::uint64_t null_events = 0;
    std::uint64_t boon_events = 0;
    std::uint64_t local_target_matches = 0;
    std::uint64_t local_agent_id = 0;
    std::uint32_t last_skill_id = 0;
    std::int32_t last_value = 0;
    std::uint8_t last_statechange = 0;
    std::uint8_t last_buff = 0;
    std::uint8_t last_buffremove = 0;
    std::uint8_t last_src_self = 0;
    std::uint8_t last_dst_self = 0;
    std::uint8_t bridge_open = 0;
    char last_src_name[48]{};
    char last_dst_name[48]{};
};

struct SharedSnapshot {
    volatile LONG sequence;
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t feed_seen;
    SharedDiagnostics diagnostics;
    std::uint32_t track_count[kBoonCount];
    std::uint64_t expires_at_ms[kBoonCount][kMaxTrackedStacks];
};

class SharedState final {
public:
    SharedState() = default;
    ~SharedState() = default;

    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    bool Open() noexcept;
    void Close() noexcept;
    bool IsOpen() const noexcept { return view_ != nullptr; }

    void PublishDiagnostics(const SharedDiagnostics& diagnostics) noexcept;

    void Publish(const std::uint32_t* track_count,
                 const std::uint64_t* flattened_expiries,
                 bool feed_seen) noexcept;

    bool Read(SharedSnapshot& out) const noexcept;

private:
    HANDLE mapping_ = nullptr;
    SharedSnapshot* view_ = nullptr;
};

std::uint64_t TickNowMs() noexcept;

} // namespace boonpanel
