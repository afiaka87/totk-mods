# Zonai Fieldworks v0.9.8

## Changes in v0.9.8

- **Emulator download only.** The Switch download stays at v0.9.4.
- Works next to **The Second Quest**. Both mods replace the same nine game files whole (four actor
  tables, the save-flag list, the tag table, the English text archive, the interface sound bank and
  the resource-size table), and an emulator loads one copy of each. With both enabled the game
  stopped at a black loading screen. When the copy the game loads is not Fieldworks' own, Fieldworks
  now adds its rows, flags, tags, names, sounds and memory budgets to it as it loads. No patch and
  no load-order change is needed, and the other mod's files are not touched.
- Self Recall keeps its four large buffers at the end of the game's main memory pool, not in the
  module. The module is about 10 MiB, down from about 120 MiB. At 120 MiB a code mod in a later
  slot sat too far from the game for its hooks to work.
- When another mod's `main.npdm` is the one in effect and reserves more than Fieldworks' 8 MiB,
  Fieldworks takes the difference from memory the game leaves free, so the game's own pool keeps
  its usual size.
- Played on Citron at 1.2.1 and 1.4.3 with The Second Quest enabled, and at 1.2.1 without it.

## Changes in v0.9.4

- New **Switch** download. It holds the same eight members; Self Recall uses the storage setup of
  its own Switch release (the newest 10 seconds of history in memory, the rest on the SD card).
- Fixed: on a Switch at 1.4.3, eating or selecting an item from the pouch closed the game. BotW
  Abilities' Champion items read the 1.4.x item-select call with the wrong arguments.
- Fixed: Fleet Sync now ends the link when the driven vehicle unloads or is destroyed. Before, a
  long Self Recall away from linked vehicles could close the game on a Switch.
- Fixed (Switch download): Self Recall no longer stops right after it starts when a fused material
  has a part the game is not drawing at that moment (seen with a Silver Lynel Saber Horn).
- New member versions: BotW Abilities v0.4.4, Fleet Sync v0.3.2 and Self Recall v1.1.4.
- Played on a Switch at 1.2.1 and 1.4.3 with this release's test builds, and on Ryujinx 1.3.3 at
  1.2.1. See Known issues for what the final Switch build has not been played with.

## Changes in v0.9.3

- Runs on Tears of the Kingdom **1.4.3** as well as 1.2.1, wheel page included. Every member
  supports both versions now.
- New member versions: Zonai Hookshot v0.12.5, Fleet Sync v0.3.1, BotW Abilities v0.4.3 and
  Bivouac v0.27.0. Their own READMEs list what changed.
- A few extras are still off on 1.4.3: the Cryonis aim labels, the faster Champion recharge in
  Hyrule Castle, and Bivouac's backup trigger from the pouch. Everything else works the same on
  both versions.

Eight of my Tears of the Kingdom mods in one package, for emulators and the Switch. Each of them
normally needs its own executable slot, so running all eight leaves almost no room for other code
mods. Fieldworks puts them in a single slot (`subsdk6`) and adds a page to the ability wheel where
you switch each one on or off while you play.

The standalone releases are still the right choice if you only want one or two of them.

## What is in it

| Member | Version | What it does | Its own README |
|---|---|---|---|
| Phantom Foothold | v1.2.2 | Climb every surface with collision, ceilings included. | `../phantom-foothold/` |
| Zonai Hookshot (with Arrowbound) | v0.12.5 | Chain to any climbable wall, zip along it and land in the climb. | `../zonai-hookshot/` |
| Fleet Sync | v0.3.2 | Fly up to four identical vehicles from one Steering Stick. | `../fleet-sync/` |
| Material Columns | v0.1.6 | A materials quick-menu mode that groups materials into families. | `../material-columns/` |
| BotW Abilities | v0.4.4 | Stasis, Remote Bombs, Cryonis, Wolf Link and the four Champions' Blessings. | `../botw-abilities/` |
| Bivouac | v0.27.0 | Eat an ingredient while climbing to build a camp you can travel back to. | `../bivouac/` |
| Self Recall | v1.1.4 | Recall Link through his own movement history. | `../self-recall/` |
| Zonai Survey | v0.4.2 | Scan the landscape for collectibles with names and icons. | `../zonai-survey/` |

Every member keeps the controls and behaviour of its standalone release. Read its README for how
to use it.

Ultimate Ascend and Drive-by are patch-file mods. They are not part of Fieldworks and can be
installed next to it.

## Requirements

- Tears of the Kingdom **1.2.1** or **1.4.3**.
- Emulator download (v0.9.8): played on Citron at both versions (see Known issues).
- Switch download (v0.9.4): for a Switch running Atmosphere, and for Ryujinx/Ryubing. Played on a Switch at
  1.2.1 and 1.4.3, and on Ryujinx 1.3.3 at 1.2.1.
- Other game versions are untested. Material Columns, Self Recall and Zonai Survey support other
  versions on their own, but the Fieldworks wheel page is only available on 1.2.1 and 1.4.3.

## Install

**Emulator (Citron and similar):**

1. Extract `Zonai Fieldworks/` into the emulator's mod folder for the game
   (`load/0100F2C0115B6000/`).
2. Disable the standalone copy of every member. Fieldworks contains them already, and two copies
   of one mod do not work together.
3. Disable any other mod that uses `subsdk6`.
4. The Second Quest can stay enabled. Nothing else needs to be installed for the two to work
   together.

**Switch:** copy the archive's `0100F2C0115B6000/` folder into `atmosphere/contents/` on the SD
card. Remove the standalone members and any other `subsdk6` mod from that title folder first. Keep
this package's `exefs/main.npdm`: it reserves 8 MiB for mod code, and a larger one from another mod
leaves the game short of memory.

