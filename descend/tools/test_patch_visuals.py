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
        if address == BASE + self.report["native_services"]["snprintf"]:
            pointer, limit, fmt = [cpu.reg_read(r) for r in GPRS[:3]]
            pattern = self.read_string(fmt)
            assert pattern in ("%s->%s", "%s settled"), pattern
            values = tuple(self.read_string(cpu.reg_read(GPRS[3 + i])) for i in range(pattern.count("%s")))
            text = (pattern % values).encode()
            cpu.mem_write(pointer, text[:limit - 1] + b"\0")
            cpu.reg_write(GPRS[0], len(text))
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
            return
        if address in (BASE + 0xbf8934, BASE + 0xbf82b4):
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
                          (actor + 552, holder), (holder + 936, player)):
            put(at, "Q", value)
        put(node + 24, "B", 2)
        put(node + 60, "3f", 0, 0, 0)
        machine.fake_calls[BASE + 0xbf8934] = 0
        machine.fake_calls[BASE + 0xbf82b4] = 0

    results = []
    hook = "TravelAnimationHook"
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
            machine.fake_calls[BASE + 0xbf8934] = 1
        machine.fake_calls[BASE + machine.report["adapters"]["original_" + hook]] = 0x9001
        machine.cpu.reg_write(GPRS[0], node)
        machine.run(machine.report["adapters"]["entry_" + hook])
        assert machine.cpu.reg_read(GPRS[0]) == (0 if handled else 0x9001), machine.case
        calls = machine.animation_calls
        expected_calls = (1 if change == "already-playing" else 2) if handled else 0
        assert len(calls) == expected_calls, machine.case
        if handled:
            command = BASE + 0x441df98
            assert calls[0][1][:2] == [player, command], machine.case
            if len(calls) == 2:
                assert calls[1][1] == [player, 0, command, 15, 0], machine.case
        results.append(machine.case)

    # Return animation is vanilla again: no gate and no changed entry instructions.
    assert "entry_ReturnAnimationHook" not in machine.report["adapters"]
    assert bytes(machine.cpu.mem_read(BASE + 0x1c57d60, 20)) == struct.pack(
        "<5I", 0xa9be7bfd, 0xa9014ff4, 0x910003fd, 0x39406008, 0x37080088)
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

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"passed": len(results), "cases": results}, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(results)} real compiled animation/camera cases")


if __name__ == "__main__":
    main()
