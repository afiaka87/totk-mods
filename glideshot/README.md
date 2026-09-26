# Glideshot v0.10.4

A hookshot for Tears of the Kingdom. Aim at a climbable wall from the ground, from a climb or from
the air, fire a visible chain, zip along it at 60 m/s, and land in the game's own climbing state.
The paraglider opens by itself for the last stretch so the game decides the grab, not the mod.

Glideshot also includes Arrowbound: activate the Arrowbound Emblem in Key Items, shoot an arrow,
and follow its normal flight with the paraglider open. Both features have been tested together
on Eden with Tears of the Kingdom 1.0.0 through 1.4.3. Earlier versions were also tested on
physical Nintendo Switch with 1.2.1.

## Controls

- Hold **ZL + L** (left shoulder button) briefly to raise the aim. A green
  diamond marks a wall the chain can take; a red diamond marks a surface it refuses.
- Press **A** to fire. The chain draws to the anchor at once and Link follows one tick later.
- Press **B** at any point to let go into the open paraglider. Losing the world (a shrine door, a warp, a load) also ends
  the trip.

While the aim is up, its activation buttons are hidden from the game. Drawing the bow releases
manual traversal; ZL + L releases an Arrowbound trip before manual targeting begins.

## Arrowbound

- Activate or deactivate the Arrowbound Emblem from Key Items. Both states use the arrow icon.
- Fire a bow from the ground or in midair. Arrow speed, gravity, arc and range stay vanilla.
- Airborne aiming retains slow motion; releasing the arrow ends it for the trip.
- The paraglider stays open during following. B lets go into ordinary paragliding; drawing the
  bow again retires the old arrow before a new trip can begin.
- Climbable-wall impacts turn Link toward the wall and hand off to normal climbing. Other
  impacts finish in the glider.

Activation is stored in `sd:/arrowbound/settings.bin`, or the emulator's emulated SD card.
It starts off when no valid setting exists and is shared with standalone Arrowbound across all
saves/profiles on that SD card. Loading an older save does not rewind it. If writing fails, the
choice still works for that session. The code-only mod ships no replacement game assets.

The emblem is available while the mod runs. New saves exclude an unearned All's Well carrier;
an emblem earned through the vanilla well quest stays in the save. Older saves are not rewritten
until the game saves them again. This does not reset quest progress.

## What counts as a target

- Static world geometry only. Moving platforms, Zonai devices and anything with a rigid body the
  chain could not follow are refused.
- Walls, including shallow underhangs. Floors and ceilings are refused.
- Between 2 m and 300 m away. The game's own no-climb surfaces are refused.

The chain is a black spine with two green strands drawn per pixel by a small shader pass, occluded
by the scene depth buffer like any other object. The reticle keeps a constant apparent size.

## Sounds

The mod plays the game's own interface sounds through the game's sound system. The aim marker,
the accept chime, the refusal buzz and the shot each have a fixed cue. Travel start and arrival
prefer Ultrahand's activation and cancel sounds when the game has that sound user loaded, and fall
back to the interface cues otherwise. No audio files are shipped.

## Requirements and installation

Requires Tears of the Kingdom **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1, 1.4.0, 1.4.1, 1.4.2 or 1.4.3**
and an executable-mod loader compatible with Atmosphere's contents layout. One package serves
every listed version: at startup the mod identifies the running game build and uses that build's
checked address table. On an unrecognized build it logs the reason and installs nothing.

1. Close the game.
2. Choose the emulator archive for Eden or another emulator, or the Switch archive for physical
   hardware. Both archives carry the same module; they are packaged separately so each download
   page can describe its own testing state.
3. On Switch, copy `0100F2C0115B6000` into `atmosphere/contents/` on the SD card. On Eden, use
   Open Mod Data Location and place the add-on folder containing `exefs` there, then enable it.
4. Confirm that the installed files are `exefs/subsdk5` and `exefs/main.npdm`.

Do not enable standalone Arrowbound beside this combined build. Remove older Glideshot/Arrowbound
ROMFS overrides when upgrading; this build needs only the two `exefs` files. Back up an existing
installation before replacing it. Do not overwrite a shared ROMFS tree belonging to other mods.

Glideshot cannot be combined with another executable mod that supplies `subsdk5`. Other subsdk
slots can load beside it, but hook and memory compatibility still depends on the particular mods.

The drawing, wall-grip and embedded Arrowbound fixes passed a combined-mod smoke
test on physical Switch with Survey and Self Recall installed. Earlier builds
passed Eden and Citron checks. This is not exhaustive compatibility testing.

## Known issues

- At very high arrow speeds, Link's legs can tuck backward while flight stays visible
  and smooth. Wing-fused arrows and every modded bow have not been exhaustively tested.
- The chain's coil can look blurred past the halfway point on long shots.
- A very short shot can reach its one-metre standoff before the paraglider opens; the mod holds
  position until it does.
- The Ultrahand travel and arrival sounds depend on the game having its expression sound user
  loaded at that moment. When it is not, the interface fallbacks play instead.

## Current update (v0.10.4)

- Supports game versions 1.0.0 through 1.4.3 in one package. All nine versions were observed
  working on Eden; this internal cleanup build was rechecked on 1.2.1 and 1.4.3.
- B cancel always leaves Link in the open paraglider; B is hidden from the game until released.
- Travel stops one metre from the wall instead of half a metre, so Link no longer ends up inside
  the wall.
