#pragma once

#include <Windows.h>
#include <cstdint>

// ArcDPS' documented x64 C ABI structures. Keep these field order/widths exact.
struct ArcAgent {
    const char* name;
    std::uintptr_t id;
    std::uint32_t prof;
    std::uint32_t elite;
    std::uint32_t self;
    std::uint16_t team;
};

struct ArcCbtEvent {
    std::uint64_t time;
    std::uint64_t src_agent;
    std::uint64_t dst_agent;
    std::int32_t value;
    std::int32_t buff_dmg;
    std::uint32_t overstack_value;
    std::uint32_t skillid;
    std::uint16_t src_instid;
    std::uint16_t dst_instid;
    std::uint16_t src_master_instid;
    std::uint16_t dst_master_instid;
    std::uint8_t iff;
    std::uint8_t buff;
    std::uint8_t result;
    std::uint8_t is_activation;
    std::uint8_t is_buffremove;
    std::uint8_t is_ninety;
    std::uint8_t is_fifty;
    std::uint8_t is_moving;
    std::uint8_t is_statechange;
    std::uint8_t is_flanking;
    std::uint8_t is_shields;
    std::uint8_t is_offcycle;
    std::uint8_t pad61;
    std::uint8_t pad62;
    std::uint8_t pad63;
    std::uint8_t pad64;
};
static_assert(sizeof(ArcCbtEvent) == 64, "ArcDPS cbtevent layout mismatch");

struct ArcDpsExports {
    std::uint64_t size;
    std::uint32_t sig;
    std::uint32_t imguivers;
    const char* out_name;
    const char* out_build;
    void* wnd_nofilter;
    void* combat;
    void* imgui;
    void* options_tab;
    void* combat_local;
    void* wnd_filter;
    void* options_windows;
};

using ArcCombatLocal = void (*)(ArcCbtEvent*, ArcAgent*, ArcAgent*, char*,
                                std::uint64_t, std::uint64_t);
using ArcCombatArea = void (*)(ArcCbtEvent*, ArcAgent*, ArcAgent*, const char*,
                               std::uint64_t, std::uint64_t);

inline constexpr std::uint8_t CBTS_BUFFINITIAL = 18;
inline constexpr std::uint8_t CBTS_BUFFACTIVE = 27;
inline constexpr std::uint8_t CBTS_BUFFDEACTIVE = 28;
inline constexpr std::uint8_t CBTS_BUFFAPPLY = 69;
inline constexpr std::uint8_t CBTS_BUFFCHANGE = 70;
inline constexpr std::uint8_t CBTS_BUFFREMOVE_SINGLE = 71;
inline constexpr std::uint8_t CBTS_BUFFREMOVE_ALL = 72;
inline constexpr std::uint8_t CBTS_MAPCHANGE = 65;
