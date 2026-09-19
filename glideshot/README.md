# Glideshot v0.8.1

A hookshot for Tears of the Kingdom. Aim at a climbable wall from the ground, from a climb or from
the air, fire a visible chain, zip along it at 60 m/s, and land in the game's own climbing state.
The paraglider opens by itself for the last stretch so the game decides the grab, not the mod.

## Controls

- Hold **ZL + L** for about a quarter of a second to raise the aim. A green
  diamond marks a wall the chain can take; a red diamond marks a surface it refuses.
- Press **A** to fire. The chain draws to the anchor at once and Link follows one tick later.
- Press **B** at any point to let go. Losing the world (a shrine door, a warp, a load) also ends
  the trip.

While the aim is up, ZL and L are hidden from the game so guard, lock-on and the ability wheel
stay quiet. Nothing else is remapped.

## What counts as a target

- Static world geometry only. Moving platforms, Zonai devices and anything with a rigid body the
  chain could not follow are refused.
- Walls, including shallow underhangs. Floors and ceilings are refused.
- Between 2 m and 300 m away. The game's own no-climb surfaces are refused.

The chain is a black spine with two green strands drawn per pixel by a small shader pass, occluded
by the scene depth buffer like any other object. The reticle keeps a constant apparent size.

## Sounds

The mod plays the game's own interface sounds through the game's sound system. The aim marker,
the accept chime, the refusal buzz and the shot each have a fixed cue. Travel start and arrival
prefer Ultrahand's activation and cancel sounds when the game has that sound user loaded, and fall
back to the interface cues otherwise. No audio files are shipped.

## Requirements and installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`, and an executable-mod loader
compatible with Atmosphere's contents layout.

1. Close the game.
2. Choose the emulator archive for Eden or another emulator, or the Switch archive for physical
   hardware. Both archives carry the same module; they are packaged separately so each download
   page can describe its own testing state.
3. Extract the archive and copy its `0100F2C0115B6000` folder into the loader's contents
   directory.
4. Confirm that the installed files are `exefs/subsdk9` and `exefs/main.npdm`.

Glideshot cannot be combined with another executable mod that supplies `subsdk9`. Other subsdk
slots can load beside it, but hook and memory compatibility still depends on the particular mods.

The emulator build was developed and tested on Eden. The Switch build is the same module and has
not been run on physical hardware yet.

## Known issues

- The chain's coil can look blurred past the halfway point on long shots.
- A very short shot can reach its half-metre standoff before the paraglider opens; the mod holds
  position until it does.
- The Ultrahand travel and arrival sounds depend on the game having its expression sound user
  loaded at that moment. When it is not, the interface fallbacks play instead.

## Changes in v0.8.1

- Activation is now ZL + L. This avoids UltraCam's ZL + ZR + L3 menu chord and Self Recall's
  ZL + R3 activation chord.
- Either button order works. If L opens the vanilla ability menu first, completing the chord closes
  that menu and activates Glideshot.
- The combined Eden test passed both activation orders, Self Recall activation and UltraCam's
  default-binding isolation.

## Changes in v0.8.0

- The chain now starts from the position the game actually draws Link at during the zip, which
  closes the gap that used to open between the coil and Link's torso.
- Travel start and arrival use Ultrahand's activation and cancel sounds, with interface fallbacks.
  The looping marker cue from earlier builds is gone.
- The debug text overlay, toasts and their font files are removed. The module no longer ships any
  romfs files.
- The mod-owned source is relicensed under MIT and no longer depends on any GPL-licensed helper
  code beyond exlaunch itself.
- Comments and tests were trimmed; the host suite covers the state machine, target validation,
  chain geometry, projection, capture math and cues.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. Apply the `InlineFloatCtx` fix from exlaunch issue #28 / PR #31;
  the jump-boost inline hook reads a float register through it.
- Layout: place `src/program/main.cpp`, `src/program/modules/`, `src/engine/`, `src/pure/` and
  `src/support/` in an exlaunch project with `src/program`, `src/pure`, `src/engine` and
  `src/support` on the include path.
- Shaders: `shaders/chain.frag`, `shaders/chain.vert` and `shaders/chain_math.inl` are compiled
  to NVN binaries by `tools/compile_shaders.py` (see `cmake/ChainShader.cmake`), which expects an
  external NVN GLSL compiler pinned by hash. The generated `ChainShaders.hpp` is not checked in.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "zonai-hookshot"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000`, `LogBufferSize 512`. Leave the
  reloc table in `offsets.hpp` empty.
- Compile definition: `TOTK_VERSION=121`. Program ID `0100F2C0115B6000`, module `subsdk9`.

## Tests

- `tests/run_host_tests.ps1` builds and runs the doctest suite for `src/pure/` (needs CMake, a
  C++23 compiler, and network access to fetch doctest when it is not vendored).

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- exlaunch by shadowninja108 and contributors.
- doctest (MIT); devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE` describes the exlaunch dependency.
