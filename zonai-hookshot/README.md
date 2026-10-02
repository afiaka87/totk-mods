# Zonai Hookshot (formerly Glideshot) v0.12.0

Aim at a climbable wall and pull Link toward it with a glowing Zonai tether from his right hand.
The double helix has layered filaments, travelling light and small motes inspired by Ascend.
Toward the far end the strands keep their proportions and a teal core, so long tethers stay crisp.
Link faces along the tether, turns gradually toward the destination, and enters normal climbing
for the final grab. Native animation gives his legs and torso movement during travel.

The mod also includes Arrowbound: activate the Arrowbound Emblem and ride the flight of an arrow
with the paraglider open. No replacement game models, textures or sound files are included.

## Controls

- Hold **ZL + L** (left shoulder button) briefly to aim.
- A green double-ring marker indicates a valid target; red indicates a refused surface.
- Press **A** to fire. Press **B** to cancel into ordinary paragliding.
- While aiming from an existing glide, Link keeps the paraglider until firing. Use directional
  input to turn while gliding. Climbing aim preserves the normal climbing pose.
- Drawing the bow releases manual traversal. ZL + L releases an Arrowbound trip before aiming.

During the pull, the paraglider is hidden and its sounds are muted. It returns for the final
approach or cancellation. The tether stays connected to Link's hand. Custom gyroscopic aiming
is disabled; aim with the normal camera controls.

## Targets

Climbable static walls from 2 to 300 metres away, including shallow underhangs. Floors, ceilings,
moving platforms, Zonai devices and the game's no-climb surfaces are refused. The reticle scales
with distance, with smaller nearby markers and larger distant ones. The tether uses scene depth
to remain behind terrain.

## Supported versions and testing

One module supports Tears of the Kingdom **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1, 1.4.0, 1.4.1,
1.4.2 and 1.4.3**. It identifies the running game at startup and installs nothing on unknown builds.

The v0.11.0 gameplay and presentation passed observed tests on all nine versions in Eden and
on physical Switch **1.2.1 and 1.4.3**. Physical Switch 1.0.0 was deferred. The v0.12.0 changes
(far chain and final-approach arms) were tested in Citron on 1.2.1 only. Other hardware/version
combinations have not been tested with this payload.
Earlier combined builds passed tests beside Survey and Self Recall; compatibility with every
other executable mod is not guaranteed.

## Installation

Close the game and back up the existing mod installation before replacing files.

- **Emulator:** extract `zonai-hookshot-v0.12.0-emulator-subsdk5.zip`. Put its `zonai-hookshot`
  folder in the game's mod directory, then enable it in the emulator's Add-Ons menu.
- **Switch:** extract `zonai-hookshot-v0.12.0-switch-subsdk5.zip`. Merge its
  `0100F2C0115B6000` folder under `atmosphere/contents/` on the SD card.

Both packages contain the same `exefs/subsdk5` and `exefs/main.npdm`. Check `SHA256SUMS.txt`.
The NPDM requests 24 MiB of system resource memory.

Do not enable standalone Arrowbound beside this combined build, or another mod using `subsdk5`.
When upgrading from Glideshot, remove the old add-on folder after backing it up so both versions
cannot load together. For a pre-slot-5 installation, remove only this mod's old `subsdk9`;
preserve slot 9 if it belongs to Survey or another mod. Remove obsolete Glideshot/Arrowbound
ROMFS overrides belonging to this mod. The current package needs no ROMFS files.

## Arrowbound

Activate or deactivate the Arrowbound Emblem in Key Items. Both states use the arrow icon.
Fire a bow from the ground or in midair to follow the arrow. Speed, gravity, arc and range remain
vanilla. Airborne aiming retains slow motion; releasing the arrow ends it for the trip.
Press B to return to ordinary paragliding, or draw the bow to retire the old trip.
Climbable-wall impacts enter climbing; other impacts finish in the glider.

