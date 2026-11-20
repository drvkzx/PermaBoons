#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Nexus.h>
#include "imgui.h"

#include "boons.h"
#include "../resources/resource.h"
#include "shared_state.h"

namespace {

AddonAPI_t* g_api = nullptr;
boonpanel::SharedState g_shared;

bool g_visible = true;
bool g_hide_title_bar = false;
bool g_show_duration = true;
bool g_show_inactive = false;
bool g_show_stack_counts = true;
// Keep the existing all-boons display by default; users can opt into a smaller set.
std::array<bool, boonpanel::kBoonCount> g_boon_enabled = [] {
    std::array<bool, boonpanel::kBoonCount> enabled{};
    enabled.fill(true);
    return enabled;
}();
bool g_horizontal_layout = true;
bool g_sort_by_category = false;
bool g_show_well_timer = true;
float g_tile_size = 42.0f;
float g_icon_spacing = 4.0f;
// Independent text styling for the three text elements drawn in the overlay.
float g_timer_font_size = 13.0f;
float g_stack_font_size = 12.0f;
float g_fallback_font_size = 14.0f;
ImVec4 g_timer_font_color = ImVec4(0.92f, 0.94f, 0.98f, 0.92f);
ImVec4 g_stack_font_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
ImVec4 g_fallback_font_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
// One shared tint for every boon duration perimeter, independent of boon type.
ImVec4 g_duration_outline_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
std::array<std::uint32_t, boonpanel::kBoonCount> g_ring_total_ms{};
std::array<std::uint32_t, boonpanel::kBoonCount> g_last_remaining_ms{};

std::string g_settings_path;
std::string g_last_saved_signature;

std::string SettingsSignature() {
    char buffer[2048]{};
    int used = std::snprintf(
        buffer, sizeof(buffer),
        "%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|",
        g_visible, g_hide_title_bar, g_show_duration, g_show_inactive,
        g_show_stack_counts, g_horizontal_layout, g_sort_by_category, g_show_well_timer, g_tile_size, g_icon_spacing,
        g_timer_font_size, g_stack_font_size, g_fallback_font_size,
        g_timer_font_color.x, g_timer_font_color.y, g_timer_font_color.z,
        g_timer_font_color.w, g_stack_font_color.x, g_stack_font_color.y,
        g_stack_font_color.z);
    std::string signature(buffer, used > 0 ? static_cast<std::size_t>(used) : 0);
    char tail[512]{};
    std::snprintf(tail, sizeof(tail),
        "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|",
        g_stack_font_color.w, g_fallback_font_color.x, g_fallback_font_color.y,
        g_fallback_font_color.z, g_fallback_font_color.w,
        g_duration_outline_color.x, g_duration_outline_color.y);
    signature += tail;
    std::snprintf(tail, sizeof(tail), "%.3f|%.3f|",
        g_duration_outline_color.z, g_duration_outline_color.w);
    signature += tail;
    for (bool enabled : g_boon_enabled) {
        signature += enabled ? "1" : "0";
    }
    return signature;
}

void SetIniValue(const char* section, const char* key, const char* value) {
    if (!g_settings_path.empty()) {
        WritePrivateProfileStringA(section, key, value, g_settings_path.c_str());
    }
}

void SaveSettingsIfChanged() {
    if (g_settings_path.empty()) return;
    const std::string signature = SettingsSignature();
    if (signature == g_last_saved_signature) return;

    char value[128]{};
    auto write_bool = [&](const char* key, bool v) {
        SetIniValue("General", key, v ? "1" : "0");
    };
    auto write_float = [&](const char* key, float v) {
        std::snprintf(value, sizeof(value), "%.4f", v);
        SetIniValue("Appearance", key, value);
    };

    write_bool("Visible", g_visible);
    write_bool("HideTitleBar", g_hide_title_bar);
    write_bool("ShowDuration", g_show_duration);
    write_bool("ShowInactive", g_show_inactive);
    write_bool("ShowStackCounts", g_show_stack_counts);
    write_bool("HorizontalLayout", g_horizontal_layout);
    write_bool("SortByCategory", g_sort_by_category);
    write_bool("ShowWellOfCorruptionTimer", g_show_well_timer);
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        char key[32]{};
        std::snprintf(key, sizeof(key), "Boon%zu", i);
        write_bool(key, g_boon_enabled[i]);
    }

