## Optional compatibility edition v0.15.1

For Ascend Through Depth Ceiling **free v1.0.5**, use the combined compatibility
download instead of both original patches. It preserves upward acceleration and
Descend drop/return, tested on Switch 1.2.1. See [COMPATIBILITY.md](COMPATIBILITY.md)
for install instructions and attribution. Standalone Descend v0.15.0 is unchanged.

# Descend v0.15.0

Descend lets Link travel down through the floor, preview the space beneath it, then drop or return to where he started.

While aiming Ascend, click the left stick to switch to Descend. Press A at a green marker to travel down.
At the exit preview, A drops and B returns. Click the left stick again to use ordinary Ascend.
The native prompt reads Descend while aiming downward.

A red marker means the current spot cannot be used. It warms toward green near a usable spot found by the
sampled search; only full green means you can descend here. Move slowly for the cue to catch up. It can miss
small openings and does not guarantee there is a usable spot nearby.

## Installation

Requires Tears of the Kingdom **1.2.1**, build **9B4E43650501A4D4**.

- Emulator: extract the `descend` folder into the game's mod directory and enable Descend.
- Atmosphere: copy the archive's `exefs_patches/descend` folder into `atmosphere/exefs_patches/descend` on the SD card.
  The `exefs/descend.pchtxt` file is for emulators; do not copy it into Atmosphere's title override directory.
- Remove or disable older Descend installations, including `descend-patch`, before enabling this version.
  When replacing the old subsdk version, remove its entire old Descend folder first so `subsdk9` and `main.npdm`
  cannot remain behind. Keep only one Descend installation enabled.

This is a static pchtxt/IPS32 patch. It uses no subsdk slot, replaces no game resources and needs no resource table.
The two patch formats contain the same changes. Ordinary Ascend remains available; Infinite Ascend was used
alongside Descend during development. Mods editing the same hooks or code padding can conflict. The local
known-patch survey found no overlaps; that does not cover every mod.

## Limits and testing

Tested in Citron and on a physical Switch with TotK 1.2.1.
Sloped exits can obscure Link's legs in the preview. Terrain checks intentionally refuse uncertain exits;
testing does not establish safety for every terrain arrangement.

The accepted build showed no missing swirls, black squares, exposed walls or lost controls in the reported tests.
The release cleanup retains its behavior, subject to the recorded native comparison and automated validation.

Fixed additional game RAM is 8 KiB, with 6,256 bytes used, within a 64 KiB virtual-address reservation.
Code and a 16-byte anchor reuse existing image padding. Stack use and emulator JIT overhead are not included.

## Source

First-party source is MIT licensed, copyright Clay Mullis. No game executable or resource data is included.
The `src` directory contains geometry, target selection, travel/camera state and native callbacks.
`src/program/hooks.json` is the static hook inventory. `patch` supplies initialization and bounded logging;
`tools` compiles, places and validates the patch. Shared first-party headers are in `support/include`.
The native pipeline requires a legally obtained clean game dump, the development toolchain and the private
checkout's patch-placement inputs; this source export is not a self-contained game-dump or SDK distribution.

Pure host tests can be built independently with CMake 3.25+ and a C++23 compiler:

```sh
cmake -S tests -B build-tests
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

The tests fetch doctest v2.4.11 if it is not supplied through `-DDOCTEST_DIR=...`.
Native offline checks additionally cover instruction relocation, original-call paths, full register preservation,
effects, marker behavior, animation/camera ownership and rejected patch placements. Mocked native services in
those tests do not establish real rendering or hardware compatibility.

Source repository: https://github.com/afiaka87/totk-mods/tree/main/descend