Activation is stored in `sd:/arrowbound/settings.bin`, or the emulator's emulated SD card.
It defaults off and is shared with standalone Arrowbound across saves. Loading an older save
does not rewind it. A storage failure keeps the choice for the current session.
The emblem uses the All's Well carrier while the mod runs; naturally earned All's Well and
quest progress are preserved. No custom GameData schema is shipped.

## Known limitations

- Arrowbound retains known high-speed movement issues, including leg tuck. The new manual
  Hookshot body correction does not resolve those separate issues.
- Wing-fused arrows and every modded bow have not been exhaustively tested.
- Very short pulls can wait for native glide before the final wall approach.
- Travel and arrival sounds use interface fallbacks when the native sound user is unavailable.

## Changes in v0.12.0

- The far half of the tether stays crisp: strand width follows the on-screen size of each turn, and
  the glow fades out toward the tip, leaving teal cores and edges.
- Link's arms keep their pose through the final wall approach instead of raising as if holding the
  hidden glider.
- Arrowbound fast-arrow drawing changes from ongoing work; its high-speed issues remain (see below).

## Changes in v0.11.0

- Public name changed to **Zonai Hookshot (formerly Glideshot)**.
- New Ascend-inspired helix, brighter filaments and distance-scaled double-ring reticle.
- Tether originates at Link's hand; the former dragon model is removed.
- Arm aim, natural resting left arm, glide/climb-specific aiming and gradual turning during pulls.
- Native glider leg/torso motion with body-position correction for smooth high-speed travel.
- Hidden, silent paraglider during pulls, with normal cancellation and wall-approach restoration.
- Presentation behavior ported across all nine supported versions.

## Source

This folder contains the mod's own source, shaders and host tests. It is not a complete Switch
build environment. Supply devkitPro/devkitA64, an exlaunch project and the required game-facing
headers. Framework and SDK sources are not included.

- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch), GPL-2.0, base `f698816d`.
  The float-aware inline hook context needs the fix discussed in issue #28 / PR #31.
- Native source uses C++26. Keep sibling `arrowbound` and `runtime-support` source folders.
- Compile `src/program/main.cpp`, `src/program/modules`, `src/engine` and Arrowbound's feature
  sources, excluding its standalone entry point. `arrowbound/cmake/ImportArrowbound.cmake`
  describes the imported object target and its private include paths.
- Compile Arrowbound's `ActionContext`, `AimRaycaster`, `HookshotAudio`, `HookshotInput` and
  `HookshotWorld` engine sources again with `HOOKSHOT_ENGINE_NS=zonai_hookshot` and
  `HOOKSHOT_ENGINE_TAG="[zonai-hookshot]"` to keep separate feature state.
- Include `src/pure`, `src/engine`, Arrowbound's `src/pure`, `src/engine`, `src/support` and
  `src/include`, plus `runtime-support/include`.
- Compile shaders with `tools/compile_shaders.py` and the hash-pinned external compiler pair
  described in `cmake/ChainShader.cmake`. Generated binary headers are not distributed as source.
- Use exlaunch's consumer templates: `EXL_MODULE_NAME "zonai-hookshot"`, `EXL_USE_FAKEHEAP`,
  no `EXL_DEBUG`, `HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000`,
  `LogBufferSize 512`, empty relocation table, `TOTK_VERSION=121`, module `subsdk5`,
  program ID `0100F2C0115B6000`, NPDM resource size `0x1800000`.
- Retain `ARROWBOUND_FLIGHT_DIAGNOSTICS=1` to match the tested payload. Some internal logs
  retain the former Glideshot name and version; this release preserves the tested runtime bytes.

Run `tests/run_host_tests.ps1` for the combined host suites. Tests need CMake, Ninja, a C++23
compiler and doctest 2.4.11, fetched when absent. Keep the sibling Arrowbound folder.
The default tests require no game files.

## Credits and license

Original source: MIT, copyright Clay Mullis. See the repository's root LICENSE.
The compiled module includes exlaunch (GPL-2.0). Host tests use doctest (MIT); native builds use
devkitPro/devkitA64. See NOTICE.txt for source links. No Nintendo game assets are distributed.
