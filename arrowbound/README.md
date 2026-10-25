# Arrowbound v0.2.5

## Changes in v0.2.5

Uses the shared 8 MiB loader reservation so installing this package does not overwrite the combined-mod memory fix. Arrowbound now chains Champion inventory hooks, restoring its emblem and arrow riding when BotW Abilities is installed.

The six-mod combination of Infinite Ascend, Fleet Sync, Self Recall (Switch), Zonai Hookshot with Arrowbound, BotW Abilities and Zonai Survey passed a physical Switch 1.2.1 session. Other game versions and combinations have not had a new test of the 8 MiB setting. Arrowbound was tested inside Hookshot; the standalone build was not separately boot-tested.

This update carries the shared Arrowbound code from Zonai Hookshot v0.12.1: per-build
address tables, the 1.4.x emblem and flight-clock fixes, and shared emblem selection/grant helpers. This standalone package keeps the 1.2.1 NPDM and is documented for
1.2.1 only; it is not separately boot-tested. New gameplay checks are deferred. For 1.0.0 through 1.4.3, use Zonai Hookshot.
The sibling runtime-support/include supplies the MIT detour decoder.

Ride your arrows with the paraglider open in Tears of the Kingdom 1.2.1.

## Controls

Activate the Arrowbound Emblem in Key Items, then shoot an arrow from the ground
or in midair. Arrow speed, gravity and range remain vanilla. Midair aiming keeps
slow motion; releasing the arrow ends it for the flight. Press B to detach into
ordinary paragliding. Drawing the bow again releases the old arrow before aiming.
Climbable-wall impacts turn Link toward the wall and hand off to normal climbing.

Activation persists in `sd:/arrowbound/settings.bin` or the emulator's emulated
SD card. It defaults off and is shared across all saves on that SD card, including
the combined Glideshot build. Loading an older save does not rewind the setting.
Storage failures leave a session-only setting.

New saves exclude an unearned All's Well carrier; an emblem earned through the
vanilla quest is preserved. Older saves are not rewritten until the game saves again.

## Installation

Requires game version 1.2.1, build 9B4E43650501A4D4. Close the game first.

- Emulator ZIP: copy its Arrowbound folder into the game's Mod Data Location,
  then enable the add-on.
- Switch ZIP: copy its 0100F2C0115B6000 folder into atmosphere/contents on the SD card.
- Back up existing files before upgrading. Remove only old Arrowbound ROMFS
  overrides; this package needs no ROMFS files. Leave other mods' files alone.

The payload is exefs/subsdk9 and exefs/main.npdm. Do not enable this standalone
mod alongside Glideshot, which already includes Arrowbound, or another subsdk9 mod.
Standalone Arrowbound does not include the manual ZL+R3 hookshot or visible chain.

## Testing status

Earlier shared gameplay passed inside Glideshot on Eden and physical Switch.
The 0.12.1 combined Zonai Hookshot build was observed working in Citron on
1.0.0, 1.2.1 and 1.4.3. Its other versions and Switch were not separately tested.
The standalone build passes 74 host tests but has not had a separate observed
boot. New gameplay checks for this refactor are deferred; the known high-speed
Arrowbound pose limitation remains.
Both downloads contain the same standalone executable with different install layouts.

## Known issues

At very high speed, Link's legs can tuck backward while flight remains visible
and smooth. This is not the previous mid-flight leg kick. Wing-fused arrows and
every modded bow have not been exhaustively tested.

## Source and license

Original mod code is MIT licensed, copyright Clay Mullis.
Source: https://github.com/afiaka87/totk-mods/tree/main/arrowbound
The compiled module includes exlaunch (GPL-2.0):
https://github.com/shadowninja108/exlaunch

No Nintendo game assets are distributed. See NOTICE.txt.
