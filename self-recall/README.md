# Self Recall v1.0.5

Self Recall records up to 64 seconds of Link's movement and visible pose, then plays that history backward. It recreates Recall for Link with reversed movement and animation, the native golden ribbon, wrist glow, sound and muted-world presentation.

## Controls

| Input | Effect |
|---|---|
| Hold **ZL + D-pad Down** for about one second | Start Self Recall |
| Press **B** during Recall | Stop at the current point |

## Recall speed

The currently worn Glide set pieces select the speed:

| Pieces worn | Speed |
|---:|---:|
| 0 | 1.25x |
| 1 | 1.5x |
| 2 | 2x |
| 3 | 4x |

Every upgraded version of the Glide Mask, Glide Shirt and Glide Tights counts.

## Features

- Replays position, orientation and full-body animation in reverse.
- Replays clothing, weapons, shields, bows, fused attachments and paraglider presentation.
- Supports walking, running, sprinting, jumping, swimming, climbing, gliding and steering-stick history.
- Uses the native Recall ribbon, wrist effect, activation sound, ambient loop and completion sound.
- Mutes the world while keeping recalled Link in color.
- Drains stamina and stops when stamina is empty.
- Prevents damage caused by accelerated Recall release.
- Leaves health, inventory, enemies, quests and world state unchanged.

## Requirements

- The Legend of Zelda: Tears of the Kingdom 1.2.1, build `9B4E43650501A4D4`.
- An emulator or console environment that supports exlaunch `subsdk9` modules.
- No other executable code mod enabled at the same time.

Developed and tested on Eden. Switch hardware is untested.

## Install

1. Open the game's mod/load directory for title `0100F2C0115B6000`.
2. Extract the archive so it creates `self-recall/exefs/subsdk9` and `self-recall/exefs/main.npdm`.
3. Enable `self-recall` and disable every other add-on containing `exefs/subsdk9`.

## Known issues

- During fast vertical skydiving Recall, a side-on camera can lose Link above or below the frame.
- The camera can briefly enter terrain while recalling through some climbing overhangs.
- Static historical pose markers along the ribbon are not included.

## Changes in v1.0.5

- Replaces the earlier route-only presentation with recorded full-body animation and equipment playback.
- Uses native Recall rendering, wrist effects, audio and world color treatment.
- Adds climbing, swimming, gliding, vehicle control, fused attachment and ledge continuity.
- Adds stamina drain, release damage protection and Glide-set speed scaling.
- Removes the temporary speed banner, all text-system code and all font assets.
- Publishes the mod-owned source under the MIT License.

## Source

The public repository contains the mod-owned source and host tests under the MIT License. exlaunch, SDK headers, game data, private research and local build infrastructure are not included.