**Ryujinx/Ryubing:** put the contents of the Switch archive's `0100F2C0115B6000/` folder (`exefs/`
and `romfs/`) into a new folder under `mods/contents/0100f2c0115b6000/`, for example
`Zonai Fieldworks/`. Turn off the standalone members and any other `subsdk6` mod in Manage Mods.

The package carries its own resource-size tables (`ResourceSizeTable.Product.121.rsizetable.zs` for
1.2.1 and `ResourceSizeTable.Product.143.Nin_NX_NVN.rsizetable.zs` for 1.4.3). If another mod also
ships the table for your game version, only one of the two is loaded. With the emulator download,
Fieldworks adds its own budgets when the other mod's table is the one loaded. When Fieldworks' table
is the one loaded, the other mod's budgets are lost; merge the tables with a mod manager or expect
missing items from that mod. The same holds for the other eight files listed under v0.9.8.

## Switching members

- Every member starts **off**.
- Hold L to open the ability wheel and tap R to change page until you reach the Fieldworks page.
  Release on a member's icon to switch it. The caption shows the saved choice.
- Seven of the eight switch at once. A member that is switched off ends what it is doing and
  removes what it put in the world: a Stasis target is let go, Remote Bombs and Cryonis pillars are
  removed, Wolf Link leaves, and Bivouac's camps and their map markers disappear until Bivouac is
  switched on again.
- A member that is in the middle of something it cannot cut short (riding a linked Steering Stick,
  warping to a camp) switches off when that ends. The caption says so.
- Phantom Foothold changes terrain as the terrain loads, so its switch only applies to areas
  loaded afterwards: fast travel, enter a shrine or travel far enough. Its caption ends with
  "(newly loaded areas)".
- The choices are saved in `ZonaiFieldworks/settings.txt` on the SD card (the emulated SD card on
  an emulator), one `member-id=1` or `member-id=0` line per member. All save files share them.

## Known issues

- After switching Zonai Hookshot off, the paraglider was invisible once during testing. It did not
  happen again and the cause is not known.
- Phantom Foothold cannot be switched for terrain that is already loaded (see above).
- On 1.4.3 the faster Champion recharge in Hyrule Castle and Bivouac's backup trigger from the
  pouch are off.
- Only 1.2.1 and 1.4.3 have been played.
- With The Second Quest, fast travel and long sessions have not been tried.
- The Second Quest is the only other mod the shared-file change was tried with. If your emulator or
  mod manager loads Fieldworks' copy of a shared file and not the other mod's, the other mod still
  loses its changes to that file. Citron loads the copy from the folder whose name sorts first.
- Without The Second Quest, v0.9.8 was played at 1.2.1 only, with the test setup's resource-size
  table and not the one in the download.
- The Switch download is still v0.9.4 and does not have the v0.9.8 changes.
- The final Switch build (v0.9.4) has not been played on a Switch. It differs from the last build played
  there only by the Self Recall fused-material fix, which was tested on Ryujinx. Fleet Sync and
  Cryonis have not been checked on a Switch at 1.2.1.
- Ryujinx closed once during a shield surf with Daruk's Protection up. It did not happen again and
  the cause is not known.

## Building from source

This folder contains only Fieldworks' own code: the wheel page, the switches, the settings file,
the shared-file merge and the scripts that compose the game files. It is **not** a complete project and comes with no
guarantee that it builds or works as-is.

- Fieldworks compiles every member from that member's source. The member folders in this
  repository are the standalone releases. Zonai Survey's folder (v0.4.2) predates the switch
  callbacks that Fieldworks calls (the entry points added to members for live switching), so this
  folder does not build against it as published.
- The build option `FIELDWORKS_TARGET` is `emulator` (default) or `switch`. `switch` builds Self
  Recall with its `switch-compressed` storage profile and 10 seconds of SD-card history; the other
  members are the same in both downloads. `emulator` also sets Self Recall's
  `SELF_RECALL_ARENA_TAIL` option (its buffers at the end of the game's memory pool). That code is
  newer than the published Self Recall folder (v1.1.4).
- `src/fieldworks/SharedFiles.cpp` and `src/pure/merge/` add Fieldworks' content to a shared game
  file as the game loads it. `scripts/merge_deltas.py` writes the additions (the archive's
  `romfs/Fieldworks/Merge/`) from the composed files and a clean game dump. `scripts/merge_check.py`
  checks the merge on real files with `tests/merge_tool.cpp`.
- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "zonai-fieldworks"`, keep `EXL_USE_FAKEHEAP`, and use `HeapSize 0x80000`,
  `JitSize 0x20000`, `InlinePoolSize 0x4000`, `LogBufferSize 1024`.
- Program ID `0100F2C0115B6000`, module `subsdk6`.
- `src/fieldworks/generated/ClimbPatches.hpp` is Phantom Foothold's climbing patch as a table,
  written by `scripts/climb_patches.py`.
- `scripts/compose_romfs.py` builds the package's game files from the members' release packages
  and merges the four files that both Bivouac and BotW Abilities replace. It depends on private
  build tooling and on a clean game dump, and is published for reference.
- The `romfs/` files are edited game data and are not part of this source folder. They are only
  in the release archive.

## Tests

`tests/run_host_tests.ps1` builds and runs the host tests for the settings file, the wheel
captions and the shared-file merge. It needs CMake, Ninja and a C++23 compiler (`g++` by default, or set `CXX`) on `PATH`.
Each member has its own host tests in its own folder.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- exlaunch by shadowninja108 and contributors.
- devkitPro.
- See each member's README for its own credits.

## License

MIT - see `LICENSE` at the repository root. `NOTICE.txt` describes the exlaunch dependency and
the game files in the release archive.
