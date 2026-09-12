# Self Recall v1.0.9

Recall Link through his recent movement and animation history. The regular build
retains 64 seconds for emulators. The Switch build retains 30 seconds within the
smaller memory budget of physical hardware.

## Controls

- Hold **ZL + right-stick click (R3)** for about one second to start Recall. The
  telescope action is suppressed only while the complete chord is held.
- Press **B** to stop. Empty stamina also stops Recall.

## Features

- Reverses movement and full-body animation, including walking, running, jumping,
  climbing, swimming, gliding and steering-stick movement.
- Preserves clothing, weapon, shield, bow, fused attachment and paraglider history.
- Uses the game's Recall ribbon, wrist glow, sounds and muted-world presentation.
- Leaves inventory, health, enemies, quests and world state unchanged.

The current Glide set controls speed, including upgraded pieces:

| Pieces worn | Speed |
|---:|---:|
| 0 | 1.25x |
| 1 | 1.5x |
| 2 | 2x |
| 3 | 4x |

## Requirements and installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`, and an
executable-mod loader compatible with Atmosphere's contents layout.

1. Close the game.
2. Choose the regular archive for an emulator or the Switch archive for physical
   hardware.
3. Extract the archive and copy its `0100F2C0115B6000` folder into the loader's
   contents directory.
4. Confirm that the installed files are `exefs/subsdk8` and `exefs/main.npdm`.

Self Recall cannot be combined with another executable mod that supplies
`subsdk8`. Other subsdk slots can load beside it, but hook and memory compatibility
still depends on the particular mods.

## Known issues

- On Switch, Link skips along his path at 4x speed and during fast movement at 2x.
  The rest of the game stays smooth. Zero or one Glide piece gives the tested
  smooth 1.25x/1.5x rates.
- Driving Recall can stutter mildly at the beginning on Switch.
- Recorded shirtless transitions can make Link's torso disappear.
- The camera can lose Link during fast vertical Recall or enter terrain around
  climbing overhangs.
- Historical pose markers along the ribbon are not included.

The regular and Switch builds passed their v1.0.9 boot sessions with no regression
reported outside the listed known issues. Every possible history and mod
combination has not been tested.

## Changes in v1.0.9

- Reduces the first-party C/C++ source-and-test tree from 73 files to 48 and from
  19,392 lines to 16,913 while retaining the accepted gameplay behavior.
- Consolidates implementation files by owner and removes dead APIs, redundant
  tests and success-only runtime telemetry.
- Shrinks each history sample from 104 bytes to 68 bytes, reducing static memory
  by 120 KiB in the regular build and 28 KiB in the Switch build.
- Retains the v1.0.8 controls, `subsdk8` slot, lossless compression and separate
  64-second regular and 30-second Switch profiles.

## Changes in v1.0.8

- Moves the executable module from `subsdk9` to `subsdk8`.
- Changes activation from ZL + D-pad Down to ZL + right-stick click.
- Unifies the regular and Switch builds behind named storage profiles.
- Applies lossless pose compression, compact appearance records, exact-sized
  effect schemas and safe appearance expiry to both builds.
- Retains 64 seconds and the original storage pools in the regular build; retains
  the accepted 30-second compressed profile in the Switch build.

## Source and dependencies

The public source contains the mod-owned code and host tests under the MIT License.
Build the regular profile with `SELF_RECALL_STORAGE_PROFILE=emulator-compressed`;
build Switch with `SELF_RECALL_STORAGE_PROFILE=switch-compressed`.

The public repository is source only and is not a standalone Switch build tree.
Native builds require Tears of the Kingdom 1.2.1 headers, devkitA64 and exlaunch
configured for module name `self-recall`, fake heap enabled, no debug logging,
`HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000`, `LogBufferSize 512`,
an empty relocation table and subsdk slot 8.

exlaunch, SDK headers, compression libraries and game assets are not included in
the public source. Host tests use doctest 2.4.11 (MIT), Zstandard 1.5.7 (BSD) and
LZ4 1.10.0 (BSD). Native builds use the same compression versions, caller-owned
workspaces and the fast Zstandard strategy. The compiled module includes exlaunch
(GPL-2.0), Zstandard and LZ4; see the accompanying notices and licenses.
