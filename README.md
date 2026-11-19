# BoonPanel (Nexus + ArcDPS)

A movable Guild Wars 2 boon panel for the local player. It is built as one C++ DLL with two load interfaces:

- **Nexus** draws the movable ImGui panel and loads the boon artwork as textures.
- **ArcDPS** supplies live local-player boon events through its combat extension callbacks.

The DLL is built once, then copied under two names so both loaders can load their own module instance:

```text
Guild Wars 2/
├── addons/
│   └── BoonPanel.dll
└── bin64/
    └── arcdps_BoonPanel.dll
```

## Features

- Displays the 12 standard boons using their original Guild Wars 2 boon artwork: Aegis, Alacrity, Fury, Might, Protection, Quickness, Regeneration, Resistance, Resolution, Stability, Swiftness and Vigor.
- Uses Nexus' texture service to download and cache the small PNG icons from the Guild Wars 2 Wiki's file redirects. The first load requires an internet connection; icons are cached by Nexus for the game session. If an icon cannot be loaded, its short text label is shown as a fallback.
- Shows a smooth shield-shaped five-sided countdown outline fitted to each active boon icon, plus a compact numeric timer below it. The pentagon outline resets when a boon is refreshed; Might and Stability retain their stack badges and show the next stack expiry.
- Uses a transparent, draggable Nexus/ImGui window. The default layout is a compact horizontal row; all controls are available from **Nexus → Addons → BoonPanel → Configure** (icon size, icon spacing, optional category sorting, horizontal/grid layout, inactive icons, durations, stack counts and overlay visibility). Category sorting groups offensive boons first, defensive boons second, and support/utility boons last.
- Tracks boons on the local character whether applied by you or by an ally. Expiration timestamps are shared between the Nexus and ArcDPS module instances so countdowns tick smoothly.
- Compensates for ArcDPS delayed area events using event timestamps; it does not add an artificial display delay.
- Supports classic ArcDPS buff events (`buff == 1`) and newer dedicated buff statechanges (`CBTS_BUFFAPPLY`/`BUFFCHANGE`/`BUFFREMOVE`).
- Works in WvW to the extent ArcDPS exposes the local player's boon events in that situation.

## Artwork and attribution

The boon PNGs are the Guild Wars 2 boon icons mirrored by the [Guild Wars 2 Wiki](https://wiki.guildwars2.com/wiki/Category:Boon_icons). The game artwork and Guild Wars 2 marks belong to ArenaNet/NCSOFT. BoonPanel is an independent community addon and is not affiliated with ArenaNet. The icon files are loaded from the wiki's `Special:Redirect/file/` endpoint through Nexus' asynchronous texture API; they are not bundled into the DLL.

## Important WvW/data limitation

Nexus' public shared game data does not include a complete active-boon snapshot. The data bridge therefore uses ArcDPS realtime combat callbacks. ArcDPS documents boon apply/change/remove events as visibility-limited; initial boon state outside an instance is limited to squad information. As a result:

- A boon already active before tracking starts may not appear until it is applied or refreshed again.
- Boon events that ArcDPS does not expose cannot be inferred by this addon.
- The panel shows **your character's** boons, including boons supplied by allies; it does not display every ally/enemy's boon bar.

No memory scanning, injection, or hidden-state reading is implemented. The addon does not bypass ArenaNet/ArcDPS visibility limits. Review the Guild Wars 2 third-party program policy and current ArcDPS/Nexus addon policies before use.

## Build on GitHub

1. Create an empty GitHub repository.
2. Upload/push the contents of this folder to the repository's root.
3. Open **Actions** and enable workflows if GitHub asks.
4. Run **Build BoonPanel** using **Run workflow**, or push a commit.
5. Open the completed workflow run and download the `BoonPanel-Windows-x64` artifact.
6. Extract the ZIP into your Guild Wars 2 installation directory, keeping the `addons` and `bin64` folders intact.
7. Restart the game. Ensure both Nexus and ArcDPS are installed and enabled.

To publish a GitHub Release automatically, push a version tag such as `v0.2.1`. The workflow builds the ZIP and attaches it to the release.

The workflow uses `windows-2022`, CMake, the official `RaidcoreGG/Nexus-API` header repository and Dear ImGui v1.80. Nexus requires the exact ImGui 1.80 structure layout; do not change this dependency unless Nexus updates its ABI requirements.

## Build locally (Windows x64)

Requirements: Visual Studio 2022 C++ workload, Git, and CMake 3.25 or newer.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The install layout is produced at `build/package/`.

## Troubleshooting

- **Nexus does not list the addon:** confirm `addons/BoonPanel.dll` exists, is 64-bit, and was built against the current Nexus SDK header/API version.
- **ArcDPS does not list the extension:** confirm the file is named exactly `arcdps_BoonPanel.dll` and is in `bin64` (or the GW2 root directory). Confirm ArcDPS itself is installed.
- **Icons show text abbreviations instead of artwork:** allow Nexus to access the Guild Wars 2 Wiki over HTTPS; the image texture service loads the icon files asynchronously. The abbreviation is a fallback if an image request fails.
- **Panel does not show a boon:** apply/reapply the boon after both modules load. ArcDPS cannot provide events filtered by its visibility rules.
- **Some boon timers/stacks are missing:** the realtime feed is not a guaranteed full-state API; refer to the WvW/data limitation above.

## Project layout

```text
src/
  arcdps_api.h       ArcDPS ABI declarations used by this project
  arc_extension.cpp ArcDPS callback, boon tracking and DLL exports
  boons.h            Boon names, IDs and stacking metadata
  nexus_addon.cpp    Nexus export, texture loading and icon-based ImGui panel
  shared_state.h/.cpp Named process-local shared-memory snapshot
.github/workflows/
  build.yml          GitHub Actions Windows x64 build and release workflow
CMakeLists.txt       Dependency fetch and DLL/package build
```

## License

The addon source is provided under the MIT License. Guild Wars 2 artwork is not covered by that license and remains the property of its respective rights holders.


### Pentagon duration outline

Active boon duration is shown as a smooth, boon-colored progress outline following the boon artwork’s asymmetric shield-shaped perimeter (pointed top, sloped shoulders, tapered sides and flat base). The subdued full pentagon remains visible as the track; the colored portion shrinks as the boon expires and resets when the boon is applied or refreshed.

### Boon selection

Open Nexus Addons → BoonPanel → Configure and use the **Boon selection** checkboxes to choose which boons appear in the overlay. **Select all** and **Select none** provide quick presets. All boons are enabled by default to preserve the original display. Filtering affects only the displayed tiles; ArcDPS tracking continues for every supported boon.

### In-game window style menu

Right-click anywhere on the BoonPanel window (including over a boon icon) to open the context menu. Choose **Style → Hide title bar** to remove the title bar while keeping the panel movable/resizable behavior provided by ImGui. The title bar is shown by default.


### Horizontal and vertical boon arrangement

Open Nexus Addons → BoonPanel → Configure. Under **Layout → Boon arrangement**, choose **Horizontal** to place enabled boon icons in a row, or **Vertical** to stack them in a single column. Icon spacing and icon size apply to both arrangements.
