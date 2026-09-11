# Self Recall v1.0.7 - Switch

Recall Link through up to 30 seconds of his movement and animation history.
This is the Switch build. Emulator users should keep the existing v1.0.6 download,
which retains 64 seconds of history.

## Controls

- Hold **ZL + D-pad Down** for about one second to start Recall.
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

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`, and a Switch
setup that loads executable mods through Atmosphere.

1. Close the game.
2. Extract the download. Copy its `0100F2C0115B6000` folder into
   `atmosphere/contents/` on the SD card.
3. The installed files must be `atmosphere/contents/0100F2C0115B6000/exefs/subsdk9`
   and `atmosphere/contents/0100F2C0115B6000/exefs/main.npdm`.
4. Use only one executable mod that supplies `subsdk9`; remove or disable conflicting
   code mods before starting the game.

The compressed profile was play-tested on physical Switch hardware. Other mod
combinations and every possible 30-second history have not been tested. Data and
texture mods can also increase memory demand.

## Known issues

- On Switch, Link skips along his path at 4x speed and during fast movement at 2x.
  The rest of the game stays smooth. Use zero or one Glide piece for the tested
  smooth 1.25x/1.5x rates.
- Driving Recall can stutter mildly at the beginning.
- Recorded shirtless transitions can make Link's torso disappear.
- The camera can lose Link during fast vertical Recall or enter terrain around
  climbing overhangs.
- Historical pose markers along the ribbon are not included.

## Changes in v1.0.7

- Adds a separate Switch build with a 30-second history and lossless compression.
- Keeps native recording cadence, reversed animation, equipment history and speeds.
- Reduces pose, appearance and archived equipment storage for physical hardware.
- Disables memory profiling and raw diagnostic capture in the release build.
- Leaves the existing emulator download unchanged. Speed jitter remains a known issue.

## Source and dependencies

The public source contains the mod-owned code and host tests under the MIT License.
The full emulator profile remains available in source. For this Switch build use
`SELF_RECALL_STORAGE_PROFILE=7`, `SELF_RECALL_MEMORY_PROFILE=0` and
`SELF_RECALL_CORPUS_CAPTURE=0`.

exlaunch, SDK headers, compression libraries and game assets are not included in
the public source. Host tests fetch doctest 2.4.11 (MIT), Zstandard 1.5.7 (BSD) and
LZ4 1.10.0 (BSD). Native builds use the same compression versions, caller-owned
workspaces and the fast Zstandard strategy. The compiled module includes exlaunch
(GPL-2.0), Zstandard and LZ4; see the accompanying notices and licenses.
