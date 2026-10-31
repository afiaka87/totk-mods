# Material Columns v0.1.4

A fifth mode for the materials quick-access menu in Tears of the Kingdom 1.2.1 and 1.4.2.
Hold D-pad Up and press Y until Material Columns appears. Move the right stick left or right
to choose a family, and up or down to choose an item within it. The four original sorts remain.

Families are ordered by their combined native usage counts, most used first. Ties use the existing type order; items within families keep their order.

[INI support for mod authors](docs/CATEGORY-CONFIG.md) assigns actor names to friendly category IDs or defines translated categories without rebuilding. RomFS companion files cover Remote Bombs, FuseablesEX, Invisible Attachments Shop and The Second Quest. Added-material visibility and category behavior were accepted on Citron 1.2.1 with the latter three mods merged. Remote Bombs mappings have offline coverage. The same configuration format supports both game versions; third-party material mods must separately support the installed version.

The selected family shows its upper and lower items at full opacity. Its immediate neighbors
show faded previews; farther families show only their middle item. Labels follow that focus.
Overflow fades indicate more items. Zonai devices share one family. Each family's selection
is remembered across menu openings, saves and restarts. Native drop, throw and arrow-fuse actions remain.

## Installation

Supports TotK **1.2.1** (build **9B4E43650501A4D4**) and **1.4.2** (build **5CB42B1CF25469FB**).
The same binary detects the game version at startup. Other versions are unsupported; unrecognized builds leave the mod inactive.
Close the game before installing.

- Emulator: extract the emulator ZIP into the game's mod directory, producing
  `material-columns/exefs/` and `material-columns/romfs/`. Enable Material Columns.
- Switch with Atmosphere: extract the Switch ZIP into `atmosphere/contents/`, merging
  `0100F2C0115B6000/`. Keep both `exefs/` and `romfs/`.
- Uses **subsdk6**. Not compatible with **Fleet Sync**, or another mod supplying `subsdk6`.
- Upgrading a development build: remove only Material Columns' old `exefs/subsdk1`.
  Do not remove another mod's executable. Different slots do not guarantee hook compatibility.

Preferences are stored under `sdmc:/material-columns/`. This release writes no diagnostic logs.
The mod does not edit game saves. Category labels automatically follow the system language in 16 regional locales; other quick-menu replacements are unverified.
The cursor, navigation, category order and background fixes were accepted on a real Switch.
The combined binary has offline validation against both game images and loader imports, plus host tests.
Localization was accepted on Citron and Switch; custom-material visibility and grouping were accepted on Citron 1.2.1. The final silent release has offline validation; third-party material mods were not tested on 1.4.2.
The complete memory cost on 1.4.2 has not been measured.

v0.1.4 adds automatic column-name localization and INI category support. Choose the package for your device; both contain the same binary for 1.2.1 and 1.4.2.

## Source and tests

This is first-party source, not a complete Switch build environment. No game dumps, generated
catalogs/layout data, SDK headers, exlaunch template headers or binaries belong in this source tree.
Native compilation requires devkitA64, exlaunch, compatible engine declarations, C++20,
`TOTK_VERSION=121` (SDK bindings), `MATERIAL_COLUMNS_RELEASE`, `MATERIAL_COLUMNS_VERSION="0.1.4"`, and generated `LayoutAssets.hpp` and
`MaterialCatalog.hpp` from an owned game. Include `src/` and `runtime-support/include/`.
The module uses slot6, a 64 KiB fake heap, 16 KiB JIT space, 4 KiB inline pool, 512-byte
framework log-buffer setting (unused by the silent release logger) and a 24 MiB process-wide NPDM reservation. The latter is shared process
configuration, not an estimate of this mod's RAM use.

The exlaunch InlineFloatCtx accessor fix is required; see upstream issue #28 / PR #31 at
https://github.com/shadowninja108/exlaunch. No guarantee is made that this source-only checkout
builds a Switch module without those external inputs and tooling.

Portable tests use synthetic fixtures and need no game data:

```sh
cmake -S tests -B tests/build-host
cmake --build tests/build-host --config Debug
ctest --test-dir tests/build-host -C Debug --output-on-failure
```

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## License and credits

First-party code is MIT. The executable includes external GPL-2.0 exlaunch code; see NOTICE.
The small runtime layout derivatives remain game assets and are not relicensed under MIT.
Thanks to the exlaunch and devkitPro projects. Source: https://github.com/afiaka87/totk-mods/tree/main/material-columns


Localization source: localization/columns.json. Build-time checks generate the RomFS packs; only the selected pack occupies a 2 KiB runtime buffer. Missing or invalid packs fall back to English. Tested languages worked on Citron and Switch; visual coverage of all 16 locales is not established.
