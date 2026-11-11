# totk-mods

Source for my Tears of the Kingdom (1.2.1) mods. Each mod lives in its own folder with its own
README. Binaries are published on GameBanana and NexusMods; this repository holds source only.

As this is a repository for source code only I am not providing toolchain preferences and build instructions. 
I suspect the community is savvy enough to build it themselves. 

This is also to prevent any external GPL licensed code (shockingly common in the modding scene) from being shipped in the codebase. 
Exlaunch authors are owed attribution here. 


| Project | Folder | Version | Slot | What it does |
|---|---|---|---|---|
| Phantom Foothold | `phantom-foothold/` | v1.2.2 | subsdk2 | Climb every surface with collision, ceilings included; no game files replaced. |
| Ultimate Ascend | `ultimate-ascend/` | v0.16.0 | pchtxt / IPS32 | Downward and extended upward travel; nine game versions; experimental shrine restriction. |
| Zonai Hookshot (formerly Glideshot) | `zonai-hookshot/` | v0.12.4 | subsdk5 | A hookshot: chain to any climbable wall, zip along it, land in the native climb. Contains Arrowbound as well. |
| Fleet Sync | `fleet-sync/` | v0.2.4 | subsdk6 | Fly up to four identical receiver vehicles from one Steering Stick. |
| Material Columns | `material-columns/` | v0.1.6 | subsdk6 | A fifth materials quick-menu mode that groups materials into families you browse with the right stick; supports 1.2.0, 1.2.1, 1.4.2 and 1.4.3. |
| Bivouac | `bivouac/` | v0.26.3 | subsdk7 | Eat a Hearty Truffle, Big Hearty Truffle or Big Hearty Radish while climbing to build a persistent cliff or water camp you can travel back to. |
| BotW Abilities | `botw-abilities/` | v0.3.0 | subsdk7 | BotW's Stasis, Remote Bomb and Cryonis in one module, chosen from the ability wheel (second page: hold L, tap R), plus the four Champions' Blessings. |
| Self Recall | `self-recall/` | v1.1.3 | subsdk8 | Recall Link through his own movement and animation history. |
| Zonai Survey | `zonai-survey/` | v0.4.1 | subsdk9 | Scan the landscape for collectibles with visible names and icons. |
| Zonai Fieldworks | `zonai-fieldworks/` | v0.8.1 | subsdk6 | Eight of the mods above in one emulator package (Phantom Foothold, Zonai Hookshot, Fleet Sync, Material Columns, BotW Abilities, Bivouac, Self Recall, Zonai Survey), each switched on or off from an ability-wheel page. TotK 1.2.1. |
| Drive-by (formerly Free Rein) | `drive-by/` | v0.12.2 | none (patch files only) | Use the bow, sword and shield while holding a Zonai Steering Stick, and keep driving. |
| Runtime support | `runtime-support/` | | | Shared source used by several mods above (font and texture drawing, input, actor and hook helpers). Not a mod on its own. |


All code here is MIT-licensed (see `LICENSE`). The feature mods use exlaunch. It is not included. See `NOTICE`.

## Executable slots

Each code mod loads from its own `subsdk` slot, so mods with different slots can be installed
together. Only one mod per slot can be enabled:

- Fleet Sync and Material Columns both use subsdk6. Enable one of them.
- Bivouac and BotW Abilities both use subsdk7. Enable one of them.
- Zonai Fieldworks uses subsdk6 and already contains eight of the mods. Disable their standalone copies when it is enabled.

Slots 1 and 3 are left available for TKMM and UltraCam. These assignments apply to
new slot-migrated packages, not older downloads. Remove each upgraded mod's old
subsdk9 file; do not remove a different mod's slot 9 executable.

Different slots avoid file collisions, not every hook or memory conflict.
