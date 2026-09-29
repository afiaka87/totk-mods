# Free Rein v0.4.0

Use your bow while holding a Zonai Steering Stick. When you draw the bow, Link takes his hands off
the stick, holds the bow normally and turns his body and legs to aim where the camera points. When
you lower the bow, his hands go back to the stick.

## Requirements

Tears of the Kingdom **1.2.1** only. Both files in the download are keyed to 1.2.1's program, so
other versions ignore them and play normally.

## Installation

Close the game first. Choose the archive for where you play.

### Emulators: `free-rein-v0.4.0-emulator.zip`

1. Open the game's mod folder. In most emulators, right-click the game and choose
   *Open Mod Data Location*; it is `load/0100F2C0115B6000/`.
2. Copy the `Free Rein` folder from the archive into it, so you have:
   ```
   load/0100F2C0115B6000/Free Rein/exefs/free-rein.pchtxt
   load/0100F2C0115B6000/Free Rein/cheats/9B4E43650501A4D4.txt
   ```
3. Enable **Free Rein** in the game's Add-Ons list.

The emulator applies `free-rein.pchtxt` when the game starts. The cheat file writes the same
changes; an emulator that also loads it simply repeats them.

### Switch (Atmosphere): `free-rein-v0.4.0-switch.zip`

1. Copy the archive's `0100F2C0115B6000` folder into `atmosphere/contents/` on the SD card, so you
   have:
   ```
   atmosphere/contents/0100F2C0115B6000/cheats/9B4E43650501A4D4.txt
   ```
   If that file already exists, another mod's 1.2.1 cheats are in it (Phantom Foothold ships one).
   Do not replace it: add every line of Free Rein's file, from `[Free Rein v0.4.0]` to the end, at
   the end of the existing file. Free Rein's lines touch none of Phantom Foothold's addresses.
2. Atmosphere turns cheats on at game launch by default. If you have set cheats to start off,
   enable **Free Rein v0.4.0** in your cheat manager before loading a save.

Atmosphere does not read `.pchtxt` files, so `exefs/free-rein.pchtxt` does nothing on a Switch.

## Compatibility

Free Rein uses no executable module slot, so it loads beside exlaunch mods. It conflicts with
any patch or cheat that changes the same places in the game's program (offsets from the start of
the main program, as in the cheat file; the `.pchtxt` offsets are `0x100` higher):

- the list of actions the bow may run beside (`0xB7DC20` and `0xB7DCD0`-`0xB7DCEF`);
- the Steering Stick's animation choice (`0x1D6A6C4`);
- the hand placement check (`0x82D540`);
- the unused space at the end of the program's code that holds the new code
  (`0x2B19F00`-`0x2B19FC3`).

## Testing

v0.4.0 was played on 1.2.1 three ways: in Eden with the patch, in Eden with only the cheat file,
and on a Switch with the cheat file beside three other code mods. Each time the bow drew and fired
while holding a Steering Stick, Link's hands left the stick, his legs turned toward the aim all the
way round, and his hands went back to the stick when the bow was lowered.

## Uninstall

Disable or delete the `Free Rein` folder on an emulator. On the Switch, delete the cheat file, or
remove Free Rein's lines if you added them to another mod's file. The mod writes no save data.

## How it works

The two files in `patches/` contain the same 50 instruction words: `free-rein.pchtxt` as an
IPSwitch text patch for emulators, and `9B4E43650501A4D4.txt` as an Atmosphere cheat. The
cheat writes the new code first and the two jumps into it last. The patch makes three changes:

1. **Allow the bow on the stick.** The game checks a list of actions the bow can run beside and
   refuses it while the Steering Stick is held. Nine words add the Steering Stick to that list.
2. **Aim with the legs.** While the bow is drawn on the stick, the stick's animation update plays
   the lower-body vehicle animation the sand seal sled uses for bow aiming, and passes it the
   camera direction as the sled does, so the legs turn toward the aim.
3. **Free the hands.** The stick holds Link's hands on its handles every frame. While the bow is
   drawn, the hand placement is switched off the same way the game does when you let go of the
   stick. It switches back on when you lower the bow.
