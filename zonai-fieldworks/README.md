# Zonai Fieldworks v0.8.1

## Changes in v0.8.1

The eight icons on the Fieldworks wheel page are smaller: 75% of their v0.8.0 size, and 66% for
Zonai Hookshot. Nothing else changed. The module is the same file as in v0.8.0, so its log line
still says v0.8.0.

Eight of my Tears of the Kingdom mods in one package, for emulators. Each of them normally needs its
own executable slot, so running all eight leaves almost no room for other code mods. Fieldworks
puts them in a single slot (`subsdk6`) and adds a page to the ability wheel where you switch each
one on or off while you play.

The standalone releases are unchanged and are still the right choice if you only want one or two
of them, or if you play on a Switch.

## What is in it

| Member | Version | What it does | Its own README |
|---|---|---|---|
| Phantom Foothold | v1.2.2 | Climb every surface with collision, ceilings included. | `../phantom-foothold/` |
| Zonai Hookshot (with Arrowbound) | v0.12.4 | Chain to any climbable wall, zip along it and land in the climb. | `../zonai-hookshot/` |
| Fleet Sync | v0.2.4 | Fly up to four identical vehicles from one Steering Stick. | `../fleet-sync/` |
| Material Columns | v0.1.6 | A materials quick-menu mode that groups materials into families. | `../material-columns/` |
| BotW Abilities | v0.3.0 | Stasis, Remote Bombs, Cryonis, Wolf Link and the four Champions' Blessings. | `../botw-abilities/` |
| Bivouac | v0.26.3 | Eat an ingredient while climbing to build a camp you can travel back to. | `../bivouac/` |
| Self Recall | v1.1.3 | Recall Link through his own movement history. | `../self-recall/` |
| Zonai Survey | v0.4.2 | Scan the landscape for collectibles with names and icons. | `../zonai-survey/` |

Every member keeps the controls and behaviour of its standalone release. Read its README for how
to use it.

Ultimate Ascend and Drive-by are patch-file mods. They are not part of Fieldworks and can be
installed next to it.

## Requirements

- Tears of the Kingdom **1.2.1** on an emulator. Tested on Citron.
- Other game versions are untested. Material Columns, Self Recall and Zonai Survey support other
  versions on their own, but the Fieldworks wheel page is only available on 1.2.1.
- There is no Switch build yet.

## Install

1. Extract `Zonai Fieldworks/` into the emulator's mod folder for the game
   (`load/0100F2C0115B6000/`).
2. Disable the standalone copy of every member. Fieldworks contains them already, and two copies
   of one mod do not work together.
3. Disable any other mod that uses `subsdk6`.

The package carries its own resource-size table (`ResourceSizeTable.Product.121.rsizetable.zs`).
If another mod also ships that file, only one of the two tables is loaded. Merge them with a mod
manager or expect missing items from whichever mod lost.

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
- The choices are saved in `ZonaiFieldworks/settings.txt` on the emulated SD card, one
  `member-id=1` or `member-id=0` line per member. All save files share them.

## Known issues

- After switching Zonai Hookshot off, the paraglider was invisible once during testing. It did not
  happen again and the cause is not known.
- Phantom Foothold cannot be switched for terrain that is already loaded (see above).
- Only 1.2.1 has been played. Nothing has been tested on a Switch.

## Building from source

This folder contains only Fieldworks' own code: the wheel page, the switches, the settings file
and the script that composes the game files. It is **not** a complete project and comes with no
guarantee that it builds or works as-is.

- Fieldworks compiles every member from that member's source. The member folders in this
  repository are the standalone releases. They do **not** contain the switch callbacks that
  Fieldworks calls (the entry points added to each member for live switching), so this folder
  does not build against them as published.
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

`tests/run_host_tests.ps1` builds and runs the host test for the settings file and the wheel
captions. It needs CMake, Ninja and a C++23 compiler (`g++` by default, or set `CXX`) on `PATH`.
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
