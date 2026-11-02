"""Exercise actual animation and camera callbacks; native animation/render services are mocked."""
from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from test_patch_runtime import BASE, GPRS, HEAP, STOP, Machine
from unicorn import arm64_const as arm


class VisualMachine(Machine):
    def instruction(self, cpu, address, size, context):
        if address == BASE + self.report["native_services"]["sqrtf"]:
            value = struct.unpack("<f", struct.pack("<I", cpu.reg_read(arm.UC_ARM64_REG_S0)))[0]
            result = math.sqrt(value) if value >= 0 else float("nan")
            cpu.reg_write(arm.UC_ARM64_REG_S0, struct.unpack("<I", struct.pack("<f", result))[0])
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
            return
        if address == BASE + self.report["native_services"].get("snprintf", -1):
            pointer, limit, fmt = [cpu.reg_read(r) for r in GPRS[:3]]
            pattern = self.read_string(fmt)
            assert pattern in ("%s->%s", "%s settled"), pattern
            values = tuple(self.read_string(cpu.reg_read(GPRS[3 + i])) for i in range(pattern.count("%s")))
            text = (pattern % values).encode()
            cpu.mem_write(pointer, text[:limit - 1] + b"\0")
            cpu.reg_write(GPRS[0], len(text))
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
            return
        if address in (BASE + self.bindings["kCheckCommand"], BASE + self.bindings["kPlayCommand"]):
            self.animation_calls.append((address - BASE, [cpu.reg_read(r) for r in GPRS[:5]]))
        super().instruction(cpu, address, size, context)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("layout", "tool-root", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    machine = VisualMachine(args.layout, args.tool_root)
    offsets, types = {}, {}
    with (args.layout / "native/descend-patch.o").open("rb") as stream:
        elf = ELFFile(stream)
        cursor, bases = 0, {}
        for i, section in enumerate(elf.iter_sections()):
            if not section["sh_size"] or not section["sh_flags"] & 2 or section["sh_flags"] & 4 or section.name in (
                ".data.patch_callback_roots", ".data.patch_guard_root"):
                continue
            alignment = max(1, section["sh_addralign"])
            cursor = (cursor + alignment - 1) & -alignment
            bases[i] = cursor
            cursor += section["sh_size"]
        for symbol in elf.get_section_by_name(".symtab").iter_symbols():
            if symbol["st_shndx"] in bases:
                offsets[symbol.name] = bases[symbol["st_shndx"]] + symbol["st_value"]
        # Compiler layout, not hand-maintained guesses about C++ padding.
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                name = die.attributes.get("DW_AT_name")
                if die.tag == "DW_TAG_structure_type" and name and name.value in (b"Event", b"CameraUp") and \
                        "DW_AT_byte_size" in die.attributes:
                    types[name.value.decode()] = {
                        child.attributes["DW_AT_name"].value.decode(): child.attributes["DW_AT_data_member_location"].value
                        for child in die.iter_children() if child.tag == "DW_TAG_member" and
                        "DW_AT_data_member_location" in child.attributes}
    assert cursor == len(machine.data)

    def address(name):
        matches = [HEAP + offset for symbol, offset in offsets.items() if symbol.endswith(name + "E")]
        assert len(matches) == 1, name
        return matches[0]

    def function(name):
        matches = [value for symbol, value in machine.report["runtime_symbols"].items() if name in symbol]
        assert len(matches) == 1, name
        return matches[0]

    def put(at, fmt, *values):
        machine.cpu.mem_write(at, struct.pack("<" + fmt, *values))

    def get(at, fmt):
        return struct.unpack("<" + fmt, machine.cpu.mem_read(at, struct.calcsize("<" + fmt)))[0]

    def event(name, fmt, *values):
        put(address("g_event") + types["Event"][name], fmt, *values)

    node, context, owner, actor, holder, player = [HEAP + x for x in (0x4000, 0x5000, 0x6000, 0x7000, 0x8000, 0x9000)]
    camera, saved = HEAP + 0xc000, HEAP + 0xd000
    tick = 19_200_000

    def reset(name):
        machine.reset()
        machine.set_ready()
        machine.case = name
        machine.animation_calls = []
        machine.fixture_writes = [(camera, camera + 0x300), (saved, saved + 32)]
        event("active", "B", 1)
        event("serial", "I", 12)
        event("actor", "Q", actor)
        event("player", "Q", player)
        event("target", "3f", 0, 0, 0)
        event("touched", "Q", tick)
        for at, value in ((node + 8, context), (context + 8, owner), (owner + 504, actor),
                          (actor + machine.bindings["actor::kComponents"], holder), (holder + 936, player)):
            put(at, "Q", value)
        put(node + 24, "B", 2)
        put(node + 60, "3f", 0, 0, 0)
        machine.fake_calls[BASE + machine.bindings["kCheckCommand"]] = 0
        machine.fake_calls[BASE + machine.bindings["kPlayCommand"]] = 0

    results = []
    hook = "TravelAnimationHook"

    def check_animation(handled, already_playing=False):
        machine.fake_calls[BASE + machine.report["adapters"]["original_" + hook]] = 0x9001
        machine.cpu.reg_write(GPRS[0], node)
        machine.run(machine.report["adapters"]["entry_" + hook])
        assert machine.cpu.reg_read(GPRS[0]) == (0 if handled else 0x9001), machine.case
        calls = machine.animation_calls
        assert len(calls) == ((1 if already_playing else 2) if handled else 0), machine.case
        if handled:
            command = BASE + machine.bindings["kCommandFall"]
            assert calls[0][1][:2] == [player, command], machine.case
            if len(calls) == 2:
                assert calls[1][1] == [player, 0, command, 15, 0], machine.case
        results.append(machine.case)

    cases = [(f"state-{state}", state, None, int(state < 5))
             for state in range(8)]
    cases += [(name, 3, name, 0) for name in ("inactive", "foreign", "released", "disabled", "missing-player")]
    cases += [("wrong-target", 3, "wrong-target", 0),
              ("already-playing", 3, "already-playing", 1)]
    for name, state, change, handled in cases:
        reset(hook + "-" + name)
        put(node + 32, "I", state)
        if change == "inactive":
            event("active", "B", 0)
        elif change == "foreign":
            event("actor", "Q", actor + 0x100)
        elif change == "released":
            event("released", "B", 1)
        elif change == "disabled":
            put(node + 24, "B", 0)
        elif change == "missing-player":
            put(holder + 936, "Q", 0)
        elif change == "wrong-target":
            put(node + 64, "f", 1)
        elif change == "already-playing":
            machine.fake_calls[BASE + machine.bindings["kCheckCommand"]] = 1
        check_animation(handled, change == "already-playing")

    # Hold actor origin still while native attention moves with the pose. Render focus must follow it, including before the delayed preview camera starts and halfway through that camera blend.
    for elapsed in (0, 500, 1999, 2000, 2090, 2180, 2500):
        reset(f"animated-focus-{elapsed}")
        machine.cpu.mem_write(camera, bytes(0x300))
        target_component = HEAP + 0xe300
        machine.fixture_writes.extend(((target_component, target_component + 256),))
        event("route", "B", 1)
        put(target_component + 24, "Q", actor)
        put(actor + machine.bindings["actor::kPosition"], "3f", 0, 0, 0)
        put(target_component + 212, "3f", 0, 1.7, 0)
        machine.cpu.reg_write(GPRS[0], target_component)
        machine.run(function("afterCameraTarget"))
        assert get(address("g_event") + types["Event"]["focusReady"], "B") == 1
        event("previewTick", "Q", tick)
        # This axis was absent from the old tests: actual actor Y is unchanged; animated body Y is not.
        put(target_component + 212, "3f", 0, -1, 0)
        machine.cpu.reg_write(GPRS[0], target_component)
        machine.run(function("afterCameraTarget"))
        event("startTick", "Q", tick // 10)
        event("previewReady", "B", 1)
        event("previewPose", "6f", 0, -1, -4, 0, -2, 0)
        put(camera + 232, "3d", 0, 1.2, -4)
        put(camera + 272, "3d", 0, 0.9, 0)
        put(camera + 296, "3f", 0, 1, 0)
        machine.fake_calls[BASE + machine.report["native_services"]["svcGetSystemTick"]] = tick + elapsed * 19200
        original = machine.report["adapters"]["original_CameraUpdateHook"]
        machine.stop_at = BASE + original
        machine.cpu.reg_write(GPRS[0], camera)
        machine.run(machine.report["adapters"]["entry_CameraUpdateHook"])
        assert abs(get(camera + 416, "d") + 1.8) < 0.001, (machine.case,get(camera + 416, "d"),get(address("g_event") + types["Event"]["focusY"], "f"),get(address("g_event") + types["Event"]["focusOffset"], "f"),get(address("g_event") + types["Event"]["focusReady"], "B"))
        assert abs(get(address("g_event") + types["Event"]["link"] + 4, "f")) < 0.001
        results.append(machine.case)

    for attention_y in (float("nan"), float("inf"), float("-inf"), 80):
        reset(f"invalid-animated-focus-{attention_y}")
        target_component = HEAP + 0xe300
        machine.fixture_writes.extend(((target_component, target_component + 256),))
        put(target_component + 24, "Q", actor)
        put(actor + machine.bindings["actor::kPosition"], "3f", 0, 0, 0)
        put(target_component + 212, "3f", 0, attention_y, 0)
        event("focusReady", "B", 1)
        machine.cpu.reg_write(GPRS[0], target_component)
        machine.run(function("afterCameraTarget"))
        assert get(address("g_event") + types["Event"]["focusReady"], "B") == 0, machine.case
        results.append(machine.case)

    # Native preview is already loading; only its pose waits. The first animation update precedes the timestamp.
    for route in (0, 1, 2):
        for elapsed in (None, 0, 1999, 2000, 2500):
            for state in (5, 6):
                reset(f"covered-preview-animation-{route}-{elapsed}-{state}")
                put(node + 32, "I", state)
                event("route", "B", route)
                event("previewTick", "Q", 0 if elapsed is None else tick)
                machine.fake_calls[BASE + machine.report["native_services"]["svcGetSystemTick"]] = \
                    tick + (elapsed or 0) * 19200
                check_animation(state == 5 and route == 1 and (elapsed is None or elapsed < 2000))

    for change in ("inactive", "foreign", "released", "disabled", "missing-player", "wrong-target"):
        reset("covered-preview-animation-guard-" + change)
        put(node + 32, "I", 5)
        event("route", "B", 1)
        if change == "inactive": event("active", "B", 0)
        elif change == "foreign": event("actor", "Q", actor + 0x100)
        elif change == "released": event("released", "B", 1)
        elif change == "disabled": put(node + 24, "B", 0)
        elif change == "missing-player": put(holder + 936, "Q", 0)
        elif change == "wrong-target": put(node + 64, "f", 1)
        check_animation(False)

    # Run the real native fallback: it must select preview after the delay even though its first call was skipped.
    for elapsed in (1999, 2000):
        reset(f"delayed-preview-native-animation-{elapsed}")
        put(node + 32, "I", 5)
        event("route", "B", 1)
        event("previewTick", "Q", tick)
        machine.fake_calls[BASE + machine.report["native_services"]["svcGetSystemTick"]] = tick + elapsed * 19200
        machine.fake_calls[BASE + machine.native['player']] = player
        machine.fake_calls[BASE + machine.native['actor']] = actor
        machine.fake_calls[BASE + machine.native['preview_actor_check']] = 0
        machine.cpu.reg_write(GPRS[0], node)
        machine.run(machine.report["adapters"]["entry_" + hook])
        command = BASE + (machine.bindings["kCommandFall"] if elapsed < 2000 else machine.native['preview_command'])
        assert len(machine.animation_calls) == 2, machine.case
        assert machine.animation_calls[1][1] == [player, 0, command, 15, 0], machine.case
        results.append(machine.case)

    # Native state5 permits B before the animation is ready; A and its prompt wait for the AS ready event. Native updateState and its pause-module access are verified per game image.
    pause_slot = BASE + machine.native['pause_address']
    original_slot = bytes(machine.cpu.mem_read(pause_slot, 8))
    for ready in (False, True):
        for button in ("A", "B"):
            reset(f"native-preview-input-{ready}-{button}")
            machine.fixture_writes.extend(((node, node + 0x100), (player, player + 0x2000)))
            put(node + 32, "I", 5)
            put(node + 116, "f", 1)
            put(node + 129, "B", 0)
            put(player + 2790, "B", 8 if button == "B" else 0)
            as_event, pause_holder, pause_module = HEAP + 0xe000, HEAP + 0xe100, HEAP + 0xe200
            put(as_event + 64, "B", 0x10 if ready else 0)
            put(pause_slot, "Q", pause_holder if machine.native['pause_indirect'] else pause_module)
            if machine.native['pause_indirect']:
                put(pause_holder, "Q", pause_module)
            put(pause_module + machine.native['pause_member'], "H", 0)
            for role, value in (('actor', actor), ('player', player), ('event', as_event),
                                ('timer', 0), ('game_data', 1), ('confirm', 1)):
                machine.fake_calls[BASE + machine.native[role]] = value
            machine.cpu.reg_write(GPRS[0], node)
            machine.run(machine.native['state'])
            assert get(node + 32, "I") == (6 if ready or button == "B" else 5), machine.case
            assert get(node + 129, "B") == int(ready and button == "A"), machine.case
            results.append(machine.case)
    machine.cpu.mem_write(pause_slot, original_slot)

    # Return animation is vanilla again: no gate and no changed entry instructions.
    assert "entry_ReturnAnimationHook" not in machine.report["adapters"]
    assert bytes(machine.cpu.mem_read(BASE + machine.native['return_animation'], 20)) == struct.pack(
        "<5I", *machine.native['return_animation_words'])
    results.append("native-return-animation-entry-restored")

    # A completed preview snapshot cannot uncover a new event, a return, or a rejected camera.
    for change in ("current", "new-event", "new-preview", "return", "cancel", "released", "inactive",
                   "blocked-camera", "not-applied", "not-settled"):
        reset("preview-completion-" + change)
        event("previewTick", "Q", 123)
        event("previewReady", "B", 1)
        put(address("g_swirlWanted"), "I", 1)
        machine.cpu.mem_write(saved, bytes(32))
        put(saved + types["CameraUp"]["saved"], "B", int(change != "not-applied"))
        put(saved + types["CameraUp"]["up"], "3f", 0, 1, 0)
        put(saved + types["CameraUp"]["uncoverSerial"], "I", 0 if change == "not-settled" else 12)
        put(saved + types["CameraUp"]["previewTick"], "Q", 123)
        changes = {"new-event": ("serial", "I", 13), "new-preview": ("previewTick", "Q", 456),
                   "return": ("leaveTick", "Q", 456), "cancel": ("cancelTick", "Q", 456),
                   "released": ("released", "B", 1), "inactive": ("active", "B", 0),
                   "blocked-camera": ("previewReady", "B", 0)}
        if change in changes:
            event(*changes[change])
        machine.cpu.reg_write(GPRS[0], camera)
        machine.cpu.reg_write(GPRS[1], saved)
        machine.run(function("afterCameraUpdate"))
        assert get(address("g_swirlWanted"), "I") == int(change != "current"), machine.case
        results.append(machine.case)

    # Run the real camera hook across its native-render boundary, then change ownership while it is suspended.
    for change in ("current", "new-descent", "B-return", "vertical-view", "nonfinite-view"):
        reset("camera-hook-" + change)
        machine.cpu.mem_write(camera, bytes(0x300))
        event("startTick", "Q", tick // 10)
        event("previewTick", "Q", tick // 5)
        event("previewReady", "B", 1)
        event("link", "3f", 0, 0, 0)
        pose = (0, -1, -4, 0, -2, 0)
        if change == "vertical-view":
            pose = (0, -1, 0, 0, -2, 0)
        elif change == "nonfinite-view":
            pose = (float("nan"), -1, -4, 0, -2, 0)
        event("previewPose", "6f", *pose)
        put(camera + 232, "3d", 0, -1, -4)
        put(camera + 272, "3d", 0, -2, 0)
        put(camera + 296, "3f", 0, 1, 0)
        put(address("g_swirlWanted"), "I", 1)
        original = machine.report["adapters"]["original_CameraUpdateHook"]
        machine.cpu.reg_write(GPRS[0], camera)
        machine.stop_at = BASE + original
        machine.run(machine.report["adapters"]["entry_CameraUpdateHook"])
        assert get(address("g_swirlWanted"), "I") == 1, "Uncovered before the camera was applied"
        if change == "new-descent":
            event("serial", "I", 13)
        elif change == "B-return":
            event("leaveTick", "Q", tick)
        machine.fake_calls[BASE + original] = 0
        machine.stop_at = STOP
        machine.run(original)
        assert get(address("g_swirlWanted"), "I") == int(change != "current"), machine.case
        assert bytes(machine.cpu.mem_read(camera + 296, 12)) == struct.pack("<3f", 0, 1, 0), machine.case
        results.append(machine.case)

    # Track Link during the delay, then blend to preview for 180ms under cover. A/B transitions cannot race uncover.
    for route,elapsed,expected in ((0,179,1),(0,180,0),(2,180,0),
                                   (1,180,1),(1,1999,1),(1,2000,1),(1,2090,1),
                                   (1,2179,1),(1,2180,0),(1,2500,0)):
        for leaving in ('none','A-drop','B-return'):
            reset(f'preview-cover-{route}-{elapsed}-{leaving}')
            machine.cpu.mem_write(camera,bytes(0x300))
            event('startTick','Q',tick//10)
            event('previewReady','B',1)
            event('route','B',route)
            event('previewPose','6f',0,-1,-4,0,-2,0)
            put(camera+232,'3d',0,-1,-4)
            put(camera+272,'3d',0,-2,0)
            put(camera+296,'3f',0,1,0)
            put(address('g_swirlWanted'),'I',1)
            original=machine.report['adapters']['original_CameraUpdateHook']
            # Establish the preceding descent camera rather than testing only a newly admitted camera.
            for prior_ms in (-600,-300):
                machine.fake_calls[BASE+machine.report['native_services']['svcGetSystemTick']]=tick+prior_ms*19200
                machine.fake_calls[BASE+original]=0
                machine.cpu.reg_write(GPRS[0],camera)
                machine.run(machine.report['adapters']['entry_CameraUpdateHook'])
            event('previewTick','Q',tick)
            machine.fake_calls.pop(BASE+original)
            now_tick=tick+elapsed*19200
            machine.fake_calls[BASE+machine.report['native_services']['svcGetSystemTick']]=now_tick
            machine.cpu.reg_write(GPRS[0],camera)
            machine.stop_at=BASE+original
            machine.run(machine.report['adapters']['entry_CameraUpdateHook'])
            assert get(address('g_swirlWanted'),'I') == 1
            look_y=get(camera+416,'d')
            if route == 1 and elapsed <= 2000:
                assert abs(look_y-0.9) < 0.001,(machine.case,look_y)
            elif route == 1 and elapsed == 2090:
                assert -2 < look_y < 0.9,(machine.case,look_y)
            elif not expected:
                assert abs(look_y+2) < 0.001,(machine.case,look_y)
            if leaving != 'none':
                event('leaveTick','Q',now_tick)
                event('dropped','B',int(leaving == 'A-drop'))
            machine.fake_calls[BASE+original]=0
            machine.stop_at=STOP
            machine.run(original)
            assert get(address('g_swirlWanted'),'I') == (expected if leaving == 'none' else 1),machine.case
            # Even an early A-drop must retire the still-covered preview at native leave.
            if leaving != 'none':
                machine.cpu.reg_write(GPRS[0],node)
                machine.run(function('afterCancelLeave'))
                assert get(address('g_swirlWanted'),'I') == 0
            results.append(machine.case)

    reset("depths-return-camera-retains-cover-past-three-seconds")
    now_tick = tick * 8
    machine.fake_calls[BASE + machine.report["native_services"]["svcGetSystemTick"]] = now_tick
    machine.cpu.mem_write(camera, bytes(0x300))
    event("startTick", "Q", tick)
    event("leaveTick", "Q", tick * 2)
    event("cancelTick", "Q", now_tick)
    event("touched", "Q", now_tick)
    event("link", "3f", 0, 100, 0)
    put(camera + 232, "3d", 0, 101, -4)
    put(camera + 272, "3d", 0, 101, 0)
    put(address("g_ownedSerial"), "I", 12)
    put(address("g_swirlWanted"), "I", 1)
    machine.fake_calls[BASE + machine.report["adapters"]["original_CameraUpdateHook"]] = 0
    machine.cpu.reg_write(GPRS[0], camera)
    machine.run(machine.report["adapters"]["entry_CameraUpdateHook"])
    assert get(address("g_ownedSerial"), "I") == 12
    assert get(address("g_swirlWanted"), "I") == 1
    results.append(machine.case)

    for phase in ('sink', 'preview', 'return'):
        reset('pause-resume-cover-' + phase)
        machine.cpu.mem_write(camera, bytes(0x300))
        event('startTick', 'Q', tick)
        event('touched', 'Q', tick * 2)
        if phase == 'preview': event('previewTick', 'Q', tick * 2)
        if phase == 'return':
            event('leaveTick', 'Q', tick * 2)
            event('cancelTick', 'Q', tick * 2)
        event('link', '3f', 0, 100, 0)
        put(camera + 232, '3d', 0, 101, -4)
        put(camera + 272, '3d', 0, 101, 0)
        put(address('g_ownedSerial'), 'I', 12)
        put(address('g_swirlWanted'), 'I', 1)
        put(address('g_effectTick'), 'Q', tick * 2)
        machine.fake_calls[BASE + machine.report['native_services']['svcGetSystemTick']] = tick * 8
        # Cover must survive even when the effect update precedes the first resumed camera update.
        slot, effect_global, effect_owner = HEAP+0xe000, HEAP+0xe100, HEAP+0xe200
        flags = effect_owner + 0xca4
        for at,value in ((BASE+machine.bindings["ceiling_effect::kGlobalSlot"],slot),(slot,effect_global),(effect_global+0x128,effect_owner)):
            put(at,'Q',value)
        put(flags,'I',0x20)
        machine.fixture_writes.append((flags,flags+4))
        machine.run(function('synchronizeScreenCover'))
        assert get(flags,'I') == 0x8020
        original = machine.report['adapters']['original_CameraUpdateHook']
        machine.fake_calls[BASE+original] = 0
        machine.cpu.reg_write(GPRS[0],camera)
        machine.run(machine.report['adapters']['entry_CameraUpdateHook'])
        assert get(address('g_ownedSerial'),'I') == 12
        assert get(address('g_swirlWanted'),'I') == 1
        if phase == 'return':
            machine.cpu.reg_write(GPRS[0],node)
            machine.run(function('beforeCancelStage'))
            assert get(address('g_swirlWanted'),'I') == 1
        # Native completion still retires ownership after the normal handback.
        event('released','B',1)
        event('touched','Q',tick * 8)
        event('cancelTick','Q',tick * 8)
        machine.fake_calls[BASE+machine.report['native_services']['svcGetSystemTick']] = tick * 10
        machine.cpu.reg_write(GPRS[0],camera)
        machine.run(machine.report['adapters']['entry_CameraUpdateHook'])
        assert get(address('g_ownedSerial'),'I') == 0
        assert get(address('g_swirlWanted'),'I') == 0
        machine.run(function('synchronizeScreenCover'))
        assert get(flags,'I') == 0x20
        results.append(machine.case)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"passed": len(results), "cases": results}, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(results)} real compiled animation/camera cases")


if __name__ == "__main__":
    main()
