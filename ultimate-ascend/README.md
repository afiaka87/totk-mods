# Ultimate Ascend v0.16.0

Ultimate Ascend lets Link travel down through floors, preview the space below, then drop or return.
It can reach the Depths from the surface or caves and pass through supported physics objects,
including flat platforms made from glued pieces. Extended upward Ascend and long-ascent acceleration
are included through the adapted free v1.0.5 Ascend Through Depth Ceiling patch.

## Controls

Aim Ascend, then click the left stick to switch to Descend. Press A at a fully green marker to travel down.
At the exit preview, A drops and B returns. Click the left stick again to aim upward.
Deep routes change the native prompt to Depths and use a dimmer green marker.

A red marker warms toward green near sampled usable spots. Only full green permits a descent at the
current position. Move slowly for the search to catch up; the hint can miss small openings.
Upward markers keep their normal size through 300 metres and grow gradually to 3x size at 1500 metres.

## Versions and downloads

Included game versions: **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1, 1.4.0, 1.4.1, 1.4.2 and 1.4.3**.
Version 1.1.1 is not included. Build IDs select the matching patch automatically.
Emulator patches are named by compact game version (`100.pchtxt`, `110.pchtxt`, through `143.pchtxt`)
for mod-manager version detection. Switch IPS filenames use the full build ID.

- **Standard:** Ascend and Descend remain available in shrines.
- **Shrine restricted (experimental):** vanilla Ascend remains available in shrines; Descend and
  extended Ascend are disabled there. Both extended directions remain available outdoors.
  This variant passes offline tests but has not received an observed play-test.

The standard build was tested on physical Switch with 1.1.2 through 1.4.3, including Great Sky Island
ascents. The 1.0.0 and 1.1.0 reports are from Ryubing and retain the issues listed below.
The earlier 1.1.2 GSI crash was a Ryubing process crash; the same route worked on Switch.

## Installation

Close the game and choose one variant. Each variant has separate emulator and Switch downloads.

- **Emulator:** extract the `ultimate-ascend` folder into the game's mod directory and enable it.
  The experimental variant uses `ultimate-ascend-shrine-restricted` instead.
- **Switch:** copy the archive's `atmosphere` folder to the SD card root. Its IPS files belong in
  `atmosphere/exefs_patches/ultimate-ascend` (or `ultimate-ascend-shrine-restricted`).

Disable or remove older Descend/Ultimate Ascend installations and separate Ascend Through Depth Ceiling
or Infinite Ascend patches. This package already includes the adapted free upward behavior; using both
copies can overwrite shared hooks. If TKMM bundles an older patch, remove that selection and rebuild its output.
When replacing the old subsdk-based Descend, remove that mod's old folder so its runtime cannot remain active.
Keep only one variant installed/enabled. Other mods changing the same hooks or code padding can conflict.
Paid Ascend Through Depth Ceiling variants have not been checked.

This is a static pchtxt/IPS32 mod. It uses no subsdk slot, replaces no game resources and needs no resource table.

## Known issues

- One Depths descent was rejected in the 1.0.0 Ryubing test. Uncertain exits are intentionally refused;
  this particular report remains under investigation.
- Link became invisible during one Descend in the 1.1.0 Ryubing test. Intermittent return-cover loss also
  remains a possible issue. Please report the game version, platform and whether it happened during travel or return.
- Sloped exits can obscure Link's legs in the preview.
- Deep arrivals stay covered for two seconds before revealing the preview. This masks native loading stalls;
  it does not eliminate them. No test establishes safety for every terrain arrangement.

Every profile reserves 8 KiB of additional game RAM within a 64 KiB virtual-address reservation,
using 7072-7248 bytes of the allocation. Code and a 16-byte anchor reuse verified image padding.
Stack use and emulator JIT overhead are additional.

## Source and credits

First-party source is MIT licensed, copyright Clay Mullis. LordCameron's
[Ascend Through Depth Ceiling](https://gamebanana.com/mods/723719) free v1.0.5 assembly is adapted with its
MIT license and notices preserved in `ASCEND-LICENSE.txt` and `ASCEND-NOTICE.txt`.
Infinite Ascend is deprecated in favor of Ascend Through Depth Ceiling.
No game executable, resources, SDK or vendored framework is included.

`src` contains gameplay, geometry and native adapters; `patch` supplies initialization and logging;
`tools` contains the first-party native build/validation helpers; `support/include` contains shared first-party headers.
The native pipeline additionally requires the original legally obtained images, development toolchain and
the shared image/placement tooling from the development checkout. This public tree is not a self-contained
Switch build environment. Pure host tests run independently with CMake 3.25+ and a C++23 compiler:

```sh
cmake -S tests -B build-tests
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

The tests fetch doctest v2.4.11 unless supplied with `-DDOCTEST_DIR=...`.
Offline native tests cover profiles, patch placement, register preservation, effects, travel and compatibility;
mocked services do not establish rendering or hardware behavior.

Source: https://github.com/afiaka87/totk-mods/tree/main/ultimate-ascend
