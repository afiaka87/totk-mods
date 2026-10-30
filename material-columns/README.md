# Material Columns v0.1.0

A fifth mode for the materials quick-access menu in Tears of the Kingdom 1.2.1.
Hold D-pad Up and press Y until Material Columns appears. Move the right stick left or right
to choose a family, and up or down to choose an item within it. The four original sorts remain.

The selected family shows its upper and lower items at full opacity. Its immediate neighbors
show faded previews; farther families show only their middle item. Labels follow that focus.
Overflow fades indicate more items. Zonai devices share one family. Each family's selection
is remembered across menu openings, saves and restarts. Native drop, throw and arrow-fuse actions remain.

## Installation

Requires TotK **1.2.1**, build **9B4E43650501A4D4**. Other versions are unsupported.
Close the game before installing.

- Emulator: extract the emulator ZIP into the game's mod directory, producing
  `material-columns/exefs/` and `material-columns/romfs/`. Enable Material Columns.
- Switch with Atmosphere: extract the Switch ZIP into `atmosphere/contents/`, merging
  `0100F2C0115B6000/`. Keep both `exefs/` and `romfs/`.
- Uses **subsdk6**. Not compatible with **Fleet Sync**, or another mod supplying `subsdk6`.
- Upgrading a development build: remove only Material Columns' old `exefs/subsdk1`.
  Do not remove another mod's executable. Different slots do not guarantee hook compatibility.

Preferences and bounded profiling logs are stored under `sdmc:/material-columns/`.
The mod does not edit the game's save files. Other quick-menu replacements are unverified.
Category labels are English. Graphics and navigation were accepted in Citron; the final refactor
and slot migration preserve code behavior by offline comparison, not a new hardware play-test.
The mapped module is 376 KiB; this is not the complete mod memory cost.

## Source and tests

This is first-party source, not a complete Switch build environment. No game dumps, generated
catalogs/layout data, SDK headers, exlaunch template headers or binaries belong in this source tree.
Native compilation requires devkitA64, exlaunch, compatible engine declarations, C++20,
`TOTK_VERSION=121`, `MATERIAL_COLUMNS_VERSION="0.1.0"`, and generated `LayoutAssets.hpp` and
`MaterialCatalog.hpp` from an owned game. Include `src/` and `runtime-support/include/`.
The module uses slot6, a 64 KiB fake heap, 16 KiB JIT space, 4 KiB inline pool, 512-byte
framework log buffer and an 8 MiB process-wide NPDM reservation. The latter is shared process
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
