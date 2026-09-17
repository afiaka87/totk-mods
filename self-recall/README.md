# Self Recall v1.0.12

Recall Link through his recent movement and animation history. Both builds now
retain 64 seconds. The regular build keeps the whole history in memory for
emulators. The Switch build keeps the newest 10 seconds in memory and stores older
history on the SD card. **SD-card history is an alpha feature** (see below).

## Controls

- Hold **ZL + right-stick click (R3)** for about one second to start Recall. The
  telescope action is suppressed only while the complete chord is held.
- Press **B** to stop. Empty stamina also stops Recall.

## Features

- Reverses movement and full-body animation, including walking, running, jumping,
  climbing, swimming, gliding and steering-stick movement.
- Preserves clothing, weapon, shield, bow, fused attachment and paraglider motion.
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

## Switch SD-card history (alpha)

The Switch build writes older Recall history to the SD card while you play and
reads it back during Recall. It creates a `self-recall-alpha` folder at the root of
the SD card containing:

- `history.bin`, a reused history file of up to 16 MiB;
- one timing log per game session, `log-<number>.txt`, of up to 8 MiB each. These
  logs help diagnose slow cards. They are not removed automatically; delete old
  logs whenever you like while the game is closed.

This feature has been tested on one Switch with one SD card. Slower or nearly full
cards have not been tested. If older history cannot be read back in time, Recall
ends early at that point. If the card fails repeatedly, the build stops using it
and Recall falls back to the newest history held in memory.

## Known issues

- On Switch, Link skips along his path at 4x speed and during fast movement at 2x.
  The rest of the game stays smooth. Zero or one Glide piece gives the tested
  smooth 1.25x/1.5x rates.
- Driving Recall can stutter mildly at the beginning on Switch.
- Recorded shirtless transitions can make Link's torso disappear.
- The camera can lose Link during fast vertical Recall or enter terrain around
  climbing overhangs.
- Historical pose markers along the ribbon are not included.

The regular and Switch builds passed their v1.0.10 boot sessions. The Switch build
also passed with a verified eight-mod RomFS stress overlay. The v1.0.11 weapon-effect
fix passed on physical Switch hardware with that overlay installed and in Eden
running the Switch build. The v1.0.12 Switch SD-card history passed one
physical-Switch session with the same overlay: full 64-second Recall at 1.25x and
4x, and Recall after sleep and resume. The regular build's v1.0.12 changes have not
had a separate boot session. Every possible history, SD card and mod combination
has not been tested.

## Changes in v1.0.12

- Restores the full 64-second history on Switch. The newest 10 seconds stay in
  memory and older history is stored on the SD card (alpha).
- Lowers the Switch build's static memory use slightly compared with v1.0.11.
- The Switch build still uses the currently equipped gear during Recall.
- The regular emulator build keeps its existing behavior.

## Changes in v1.0.11

- Fixes Switch weapon effects, such as the Master Sword glow and a Topaz-fused
  weapon's lightning orb, staying behind or apart from the weapon during Recall.
  Effects of the currently equipped gear now follow its rewinding pose exactly.
- The regular emulator build keeps its existing historical effect behavior.

## Changes in v1.0.10

- Reduces Switch static memory enough to run with a verified eight-mod RomFS
  stress overlay while retaining the 30-second history.
- Keeps current equipment under the game's ownership on Switch, while recording
  its complete bone motion so shields, weapon pieces, fused parts, accessories
  and the paraglider follow the rewind correctly.
- Retains the full 64-second historical-equipment implementation in the regular
  emulator build.
- Fixes a paraglider that could appear early at its future history position and
  removes redundant Switch playback work that caused occasional stutter.

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
build Switch with `SELF_RECALL_STORAGE_PROFILE=switch-compressed` and
`SELF_RECALL_SD_HISTORY=10`.

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
