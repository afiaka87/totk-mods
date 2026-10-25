# Phantom Foothold v1.2.2

## Changes in v1.2.2

Uses the shared 8 MiB loader reservation so installing this package does not overwrite the combined-mod memory fix.

The six-mod combination of Infinite Ascend, Fleet Sync, Self Recall (Switch), Zonai Hookshot with Arrowbound, BotW Abilities and Zonai Survey passed a physical Switch 1.2.1 session. Other game versions and combinations have not had a new test of the 8 MiB setting.

Climb everything. The game stamps thousands of surfaces "no-climb" inside its collision
data (Temple of Time masonry, shrine interiors, Zora's Domain pillars, town buildings,
bridges, Depths scaffolding, the great doors). Phantom Foothold patches the two routines
that load collision files and clears that stamp in memory as each file loads, so every
surface with collision accepts a climb, from one 60 KB code module and no replaced game
files. Climb Ceilings is included: a small cheat file lets Link keep climbing straight
across overhangs and ceilings.

- Invisible barriers (map edges, quest walls) are deliberately left alone.
- Very small or moving objects may still refuse a grab; size and motion checks are untouched.
- Dragons are climbable now.

## Requirements

- The Legend of Zelda: Tears of the Kingdom **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1 or 1.4.0–1.4.3**.
  The code module checks the game's collision loaders before patching; on any other version
  it installs nothing and climbing stays vanilla. Each version has its own cheat file, named
  by that version's build id, and the game loads only the matching one:

| Game version | Cheat file |
|---|---|
| 1.0.0 | `082CE09B06E33A12.txt` |
| 1.1.0 | `D5AD6AC71EF53E3E.txt` |
| 1.1.2 | `9A10ED9435C06733.txt` |
| 1.2.0 | `6F32C68DD3BC7D77.txt` |
| 1.2.1 | `9B4E43650501A4D4.txt` |
| 1.4.0 | `6265F94D606242CE.txt` |
| 1.4.1 | `965EAB9CEB8EB867.txt` |
| 1.4.2 | `5CB42B1CF25469FB.txt` |
| 1.4.3 | `277178B7DBA1B6D4.txt` |

- Contains no game assets. No other mods, DLC, or save changes required.

## Install (emulator)

1. Right-click the game in your emulator and open the mod/load directory
   (`load/0100F2C0115B6000/`).
2. Extract the emulator zip there, producing `phantom-foothold/exefs/subsdk2`,
   `phantom-foothold/exefs/main.npdm` and the nine files in `phantom-foothold/cheats/`.
3. Enable **phantom-foothold** in the game's properties / add-ons list, and make sure cheats
   are enabled for the game (the ceiling behavior lives in the cheat file).

## Install (Switch)

Extract the Switch zip under `atmosphere/contents/`, merging its `0100F2C0115B6000/` folder:
`exefs/subsdk2` and `exefs/main.npdm` go to `atmosphere/contents/0100F2C0115B6000/exefs/`
and the nine cheat files to `atmosphere/contents/0100F2C0115B6000/cheats/`. Do not replace
other mods' files.

## Compatibility

- Do not enable another mod that supplies `subsdk2`. Other executable slots are separate; this does not guarantee hook or memory compatibility.
- Walls climb but you slide off at overhangs: the cheat file is missing or cheats are disabled.

## Changes in v1.2.0

- Supports Tears of the Kingdom 1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1 and 1.4.0–1.4.3 in each
  download, with one cheat file per version. v1.1.1 required 1.2.1.
- On 1.4.x, the game no longer closes at launch with the mod installed: the module header
  now names the SDK version that the 1.4.x game's loader reads.
- Tested on hardware: the version port passed combined sessions with Self Recall and Zonai
  Survey on a physical Switch running each of the nine versions. The final v1.2.0 build
  passed on a physical Switch running 1.4.3 and in Eden on 1.2.1.

`exefs/main.npdm` reserves 16 MiB for mods on every supported version (SHA-256
`D593A80E5E53F860...`, 1,580 bytes).

## Changes in v1.1.0

The former ceiling foot-planting data file (`romfs/Pack/Actor/Player.pack.zs`) is gone:
testing showed ceiling climbing is identical without it, so the add-on no longer touches any
game data file. The cheat text now ships inside the package. No other behavior change from 1.0.

## Slot-only update in v1.1.1

Moves the executable from subsdk9 to **subsdk2**. Gameplay is unchanged.
Remove this mod's old `exefs/subsdk9` when upgrading. On Switch, preserve any
slot 9 file belonging to another mod. This change separates executable filenames;
it does not claim new compatibility testing.

Downloads: `phantom-foothold-v1.2.2-emulator-subsdk2.zip` and `phantom-foothold-v1.2.2-switch-subsdk2.zip`. The Switch archive wraps the same payload in `0100F2C0115B6000/` for extraction under `atmosphere/contents/`.


## Changes in v1.2.1

Code cleanup for collision-buffer access and per-loader statistics. All nine game profiles,
cheats and climbing behavior stay the same. The compiled instructions and allocated data
match the accepted v1.2.0 build exactly; only the build identifier differs. The module still
maps 212 KiB and the process resource setting remains 16 MiB. Existing gameplay observations
transfer through this binary comparison; no separate gameplay session was run for this cleanup.
