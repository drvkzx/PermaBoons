#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>

#include <Nexus.h>
#include "imgui.h"

#include "boons.h"
#include "shared_state.h"

namespace {

AddonAPI_t* g_api = nullptr;
boonpanel::SharedState g_shared;

bool g_visible = true;
bool g_show_duration = true;
bool g_show_inactive = false;
bool g_show_stack_counts = true;
float g_tile_size = 48.0f;
int g_columns = 6;

// These are the original Guild Wars 2 boon icons mirrored by the GW2 Wiki.
// Nexus downloads them asynchronously and keeps the resulting textures cached
// for the lifetime of the game. The special-file endpoint redirects to the PNG.
constexpr const char* kBoonIconFiles[boonpanel::kBoonCount] = {
    "Aegis.png", "Alacrity.png", "Fury.png", "Might.png",
    "Protection.png", "Quickness.png", "Regeneration.png", "Resistance.png",
    "Resolution.png", "Stability.png", "Swiftness.png", "Vigor.png"
};

void DrawBoonTile(std::size_t index, std::uint32_t stacks,
                  std::uint32_t remaining_ms, bool active) {
    const auto& boon = boonpanel::kBoons[index];
    const float tile = g_tile_size;
    const float footer = g_show_duration ? 17.0f : 2.0f;
    const ImVec2 size(tile, tile + footer);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("boon_icon", size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 image_min(pos.x, pos.y);
    const ImVec2 image_max(pos.x + tile, pos.y + tile);
    draw->AddRectFilled(image_min, image_max, IM_COL32(12, 14, 18, 220), 5.0f);

    // GetOrCreate is deliberately called each frame until the asynchronous
    // download finishes; after that Nexus returns the cached texture.
    if (g_api && g_api->Textures_GetOrCreateFromURL) {
        char identifier[64]{};
        std::snprintf(identifier, sizeof(identifier), "TEX_BOONPANEL_%s", boon.short_name);
        char endpoint[160]{};
        std::snprintf(endpoint, sizeof(endpoint), "/wiki/Special:Redirect/file/%s", kBoonIconFiles[index]);
        if (auto* texture = g_api->Textures_GetOrCreateFromURL(
                identifier, "https://wiki.guildwars2.com", endpoint)) {
            const ImU32 tint = active ? IM_COL32(255, 255, 255, 255)
                                      : IM_COL32(115, 120, 128, 155);
            draw->AddImage(reinterpret_cast<ImTextureID>(texture->Resource),
                           image_min, image_max, ImVec2(0, 0), ImVec2(1, 1), tint);
        } else {
            // Keep a legible fallback while the icon is downloading or if the
            // remote image is unavailable.
            const ImVec2 label_size = ImGui::CalcTextSize(boon.short_name);
            draw->AddText(ImVec2(pos.x + (tile - label_size.x) * 0.5f,
                                 pos.y + (tile - label_size.y) * 0.5f),
                          active ? IM_COL32(255, 255, 255, 255)
                                 : IM_COL32(155, 160, 168, 190), boon.short_name);
        }
    }

    draw->AddRect(image_min, image_max,
                  active ? IM_COL32(238, 238, 245, 220) : IM_COL32(90, 95, 105, 150),
                  5.0f, 0, active ? 1.4f : 1.0f);

    if (active && boon.stacks_in_intensity && g_show_stack_counts && stacks > 0) {
        char stack_text[8]{};
        std::snprintf(stack_text, sizeof(stack_text), "%u", stacks);
        const ImVec2 text_size = ImGui::CalcTextSize(stack_text);
        const ImVec2 badge_min(pos.x + tile - text_size.x - 10.0f, pos.y + 2.0f);
        const ImVec2 badge_max(pos.x + tile - 2.0f, pos.y + text_size.y + 6.0f);
        draw->AddRectFilled(badge_min, badge_max, IM_COL32(0, 0, 0, 205), 4.0f);
        draw->AddText(ImVec2(badge_min.x + 3.0f, badge_min.y + 1.0f),
                      IM_COL32(255, 255, 255, 255), stack_text);
    }

    if (g_show_duration && active) {
        char timer[16]{};
        if (remaining_ms >= 60000) {
            std::snprintf(timer, sizeof(timer), "%um", static_cast<unsigned>(remaining_ms / 60000));
        } else {
            std::snprintf(timer, sizeof(timer), "%.1f", static_cast<double>(remaining_ms) / 1000.0);
        }
        const ImVec2 timer_size = ImGui::CalcTextSize(timer);
        const ImVec2 bar_min(pos.x + 1.0f, pos.y + tile - 14.0f);
        const ImVec2 bar_max(pos.x + tile - 1.0f, pos.y + tile - 1.0f);
        draw->AddRectFilled(bar_min, bar_max, IM_COL32(0, 0, 0, 185), 3.0f);
        const ImVec2 timer_pos(pos.x + (tile - timer_size.x) * 0.5f,
                              pos.y + tile - timer_size.y - 2.0f);
        draw->AddText(ImVec2(timer_pos.x + 1.0f, timer_pos.y + 1.0f),
                      IM_COL32(0, 0, 0, 255), timer);
        draw->AddText(timer_pos, IM_COL32(255, 255, 255, 255), timer);
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
    if (!g_visible) {
        return;
    }

    boonpanel::SharedSnapshot snapshot{};
    const bool read_ok = g_shared.Read(snapshot);
    const std::uint64_t now = boonpanel::TickNowMs();

    if (ImGui::Begin("Boon Panel##BoonPanel", &g_visible,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("ACTIVE BOONS");
        ImGui::SameLine();
        ImGui::TextDisabled("Local character");
        ImGui::Separator();

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
        int shown = 0;
        for (std::size_t i = 0; i < boonpanel::kBoonCount; ++i) {
            if (!g_show_inactive && !active[i]) {
                continue;
            }
            if (shown > 0 && (shown % g_columns) != 0) {
                ImGui::SameLine(0.0f, 5.0f);
            }
            DrawBoonTile(i, stacks_now[i], remaining[i], active[i]);
            ++shown;
            if ((shown % g_columns) == 0) {
                ImGui::Spacing();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Panel settings")) {
            ImGui::Checkbox("Show durations", &g_show_duration);
            ImGui::SameLine();
            ImGui::Checkbox("Show stack counts", &g_show_stack_counts);
            ImGui::Checkbox("Show inactive icons", &g_show_inactive);
            ImGui::SliderFloat("Tile size", &g_tile_size, 32.0f, 72.0f, "%.0f px");
            ImGui::SliderInt("Columns", &g_columns, 3, 12);
        }
    }
    ImGui::End();
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

    g_shared.Open();
    g_api->GUI_Register(RT_Render, AddonRender);
}

void AddonUnload() {
    if (g_api) {
        g_api->GUI_Deregister(AddonRender);
        g_api = nullptr;
    }
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
