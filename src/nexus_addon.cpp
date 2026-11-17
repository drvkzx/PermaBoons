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

void DrawBoonTile(std::size_t index, std::uint32_t stacks,
                  std::uint32_t remaining_ms, bool active) {
    const auto& boon = boonpanel::kBoons[index];
    const float tile = g_tile_size;
    const float footer = g_show_duration ? 18.0f : 8.0f;
    const ImVec2 size(tile, tile + footer);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("boon_tile", size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 base = active ? IM_COL32(boon.display_color & 0xFF,
                                          (boon.display_color >> 8) & 0xFF,
                                          (boon.display_color >> 16) & 0xFF, 238)
                              : IM_COL32(50, 54, 62, 170);
    const ImU32 border = active ? IM_COL32(225, 225, 230, 210)
                                : IM_COL32(115, 120, 130, 130);
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + tile), base, 5.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + tile), border, 5.0f, 0, 1.0f);

    const ImVec2 label_size = ImGui::CalcTextSize(boon.short_name);
    draw->AddText(ImVec2(pos.x + (tile - label_size.x) * 0.5f,
                         pos.y + (tile - label_size.y) * 0.5f),
                  active ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 184, 192, 210),
                  boon.short_name);

    if (active && boon.stacks_in_intensity && g_show_stack_counts && stacks > 0) {
        char stack_text[8]{};
        std::snprintf(stack_text, sizeof(stack_text), "%u", stacks);
        const ImVec2 text_size = ImGui::CalcTextSize(stack_text);
        draw->AddRectFilled(ImVec2(pos.x + tile - text_size.x - 8.0f, pos.y + 2.0f),
                            ImVec2(pos.x + tile - 2.0f, pos.y + text_size.y + 5.0f),
                            IM_COL32(0, 0, 0, 175), 3.0f);
        draw->AddText(ImVec2(pos.x + tile - text_size.x - 5.0f, pos.y + 2.5f),
                      IM_COL32(255, 255, 255, 255), stack_text);
    }

    if (g_show_duration && active) {
        char timer[16]{};
        const float seconds = static_cast<float>(remaining_ms) / 1000.0f;
        if (remaining_ms >= 60000) {
            std::snprintf(timer, sizeof(timer), "%um", static_cast<unsigned>(remaining_ms / 60000));
        } else {
            std::snprintf(timer, sizeof(timer), "%.1f", seconds);
        }
        const ImVec2 timer_size = ImGui::CalcTextSize(timer);
        const ImVec2 timer_pos(pos.x + (tile - timer_size.x) * 0.5f, pos.y + tile + 1.0f);
        draw->AddText(timer_pos, IM_COL32(245, 245, 245, 245), timer);
    }

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(boon.name);
        if (active) {
            if (boon.stacks_in_intensity) {
                ImGui::Text("Stacks: %u", stacks);
                ImGui::Text("Next stack: %.2f s", static_cast<double>(remaining_ms) / 1000.0);
            } else {
                ImGui::Text("Remaining: %.2f s", static_cast<double>(remaining_ms) / 1000.0);
            }
        } else {
            ImGui::TextUnformatted("Not currently tracked");
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
        ImGui::TextDisabled("Local player  |  ArcDPS event feed");
        ImGui::SameLine();
        ImGui::TextDisabled("WvW supported where events are visible");
        ImGui::Separator();

        std::array<std::uint32_t, boonpanel::kBoonCount> remaining{};
        std::array<std::uint32_t, boonpanel::kBoonCount> stacks_now{};
        std::array<bool, boonpanel::kBoonCount> active{};
        std::uint32_t active_count = 0;

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
                if (active[i]) {
                    ++active_count;
                }
            }
        }

        if (!read_ok) {
            ImGui::TextWrapped("DIAGNOSTIC: Shared bridge is not readable. Check that both DLL copies are installed in the same GW2 process.");
        } else {
            const auto& d = snapshot.diagnostics;
            ImGui::Text("Bridge: %s | Local player agent: %s", d.bridge_open ? "OPEN" : "CLOSED",
                        d.local_agent_id ? "KNOWN" : "UNKNOWN");
            ImGui::Text("Callbacks: local %llu | area %llu | null-agent %llu",
                        static_cast<unsigned long long>(d.local_callbacks),
                        static_cast<unsigned long long>(d.area_callbacks),
                        static_cast<unsigned long long>(d.null_events));
            ImGui::Text("Boon events: %llu | target matches: %llu",
                        static_cast<unsigned long long>(d.boon_events),
                        static_cast<unsigned long long>(d.local_target_matches));
            ImGui::Text("Local agent ID: %llu | Last skill: %u | value: %d ms",
                        static_cast<unsigned long long>(d.local_agent_id), d.last_skill_id, d.last_value);
            ImGui::Text("Last event: statechange=%u buff=%u remove=%u | src self=%u dst self=%u",
                        d.last_statechange, d.last_buff, d.last_buffremove,
                        d.last_src_self, d.last_dst_self);
            ImGui::Text("Last agents: %s -> %s", d.last_src_name, d.last_dst_name);
            if (snapshot.feed_seen == 0 || active_count == 0) {
                ImGui::TextWrapped("No active boon durations yet. Apply a boon to yourself and watch the counters above.");
            }
            if (ImGui::CollapsingHeader("Diagnostic details")) {
                ImGui::TextDisabled("If callbacks stay at zero, ArcDPS did not load this extension or callback registration failed.");
                ImGui::TextDisabled("If callbacks rise but boon events stay at zero, ArcDPS is not sending recognized boon events.");
                ImGui::TextDisabled("If boon events rise but target matches stay at zero, local-agent identification/target matching is the problem.");
                ImGui::TextDisabled("If target matches rise but no tiles activate, inspect skill IDs, duration values and shared state.");
            }
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
            ImGui::Checkbox("Show inactive boons", &g_show_inactive);
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
