# Fleet Sync v0.2.2

Fly up to four identical receiver vehicles from one Steering Stick. Shared input and
fast alignment keep the fleet together through takeoff and turns, without an extra HUD.

## Installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`. Uses **subsdk6**.
Switch and emulator archives contain the same executable with different folder layouts.

- **Emulator:** extract into the game's mod/load directory, producing
  `Fleet Sync/exefs/`. Enable **Fleet Sync** in the add-ons list.
- **Switch:** extract into `atmosphere/contents/` on the SD card, producing
  `0100F2C0115B6000/exefs/`.
- Close the game first and back up existing executable files. Do not overwrite another
  mod's subsdk6. Remove the old Linked-Stick Remote Pilot add-on or Fleet Sync's old
  subsdk9 when upgrading; preserve other mods' subsdk9 files.
- To uninstall, remove Fleet Sync's add-on or subsdk6 with the game closed.
  Preserve shared files used by other mods. No save changes are required.

## Controls

Stand beside the intended Steering Stick with shoulder/trigger buttons released.

1. **Tap both stick-clicks together, then release:** first stick becomes the controller,
   confirmed by a map-marker tone.
2. Repeat beside each identical vehicle to add a receiver, confirmed by a different
   short chime. Add up to four receivers, then mount the controller normally.
3. **Hold both stick-clicks for 1.5 seconds:** clear all links, with a reset sound.
   Release both before pairing again. Dismount before adding receivers.

Refused selections play an error sound. Failed additions preserve the existing fleet.
Clear and re-pair to change controllers or after editing vehicles.

L, R, ZL, ZR, Plus or Minus cancels the gesture without consuming those buttons.
Audited default gameplay shortcuts for UltraCam and the other published mods do not
overlap. Close UltraCam's free-camera/sequencer before pairing; those modes use
individual stick-clicks. Custom remappings are not covered. Press both clicks together:
an isolated click can still crouch or open the telescope.

## Formation and limitations

Start with identical Autobuild hoverbikes around 8 metres apart on level ground.
Parts and stick-relative geometry must match; different world headings are allowed.
Loose sticks, different builds, incomplete scans and two sticks on one vehicle are refused.

Roughly side-by-side vehicles form a straight row. Small fore/aft errors are removed;
noticeably deliberate front/back placements are kept. Receivers target the controller's
height and orientation and recover quickly from drift. Alignment is deliberately strong,
not a rigid pose lock or teleport. A single receiver starting much too far away or too
close can acquire a compact side lane instead.

Active receivers pass through trees, loose objects and characters. Terrain, water and
ground-class structures remain solid. Dismounting or clearing restores normal collision;
the controller's collision never changes. Uneven terrain, severe water starts, some
scenery and ordinary device expiry can still disrupt formation. Hoverbikes are the
tested target, not every possible vehicle.

Different executable slots avoid file collisions, not all hook conflicts. Basic flight,
pairing cues and clearing passed a physical Switch test alongside Arrowbound/Zonai
Hookshot. Scene changes or invalidated vehicles clear links. Pairings are not saved.

Logging is automatic, including failures, with no dismount or normal run ending required.
Logs use emulator debug output; this build has no persistent Fleet Sync SD-card logger.

## Development and license

v0.2.2 cleans up the accepted flight build and prepares standalone host tests and releases.
Owned source is MIT Licensed. The executable also includes GPL-2.0 exlaunch;
removing the text renderer does not relicense that dependency. See [LICENSE](LICENSE)
and [NOTICE](NOTICE.txt). No game assets or fonts are distributed.

## Building from source

This is a source-only folder, not a standalone native build tree. No promise is made
that it builds unchanged without the matching external dependencies. Native builds
need devkitA64, compatible TotK 1.2.1 declarations and exlaunch base
`f698816d6e198afb0029ad5c07d55e7017a620fe`.
Use exlaunch's module build machinery and consumer-template headers; these are not
included here. Compile `src/program/main.cpp`, `src/engine/*.cpp` and
`src/feature/*.cpp`, adding `src` and `src/support` to the include paths.

Set `TOTK_VERSION=121`, `TOTK_121=1`, title ID `0100F2C0115B6000`,
module name `fleet-sync`, subsdk slot 6, fake heap enabled, no `EXL_DEBUG`,
`HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000` and
`LogBufferSize 1024`. The build uses the version-matched relocation table and
exlaunch's named-relocation/hook-limit adaptations. The local InlineFloatCtx
adaptation is present in the framework but this mod uses integer-only inline contexts.
The first-party `support/FleetLogSink.hpp` sends newline-terminated records through
`svcOutputDebugString`. Include it from the consumer logger header and declare
`inline exl::log::LoggerMgr<LinkedStickLogSink> Logging;` there.
The other platform/build adapters are external to the MIT source export.

Host tests run independently with doctest 2.4.11 (fetched when absent), CMake 3.25+,
Ninja and a C++23 compiler. On Windows install Visual Studio Build Tools and run:

```powershell
powershell -File tests/run_host_tests.ps1
```

On other systems use CMake directly:

```sh
cmake -S tests -B tests/build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build tests/build-host
ctest --test-dir tests/build-host --output-on-failure
```

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

exlaunch contributors, devkitPro and doctest.