    write_float("TileSize", g_tile_size);
    write_float("IconSpacing", g_icon_spacing);
    write_float("TimerFontSize", g_timer_font_size);
    write_float("StackFontSize", g_stack_font_size);
    write_float("FallbackFontSize", g_fallback_font_size);

    const ImVec4 colors[] = {g_timer_font_color, g_stack_font_color,
        g_fallback_font_color, g_duration_outline_color};
    const char* color_keys[] = {"TimerFontColor", "StackFontColor",
        "FallbackFontColor", "DurationOutlineColor"};
    for (std::size_t i = 0; i < 4; ++i) {
        std::snprintf(value, sizeof(value), "%.4f,%.4f,%.4f,%.4f",
            colors[i].x, colors[i].y, colors[i].z, colors[i].w);
        SetIniValue("Appearance", color_keys[i], value);
    }
    g_last_saved_signature = signature;
}

void LoadSettings() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&LoadSettings), &module)) {
        return;
    }
    char module_path[MAX_PATH]{};
    if (!GetModuleFileNameA(module, module_path, MAX_PATH)) return;
    char* slash = std::strrchr(module_path, '\\');
    if (!slash) slash = std::strrchr(module_path, '/');
    if (!slash) return;
    *(slash + 1) = '\0';
    g_settings_path = module_path;
    g_settings_path += "BoonPanel.ini";

    auto read_bool = [&](const char* section, const char* key, bool current) {
        return GetPrivateProfileIntA(section, key, current ? 1 : 0,
            g_settings_path.c_str()) != 0;
    };
    auto read_float = [&](const char* key, float current) {
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.4f", current);
        GetPrivateProfileStringA("Appearance", key, text, text,
            static_cast<DWORD>(sizeof(text)), g_settings_path.c_str());
        char* end = nullptr;
        const float parsed = std::strtof(text, &end);
        return end != text ? parsed : current;
    };

    g_visible = read_bool("General", "Visible", g_visible);
    g_hide_title_bar = read_bool("General", "HideTitleBar", g_hide_title_bar);
    g_show_duration = read_bool("General", "ShowDuration", g_show_duration);
    g_show_inactive = read_bool("General", "ShowInactive", g_show_inactive);
    g_show_stack_counts = read_bool("General", "ShowStackCounts", g_show_stack_counts);
    g_horizontal_layout = read_bool("General", "HorizontalLayout", g_horizontal_layout);
    g_sort_by_category = read_bool("General", "SortByCategory", g_sort_by_category);
    g_show_well_timer = read_bool("General", "ShowWellOfCorruptionTimer", g_show_well_timer);
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        char key[32]{};
        std::snprintf(key, sizeof(key), "Boon%zu", i);
        g_boon_enabled[i] = read_bool("General", key, g_boon_enabled[i]);
    }

    g_tile_size = (std::max)(28.0f, (std::min)(64.0f, read_float("TileSize", g_tile_size)));
    g_icon_spacing = (std::max)(0.0f, (std::min)(18.0f, read_float("IconSpacing", g_icon_spacing)));
    g_timer_font_size = (std::max)(8.0f, (std::min)(24.0f, read_float("TimerFontSize", g_timer_font_size)));
    g_stack_font_size = (std::max)(8.0f, (std::min)(24.0f, read_float("StackFontSize", g_stack_font_size)));
    g_fallback_font_size = (std::max)(8.0f, (std::min)(24.0f, read_float("FallbackFontSize", g_fallback_font_size)));

    const char* color_keys[] = {"TimerFontColor", "StackFontColor",
        "FallbackFontColor", "DurationOutlineColor"};
    ImVec4* colors[] = {&g_timer_font_color, &g_stack_font_color,
        &g_fallback_font_color, &g_duration_outline_color};
    for (std::size_t i = 0; i < 4; ++i) {
        char text[128]{};
        std::snprintf(text, sizeof(text), "%.4f,%.4f,%.4f,%.4f",
            colors[i]->x, colors[i]->y, colors[i]->z, colors[i]->w);
        GetPrivateProfileStringA("Appearance", color_keys[i], text, text,
            static_cast<DWORD>(sizeof(text)), g_settings_path.c_str());
        float r, g, b, a;
        if (std::sscanf(text, "%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
            *colors[i] = ImVec4(r, g, b, a);
        }
    }
    g_last_saved_signature = SettingsSignature();
}