- Fixes deactivating the Arrowbound Emblem and arrow following on 1.4.x.
- Glideshot now builds Arrowbound's shared engine, math and support code instead of carrying
  duplicate copies.
- This version has not been retested on physical Switch.

## Changes in v0.9.4

- Uses ZL + L, avoiding Self Recall's ZL + R3 activation.
- Preserves both renderers when Glideshot and Survey share the drawing callback.
- Restores destination-wall grip and Arrowbound's flight clock alongside Self Recall.

## Earlier Arrowbound integration

- Fixes fast-arrow disappearance and choppy following without changing arrow physics.
- Restores ZL + R3 manual activation and imports Arrowbound from its independently buildable source.
- Adds native-arc arrow following, steady glider presentation, airborne slow-motion cleanup,
  cancellation, re-aiming and climbable-wall handoff.
- Uses a code-only emblem and a persistent SD setting instead of replacement game-data files.
- Gives manual travel and Arrowbound exclusive movement ownership during control transfers.

## Changes in v0.8.1 (historical)

- Activation is now ZL + L. This avoids UltraCam's ZL + ZR + L3 menu chord and Self Recall's
  ZL + R3 activation chord.
- Either button order works. If L opens the vanilla ability menu first, completing the chord closes
  that menu and activates Glideshot.
- The combined Eden test passed both activation orders, Self Recall activation and UltraCam's
  default-binding isolation.

## Changes in v0.8.0

- The chain now starts from the position the game actually draws Link at during the zip, which
  closes the gap that used to open between the coil and Link's torso.
- Travel start and arrival use Ultrahand's activation and cancel sounds, with interface fallbacks.
  The looping marker cue from earlier builds is gone.
- The debug text overlay, toasts and their font files are removed. The module no longer ships any
  romfs files.
- The mod-owned source is relicensed under MIT and no longer depends on any GPL-licensed helper
  code beyond exlaunch itself.
- Comments and tests were trimmed; the host suite covers the state machine, target validation,
  chain geometry, projection, capture math and cues.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. Apply the `InlineFloatCtx` fix from exlaunch issue #28 / PR #31;
  the jump-boost inline hook reads a float register through it.
- Layout: place `src/program/main.cpp`, `src/program/modules/`, `src/engine/` and `src/pure/` in
  an exlaunch project. Include paths: `src/pure`, `../arrowbound/src/pure`, `src/engine`,
  `../arrowbound/src/engine`, `../arrowbound/src/support` and `../runtime-support/include`.
- Keep the sibling `../arrowbound/` folder. Compile its `src/engine` and
  `src/program/modules/arrowbound` sources, but not its standalone `src/program/main.cpp`.
  `../arrowbound/cmake/ImportArrowbound.cmake` shows the object-target integration with isolated
  private include paths. Expose its `src/include` to this host and use this host's exlaunch
  configuration so there is only one entry point and one hook pool. The native source uses C++26.
- Also compile Arrowbound's `src/engine/{ActionContext,AimRaycaster,HookshotAudio,HookshotInput,
  HookshotWorld}.cpp` a second time for Glideshot with `HOOKSHOT_ENGINE_NS=zonai_hookshot` and
  `HOOKSHOT_ENGINE_TAG="[zonai-hookshot]"`, so each feature keeps its own engine state.
- Shaders: `shaders/chain.frag`, `shaders/chain.vert` and `shaders/chain_math.inl` are compiled
  to NVN binaries by `tools/compile_shaders.py` (see `cmake/ChainShader.cmake`), which expects an
  external NVN GLSL compiler pinned by hash. The generated `ChainShaders.hpp` is not checked in.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "zonai-hookshot"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000`, `LogBufferSize 512`. Leave the
  reloc table in `offsets.hpp` empty.
- Compile definition: `TOTK_VERSION=121` (selects the 1.2.1 SDK headers; game addresses come
  from `../arrowbound/src/include/arrowbound/GameProfiles.hpp` at runtime). Program ID
  `0100F2C0115B6000`, module `subsdk5`. Set the NPDM system resource size to `0x1800000`
  (24 MiB), which every supported version boots with.
- This release retains `ARROWBOUND_FLIGHT_DIAGNOSTICS=1` to match the tested build.

## Tests

- `tests/run_host_tests.ps1` runs both suites: 85 manual/composition cases and 74 Arrowbound
  cases. Keep the sibling Arrowbound folder. Tests need CMake, Ninja, a C++23 compiler and network
  access to fetch doctest 2.4.11 when it is not vendored. No game files are needed for the default run.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- exlaunch by shadowninja108 and contributors.
- doctest (MIT); devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE` describes the exlaunch dependency.

## Downloads

Use `glideshot-v0.10.4-emulator-subsdk5.zip` or `glideshot-v0.10.4-switch-subsdk5.zip`.
The Switch archive wraps the same payload in `0100F2C0115B6000/` for extraction
under `atmosphere/contents/`. Check the supplied SHA256SUMS.txt before installation.
Remove only this mod's old subsdk9 when upgrading from a pre-slot-5 installation;
preserve slot 9 if it belongs to Survey or another mod.

The sibling `runtime-support/include` supplies the MIT hook-coexistence helpers.
Its `tests` directory checks callback chaining alongside Glideshot and Arrowbound.

## Executable slot

Uses `subsdk5`. When upgrading, remove this mod's old `exefs/subsdk9`
from its own add-on folder before installing the new package. On Switch, remove
only the old executable belonging to this mod; preserve another mod's slot 9 file.
Older downloads still use slot 9; use the slot-migrated version.
