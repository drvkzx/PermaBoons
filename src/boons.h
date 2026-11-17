#pragma once

#include <cstddef>
#include <cstdint>

namespace boonpanel {

enum class Boon : std::size_t {
    Aegis = 0,
    Alacrity,
    Fury,
    Might,
    Protection,
    Quickness,
    Regeneration,
    Resistance,
    Resolution,
    Stability,
    Swiftness,
    Vigor,
    Count
};

struct BoonInfo {
    std::uint32_t id;
    const char* name;
    const char* short_name;
    bool stacks_in_intensity;
    std::uint32_t display_color;
};

inline constexpr BoonInfo kBoons[] = {
    {743,   "Aegis",        "AEG", false, 0xFFB6A36A},
    {30328, "Alacrity",     "ALA", false, 0xFF9A6AC7},
    {725,   "Fury",         "FUR", false, 0xFF4A9CCB},
    {740,   "Might",        "MIG", true,  0xFFB85A48},
    {717,   "Protection",   "PRO", false, 0xFF6D9F59},
    {1187,  "Quickness",    "QUI", false, 0xFFCB9C45},
    {718,   "Regeneration", "REG", false, 0xFF53A986},
    {26980, "Resistance",   "RES", false, 0xFF8B70B6},
    {873,   "Resolution",   "RSL", false, 0xFFBE8B55},
    {1122,  "Stability",    "STA", true,  0xFF9D9B48},
    {719,   "Swiftness",    "SWI", false, 0xFF54A7C9},
    {726,   "Vigor",        "VIG", false, 0xFF8EAA5B},
};

inline constexpr std::size_t kBoonCount = static_cast<std::size_t>(Boon::Count);
static_assert(sizeof(kBoons) / sizeof(kBoons[0]) == kBoonCount);

inline int IndexForSkillId(std::uint32_t skill_id) noexcept {
    for (std::size_t i = 0; i < kBoonCount; ++i) {
        if (kBoons[i].id == skill_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace boonpanel