// Offensive -> defensive -> support/utility. Stable sorting preserves the
// original boon order within each category.
int BoonCategory(std::size_t index) {
    switch (index) {
    case 2: case 3: case 5: return 0; // Fury, Might, Quickness
    case 0: case 4: case 7: case 8: case 9: return 1; // defensive
    default: return 2; // Alacrity, Regeneration, Swiftness, Vigor
    }
}

// Boon icons are embedded as PNG resources in the DLL. Nexus decodes these
// resources and uploads them to DirectX, so no network download is needed.
std::array<Texture_t*, boonpanel::kBoonCount> g_boon_textures{};

HMODULE AddonModule() {
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&AddonModule), &module);
    return module;
}

constexpr unsigned kBoonResourceIds[boonpanel::kBoonCount] = {
    IDB_BOON_AEGIS, IDB_BOON_ALACRITY, IDB_BOON_FURY, IDB_BOON_MIGHT,
    IDB_BOON_PROTECTION, IDB_BOON_QUICKNESS, IDB_BOON_REGENERATION,
    IDB_BOON_RESISTANCE, IDB_BOON_RESOLUTION, IDB_BOON_STABILITY,
    IDB_BOON_SWIFTNESS, IDB_BOON_VIGOR
};

void RequestBoonTexture(std::size_t index) {
    if (!g_api || !g_api->Textures_GetOrCreateFromResource ||
        index >= boonpanel::kBoonCount || g_boon_textures[index] != nullptr) {
        return;
    }
    HMODULE module = AddonModule();
    if (!module) return;
    char identifier[64]{};
    std::snprintf(identifier, sizeof(identifier), "TEX_BOONPANEL_%s",
        boonpanel::kBoons[index].short_name);
    g_boon_textures[index] = g_api->Textures_GetOrCreateFromResource(
        identifier, kBoonResourceIds[index], module);
}

void PreloadBoonTextures() {
    // Load every embedded icon at addon startup, including icons currently
    // disabled in the user's boon selection. Nexus reads the PNG resources locally; no web request is made.
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        RequestBoonTexture(i);
    }
}

void RefreshPendingBoonTextures() {
    // GetOrCreate returns the texture once Nexus has finished decoding/uploading. Poll only entries
    // that have not completed yet, so there is no repeated work after loading.
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        if (g_boon_textures[i] == nullptr) {
            RequestBoonTexture(i);
        }
    }
}

