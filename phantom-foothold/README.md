# Phantom Foothold

Enables you to climb previously unclimbable walls (such as in shrines, the Great Sky Island and more).
Also enables you to climb on ceilings!

## Requirements

- Tears of the Kingdom 1.2.1. No other version will work. I don't have the time to port to every version unfortunately. 
- Eden
- Switch is currently not officially supported, but may work. I don't know. 

## Install (Eden)

1. Right-click the game in Eden and open the mod/load directory
   (`load/0100F2C0115B6000/`).
2. Extract the zip there, producing:
   ```
   phantom-foothold/exefs/subsdk9
   phantom-foothold/exefs/main.npdm
   phantom-foothold/cheats/9B4E43650501A4D4.txt
   ```
3. Enable **phantom-foothold** in the game's properties / add-ons list.

Developed and tested on the Eden emulator.

## Install (Switch, NOT SUPPORTED)

Copy `exefs/` to `atmosphere/contents/0100F2C0115B6000/exefs/` and the cheat file to
`atmosphere/contents/0100F2C0115B6000/cheats/9B4E43650501A4D4.txt`. Standard layout for
exlaunch-based mods, but this mod has not been tested on hardware.

## Compatibility

- Only **one** executable (`subsdk9`) mod can be active at a time. Disable other code mods
  (ordinary data/texture mods are unaffected). Two enabled subsdk9 mods migght hang the game at boot.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself. A lot of 
this is because the modding scene insists on using restrictive GPL licenses and I wanted to keep
my mods as open source as possible. 

- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
- Sources: compile `src/program/main_material_launderer.cpp` with `src/program` on the include path
  (`launder_policy.hpp` and `PhantomFootholdIntegration.hpp` live there).
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "phantomfoothold"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x10000`, `JitSize 0x5000`, `InlinePoolSize 0x2000`, `LogBufferSize 512`. Leave the
  reloc table in `offsets.hpp` empty.
- Program ID `0100F2C0115B6000`, module `subsdk9`. The cheat file in `cheats/` is applied by the
  emulator or Atmosphere, not compiled.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely - but this is work I do in 
my spare time, and I value my time. And again, the license is highly permissive (MIT) and I hope
 this adds to the community. My only intent is to create and share. 

## Credits

- exlaunch by shadowninja108 and contributors.
- devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE` describes the exlaunch dependency.
