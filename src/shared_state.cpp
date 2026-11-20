#include "shared_state.h"

#include <cstdio>
#include <cwchar>
#include <cstring>

namespace boonpanel {

std::uint64_t TickNowMs() noexcept {
    return static_cast<std::uint64_t>(GetTickCount64());
}

bool SharedState::Open() noexcept {
    if (view_) {
        return true;
    }

    wchar_t mapping_name[96]{};
    const DWORD pid = GetCurrentProcessId();
    if (swprintf_s(mapping_name, sizeof(mapping_name) / sizeof(mapping_name[0]), L"Local\\GW2BoonPanel_v2_%lu", static_cast<unsigned long>(pid)) < 0) {
        return false;
    }

    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, static_cast<DWORD>(sizeof(SharedSnapshot)), mapping_name);
    if (!mapping_) {
        return false;
    }

    view_ = static_cast<SharedSnapshot*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0,
                                                       sizeof(SharedSnapshot)));
    if (!view_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    // Only initialize when the mapping is new or was created by an older build.
    if (view_->magic != kSharedMagic || view_->version != kSharedVersion) {
        InterlockedExchange(&view_->sequence, 1);
        view_->magic = kSharedMagic;
        view_->version = kSharedVersion;
        view_->feed_seen = 0;
        view_->map_loading = 0;
        view_->well_timer_expires_at_ms = 0;
        view_->well_timer_duration_ms = 40000;
        std::memset(&view_->diagnostics, 0, sizeof(view_->diagnostics));
        std::memset(view_->track_count, 0, sizeof(view_->track_count));
        std::memset(view_->expires_at_ms, 0, sizeof(view_->expires_at_ms));
        MemoryBarrier();
        InterlockedExchange(&view_->sequence, 2);
    }
    return true;
}

void SharedState::Close() noexcept {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}

void SharedState::Publish(const std::uint32_t* track_count,
                          const std::uint64_t* flattened_expiries,
                          bool feed_seen, bool map_loading) noexcept {
    if (!view_ || !track_count || !flattened_expiries) {
        return;
    }

    InterlockedIncrement(&view_->sequence); // odd: write in progress
    MemoryBarrier();
    view_->magic = kSharedMagic;
    view_->version = kSharedVersion;
    view_->feed_seen = feed_seen ? 1u : 0u;
    view_->map_loading = map_loading ? 1u : 0u;
    std::memcpy(view_->track_count, track_count, sizeof(view_->track_count));
    std::memcpy(view_->expires_at_ms, flattened_expiries, sizeof(view_->expires_at_ms));
    MemoryBarrier();
    InterlockedIncrement(&view_->sequence); // even: stable snapshot
}

void SharedState::SetWellTimerDuration(std::uint32_t duration_ms) noexcept {
    if (!view_) return;
    InterlockedIncrement(&view_->sequence);
    MemoryBarrier();
    view_->well_timer_duration_ms = duration_ms;
    MemoryBarrier();
    InterlockedIncrement(&view_->sequence);
}

void SharedState::PublishWellTimer(std::uint64_t expires_at_ms) noexcept {
    if (!view_) return;
    InterlockedIncrement(&view_->sequence);
    MemoryBarrier();
    view_->well_timer_expires_at_ms = expires_at_ms;
    MemoryBarrier();
    InterlockedIncrement(&view_->sequence);
}

void SharedState::PublishDiagnostics(const SharedDiagnostics& diagnostics) noexcept {
    if (!view_) return;
    InterlockedIncrement(&view_->sequence);
    MemoryBarrier();
    view_->diagnostics = diagnostics;
    MemoryBarrier();
    InterlockedIncrement(&view_->sequence);
}

bool SharedState::Read(SharedSnapshot& out) const noexcept {
    if (!view_) {
        return false;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG before = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&view_->sequence), 0, 0);
        if (before & 1) {
            continue;
        }

        out.magic = view_->magic;
        out.version = view_->version;
        out.feed_seen = view_->feed_seen;
        out.map_loading = view_->map_loading;
        out.well_timer_expires_at_ms = view_->well_timer_expires_at_ms;
        out.well_timer_duration_ms = view_->well_timer_duration_ms;
        out.diagnostics = view_->diagnostics;
        std::memcpy(out.track_count, view_->track_count, sizeof(out.track_count));
        std::memcpy(out.expires_at_ms, view_->expires_at_ms, sizeof(out.expires_at_ms));
        MemoryBarrier();

        const LONG after = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&view_->sequence), 0, 0);
        if (before == after && !(after & 1) && out.magic == kSharedMagic &&
            out.version == kSharedVersion) {
            out.sequence = after;
            return true;
        }
    }
    return false;
}

} // namespace boonpanel