void DrawBoonTile(std::size_t index, std::uint32_t stacks,
                  std::uint32_t remaining_ms, bool active) {
    const auto& boon = boonpanel::kBoons[index];
    const float tile = g_tile_size;
    const float footer = g_show_duration ? 14.0f : 0.0f;
    const ImVec2 size(tile, tile + footer);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("boon_icon", size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 image_min(pos.x, pos.y);
    const ImVec2 image_max(pos.x + tile, pos.y + tile);
    if (Texture_t* texture = g_boon_textures[index]) {
        const ImU32 tint = active ? IM_COL32(255, 255, 255, 255)
                                  : IM_COL32(115, 120, 128, 155);
        draw->AddImage(reinterpret_cast<ImTextureID>(texture->Resource),
                       image_min, image_max, ImVec2(0, 0), ImVec2(1, 1), tint);
    } else {
        // Keep a legible fallback while the preloaded icon is downloading or
        // if the remote image is unavailable.
        const ImVec2 label_size = ImGui::GetFont()->CalcTextSizeA(g_fallback_font_size, 1000000.0f, 0.0f,
                                                            boon.short_name);
        ImVec4 label_color = g_fallback_font_color;
        if (!active) {
            label_color.x *= 0.62f;
            label_color.y *= 0.62f;
            label_color.z *= 0.62f;
            label_color.w *= 0.75f;
        }
        draw->AddText(nullptr, g_fallback_font_size,
                      ImVec2(pos.x + (tile - label_size.x) * 0.5f,
                             pos.y + (tile - label_size.y) * 0.5f),
                      ImGui::ColorConvertFloat4ToU32(label_color), boon.short_name);
    }

    // The duration outline follows a pentagon around the boon artwork. Its
    // progress baseline resets on application/refresh, while frame-to-frame
    // decay remains smooth. The pentagon is drawn with AddLine for ImGui 1.80.
    const ImVec2 center(pos.x + tile * 0.5f, pos.y + tile * 0.5f);
    if (active && remaining_ms > 0) {
        if (g_ring_total_ms[index] == 0 ||
            remaining_ms > g_last_remaining_ms[index] + 150u) {
            g_ring_total_ms[index] = remaining_ms;
        }
        g_last_remaining_ms[index] = remaining_ms;
        const float fraction = (std::max)(0.0f, (std::min)(1.0f,
            static_cast<float>(remaining_ms) / static_cast<float>((std::max)(1u, g_ring_total_ms[index]))));
        constexpr int kPentagonSides = 5;
        // Match the GW2 boon-frame silhouette: pointed crown and sloped
        // shoulders, then perfectly vertical side walls and a flat base.
        // The lower two corners are square (90 degrees), not tapered.
        // Coordinates are normalized to the icon tile and inset to its border.
        constexpr float shape[kPentagonSides][2] = {
            {0.50f, 0.035f}, // top point
            {0.885f, 0.255f}, // upper-right shoulder / start of vertical wall
            {0.885f, 0.925f}, // lower-right 90-degree corner
            {0.115f, 0.925f}, // lower-left 90-degree corner
            {0.115f, 0.255f}  // upper-left shoulder / end of vertical wall
        };
        std::array<ImVec2, kPentagonSides> vertices{};
        float perimeter = 0.0f;
        std::array<float, kPentagonSides> edge_lengths{};
        for (int vertex = 0; vertex < kPentagonSides; ++vertex) {
            vertices[vertex] = ImVec2(pos.x + tile * shape[vertex][0],
                                      pos.y + tile * shape[vertex][1]);
        }
        for (int edge = 0; edge < kPentagonSides; ++edge) {
            const ImVec2& a = vertices[edge];
            const ImVec2& b = vertices[(edge + 1) % kPentagonSides];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            edge_lengths[edge] = std::sqrt(dx * dx + dy * dy);
            perimeter += edge_lengths[edge];
            draw->AddLine(a, b, IM_COL32(0, 0, 0, 190), 2.5f);
        }

        // Trace the remaining duration clockwise from the top. Each edge uses
        // its own length so the progress speed stays uniform on this asymmetric shape.
        if (fraction > 0.001f) {
            float distance_left = fraction * perimeter;
            for (int edge = 0; edge < kPentagonSides && distance_left > 0.0f; ++edge) {
                const ImVec2 a = vertices[edge];
                const ImVec2 b = vertices[(edge + 1) % kPentagonSides];
                const float portion = (std::min)(1.0f, distance_left / edge_lengths[edge]);
                const ImVec2 end(a.x + (b.x - a.x) * portion,
                                 a.y + (b.y - a.y) * portion);
                draw->AddLine(a, end, ImGui::ColorConvertFloat4ToU32(g_duration_outline_color), 2.5f);
                distance_left -= edge_lengths[edge];
            }
        }
    } else {
        g_ring_total_ms[index] = 0;
        g_last_remaining_ms[index] = 0;
    }

    if (active && boon.stacks_in_intensity && g_show_stack_counts && stacks > 0) {
        char stack_text[8]{};
        std::snprintf(stack_text, sizeof(stack_text), "%u", stacks);
        const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(g_stack_font_size, 1000000.0f, 0.0f,
                                                           stack_text);
        const ImVec2 badge_min(pos.x + tile - text_size.x - 10.0f, pos.y + 2.0f);
        const ImVec2 badge_max(pos.x + tile - 2.0f, pos.y + text_size.y + 6.0f);
        draw->AddRectFilled(badge_min, badge_max, IM_COL32(0, 0, 0, 205), 4.0f);
        draw->AddText(nullptr, g_stack_font_size,
                      ImVec2(badge_min.x + 3.0f, badge_min.y + 1.0f),
                      ImGui::ColorConvertFloat4ToU32(g_stack_font_color), stack_text);
    }

    if (g_show_duration && active) {
        char timer[16]{};
        if (remaining_ms >= 60000) {
            std::snprintf(timer, sizeof(timer), "%um", static_cast<unsigned>(remaining_ms / 60000));
        } else {
            std::snprintf(timer, sizeof(timer), "%.1f", static_cast<double>(remaining_ms) / 1000.0);
        }
        const ImVec2 timer_size = ImGui::GetFont()->CalcTextSizeA(g_timer_font_size, 1000000.0f, 0.0f,
                                                           timer);
        const ImVec2 timer_pos(pos.x + (tile - timer_size.x) * 0.5f, pos.y + tile - 1.0f);
        draw->AddText(nullptr, g_timer_font_size, timer_pos,
                      ImGui::ColorConvertFloat4ToU32(g_timer_font_color), timer);
    }

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(boon.name);
        if (active) {
            if (boon.stacks_in_intensity) {
                ImGui::Text("Stacks: %u", stacks);
                ImGui::Text("Next stack expires in: %.2f s", static_cast<double>(remaining_ms) / 1000.0);
            } else {
                ImGui::Text("Remaining: %.2f s", static_cast<double>(remaining_ms) / 1000.0);
            }
        } else {
            ImGui::TextUnformatted("Not currently active");
        }
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

void AddonRender() {
    // Keep loading independent of visibility and boon-selection settings.
    RefreshPendingBoonTextures();

    boonpanel::SharedSnapshot snapshot{};
    const bool read_ok = g_shared.Read(snapshot);
    // Match ArcDPS windows: hide the overlay while a map transition is in progress.
    // ArcDPS clears this flag when the local player agent is created in the new map.
    if (read_ok && snapshot.map_loading != 0) {
        return;
    }
    const std::uint64_t now = boonpanel::TickNowMs();

    // Independent window: it remains visible at 0.0 after the first trigger,
    // until the user hides it in Nexus options. Countdown is event-time based.
    if (read_ok && g_show_well_timer && snapshot.well_timer_expires_at_ms != 0) {
        const std::uint64_t remaining_ms = snapshot.well_timer_expires_at_ms > now
            ? snapshot.well_timer_expires_at_ms - now : 0;
        ImGuiWindowFlags timer_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("Well of Corruption##BoonPanelWellTimer", nullptr, timer_flags)) {
            ImGui::TextUnformatted("Well of Corruption");
            ImGui::SameLine();
            ImGui::Text("%.1f s", static_cast<double>(remaining_ms) / 1000.0);
        }
        ImGui::End();
    }

    if (!g_visible) {
        SaveSettingsIfChanged();
        return;
    }

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoBackground;
    if (g_hide_title_bar) {
        window_flags |= ImGuiWindowFlags_NoTitleBar;
    }
    if (ImGui::Begin("Boon Panel##BoonPanel", &g_visible, window_flags)) {
        std::array<std::uint32_t, boonpanel::kBoonCount> remaining{};
        std::array<std::uint32_t, boonpanel::kBoonCount> stacks_now{};
        std::array<bool, boonpanel::kBoonCount> active{};

        if (read_ok) {
            for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
                std::uint64_t total_remaining = 0;
                std::uint64_t next_expiry_remaining = UINT64_MAX;
                const std::uint32_t count = (std::min)(
                    snapshot.track_count[i], static_cast<std::uint32_t>(boonpanel::kMaxTrackedStacks));
                for (std::uint32_t j = 0; j < count; ++j) {
                    const std::uint64_t expiry = snapshot.expires_at_ms[i][j];
                    if (expiry > now) {
                        ++stacks_now[i];
                        const std::uint64_t left = expiry - now;
                        total_remaining += left;
                        next_expiry_remaining = (std::min)(next_expiry_remaining, left);
                    }
                }
                if (boonpanel::kBoons[i].stacks_in_intensity) {
                    stacks_now[i] = (std::min)(stacks_now[i], 25u);
                } else if (stacks_now[i] > 0) {
                    stacks_now[i] = 1;
                }
                const std::uint64_t display_remaining = boonpanel::kBoons[i].stacks_in_intensity
                    ? (next_expiry_remaining == UINT64_MAX ? 0ull : next_expiry_remaining)
                    : total_remaining;
                remaining[i] = static_cast<std::uint32_t>((std::min<std::uint64_t>)(display_remaining, 600000ull));
                active[i] = stacks_now[i] > 0;
            }
        }

        if (!read_ok) {
            ImGui::TextDisabled("Connecting to ArcDPS boon feed...");
        }
        // Reset ring history even when inactive icons are hidden, so a later
        // reapplication always starts with a fresh full-duration ring.
        for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
            if (!active[i] || !g_boon_enabled[i]) {
                g_ring_total_ms[i] = 0;
                g_last_remaining_ms[i] = 0;
            }
        }

        std::array<std::size_t, boonpanel::kBoonCount> order{};
        for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) order[i] = i;
        if (g_sort_by_category) {
            std::stable_sort(order.begin(), order.end(), [](std::size_t a, std::size_t b) {
                return BoonCategory(a) < BoonCategory(b);
            });
        }

        int shown = 0;
        for (const std::size_t i : order) {
            if (!g_boon_enabled[i]) continue;
            if (!g_show_inactive && !active[i]) continue;
            if (shown > 0 && g_horizontal_layout) {
                ImGui::SameLine(0.0f, g_icon_spacing);
            }
            DrawBoonTile(i, stacks_now[i], remaining[i], active[i]);
            ++shown;
        }

        // Right-click the panel (including over boon icons) to access the
        // compact Style menu, matching the familiar ArcDPS window workflow.
        if (ImGui::BeginPopupContextWindow("BoonPanelContext", 1, true)) {
            if (ImGui::BeginMenu("Style")) {
                ImGui::MenuItem("Hide title bar", nullptr, &g_hide_title_bar);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    SaveSettingsIfChanged();
    ImGui::End();
}

void AddonOptions() {
    ImGui::TextUnformatted("BoonPanel display settings");
    ImGui::Separator();

    ImGui::Checkbox("Show BoonPanel overlay", &g_visible);
    ImGui::Checkbox("Show boon durations", &g_show_duration);
    ImGui::Checkbox("Show stack counts", &g_show_stack_counts);
    ImGui::Checkbox("Show inactive boon icons", &g_show_inactive);
    ImGui::Checkbox("Show Well of Corruption timer window", &g_show_well_timer);

    ImGui::Spacing();
    ImGui::TextUnformatted("Boon selection");
    ImGui::TextDisabled("Only checked boons are shown in the overlay.");
    if (ImGui::Button("Select all")) {
        g_boon_enabled.fill(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Select none")) {
        g_boon_enabled.fill(false);
    }
    for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox(boonpanel::kBoons[i].name, &g_boon_enabled[i]);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Layout");
    ImGui::TextUnformatted("Boon arrangement");
    if (ImGui::RadioButton("Horizontal", g_horizontal_layout)) {
        g_horizontal_layout = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Vertical", !g_horizontal_layout)) {
        g_horizontal_layout = false;
    }
    ImGui::Checkbox("Sort by boon category", &g_sort_by_category);
    ImGui::SliderFloat("Icon size", &g_tile_size, 28.0f, 64.0f, "%.0f px");
    ImGui::SliderFloat("Icon spacing", &g_icon_spacing, 0.0f, 18.0f, "%.0f px");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Text appearance");
    ImGui::TextUnformatted("Boon duration (seconds)");
    ImGui::ColorEdit4("Duration outline color", &g_duration_outline_color.x);
    ImGui::Spacing();
    ImGui::SliderFloat("Duration font size", &g_timer_font_size, 8.0f, 24.0f, "%.0f px");
    ImGui::ColorEdit4("Duration font color", &g_timer_font_color.x);

    ImGui::Spacing();
    ImGui::TextUnformatted("Boon stacks");
    ImGui::SliderFloat("Stack font size", &g_stack_font_size, 8.0f, 24.0f, "%.0f px");
    ImGui::ColorEdit4("Stack font color", &g_stack_font_color.x);

    ImGui::Spacing();
    ImGui::TextUnformatted("Fallback boon name (while icon is unavailable)");
    ImGui::SliderFloat("Boon name font size", &g_fallback_font_size, 8.0f, 24.0f, "%.0f px");
    ImGui::ColorEdit4("Boon name font color", &g_fallback_font_color.x);

    ImGui::Spacing();
    ImGui::TextDisabled("The boon panel background is transparent. Countdown rings update continuously.");
    SaveSettingsIfChanged();
}

void AddonLoad(AddonAPI_t* api) {
    g_api = api;
    if (!g_api) {
        return;
    }

    // Nexus owns the context and frame lifecycle. Use the same ImGui version
    // and allocator as Nexus; do not create a context or call NewFrame/Render.
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_api->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void* (*)(std::size_t, void*)>(g_api->ImguiMalloc),
        reinterpret_cast<void (*)(void*, void*)>(g_api->ImguiFree));

    LoadSettings();
    g_shared.Open();
    PreloadBoonTextures();
    g_api->GUI_Register(RT_Render, AddonRender);
    g_api->GUI_Register(RT_OptionsRender, AddonOptions);
}

void AddonUnload() {
    if (g_api) {
        g_api->GUI_Deregister(AddonRender);
        g_api->GUI_Deregister(AddonOptions);
        g_api = nullptr;
    }
    g_boon_textures.fill(nullptr);
    g_shared.Close();
}

AddonDefinition_t g_addon_definition{};

} // namespace

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_addon_definition = {};
    g_addon_definition.Signature = static_cast<std::uint32_t>(-1);
    g_addon_definition.APIVersion = NEXUS_API_VERSION;
    g_addon_definition.Name = "BoonPanel";
    g_addon_definition.Version.Major = 0;
    g_addon_definition.Version.Minor = 1;
    g_addon_definition.Version.Build = 0;
    g_addon_definition.Version.Revision = 0;
    g_addon_definition.Author = "BoonPanel community";
    g_addon_definition.Description = "Movable local-player boon panel for PvE, WvW and PvP using ArcDPS events.";
    g_addon_definition.Load = &AddonLoad;
    g_addon_definition.Unload = &AddonUnload;
    g_addon_definition.Flags = EAddonFlags::AF_None;
    return &g_addon_definition;
}
