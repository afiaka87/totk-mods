"""Exercise compiled effect callbacks through real gates and native choice branches."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from test_patch_runtime import BASE, GPRS, HEAP, STOP, VECTORS, Machine
from unicorn import arm64_const as arm
from unicorn import UC_HOOK_CODE


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("layout", "tool-root", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    machine = Machine(args.layout, args.tool_root)
    offsets = {}
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
    assert cursor == len(machine.data), "Test state layout differs from the compiled image"

    def state(name, value, wide=False):
        matches = [offset for symbol, offset in offsets.items() if symbol.endswith(name + "E")]
        assert len(matches) == 1, name
        address = HEAP + matches[0]
        machine.cpu.mem_write(address, struct.pack("<Q" if wide else "<I", value))

    results = []
    clipper, pool, query = HEAP + 0x3000, HEAP + 0x4000, HEAP + 0x7000

    def marker_fixture():
        machine.fixture_writes = [(HEAP + 0x3000, HEAP + 0x8000)]
        machine.cpu.mem_write(clipper, bytes(0x100))
        machine.cpu.mem_write(BASE + 0x462f290, struct.pack("<Q", HEAP + 0x3800))
        machine.cpu.mem_write(BASE + 0x462f298, struct.pack("<Q", HEAP + 0x3810))
        machine.cpu.mem_write(HEAP + 0x3800, struct.pack("<Q", pool))
        machine.cpu.mem_write(HEAP + 0x3810, struct.pack("<Q", 0x200))
        for i in range(4):
            machine.cpu.mem_write(pool + i * 0x200, bytes(0x200))
            machine.cpu.mem_write(pool + i * 0x200 + 32, struct.pack("<I", 0x1110 + i))
            machine.cpu.mem_write(pool + i * 0x200 + 196, struct.pack("<f", 1))
            machine.cpu.mem_write(clipper + 60 + i * 16, struct.pack("<bbhI", 0, 0, i, 0x1110 + i))
            machine.cpu.mem_write(clipper + 68 + i * 16, struct.pack("<Q", 0x00000000ffff00ff))

    for mode in ("warm", "cold", "stale", "off", "recycled"):
        machine.reset()
        machine.set_ready()
        marker_fixture()
        machine.case = "marker-blend-" + mode
        state("g_markerTick", 1 if mode == "stale" else 0 if mode == "off" else 19_200_000, True)
        state("g_warm", 0 if mode == "cold" else 0x3f400000)
        state("g_cold", 0x3e800000)
        if mode in ("cold", "stale", "off"):
            state("g_blendOwner", clipper, True)
            for i in range(4):
                machine.cpu.mem_write(pool + i * 0x200 + 196, struct.pack("<f", 0.5))
        if mode == "recycled":
            machine.cpu.mem_write(pool + 32, struct.pack("<I", 0x2222))
        machine.cpu.reg_write(GPRS[0], query)
        machine.cpu.reg_write(GPRS[19], clipper)
        expected = machine.snapshot()
        machine.stop_at = BASE + machine.report["adapters"]["original_MarkerCastHook"]
        machine.run(machine.report["adapters"]["entry_MarkerCastHook"])
        assert machine.snapshot() == expected, machine.case
        for i in range(4):
            wanted = (0.25 if i & 1 else 0.75) if mode in ("warm", "recycled") else 1
            if mode == "recycled" and i == 0:
                wanted = 1
            actual = struct.unpack("<f", machine.cpu.mem_read(pool + i * 0x200 + 196, 4))[0]
            assert actual == wanted, (machine.case, i, actual)
        results.append(machine.case)

    for effect in (21, 22, 23, 24, 8):
        for warm in (False, True):
            machine.reset()
            machine.set_ready()
            machine.case = f"marker-sound-{effect}-{warm}"
            state("g_markerTick", 19_200_000, True)
            state("g_warm", 0x3f000000 if warm else 0)
            machine.cpu.reg_write(GPRS[20], effect)
            machine.cpu.reg_write(GPRS[3], 2)
            expected = machine.snapshot()
            expected[GPRS[3]] = 0 if warm and effect in (21, 23) else 2
            machine.stop_at = BASE + machine.report["adapters"]["original_MarkerSoundHook"]
            machine.run(machine.report["adapters"]["entry_MarkerSoundHook"])
            assert machine.snapshot() == expected, machine.case
            results.append(machine.case)

    emitted = []
    def emit_native(cpu, address, size, context):
        if address != BASE + 0xb57fe8:
            return
        effect, output = cpu.reg_read(GPRS[1]), cpu.reg_read(GPRS[8])
        emitted.append(effect)
        # The real routine fills 16 bytes via X8; no assumed X0/X1 aggregate return.
        cpu.mem_write(output, struct.pack("<2Q", 0x123400000000 | effect, 0xffff00ff))
        cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
    emit_hook = machine.cpu.hook_add(UC_HOOK_CODE, emit_native)
    for name, offset, first in (("MarkerRingHook", 60, 21), ("MarkerGridHook", 92, 23)):
        machine.reset()
        machine.set_ready()
        marker_fixture()
        machine.case = "marker-emit-" + name
        state("g_markerTick", 19_200_000, True)
        state("g_warm", 0x3f000000)
        for i in range(2):
            machine.cpu.mem_write(clipper + offset + i * 16, struct.pack("<Q", 0xffff00ff))
        machine.cpu.reg_write(GPRS[0], clipper)
        emitted.clear()
        machine.run(machine.report["adapters"]["entry_" + name])
        assert emitted == [first, first + 1], (machine.case, emitted)
        for i in range(2):
            actual = struct.unpack("<2Q", machine.cpu.mem_read(clipper + offset + i * 16, 16))
            assert actual == (0x123400000000 | (first + i), 0xffff00ff)
        results.append(machine.case)
    machine.cpu.hook_del(emit_hook)

    # The scene prehook must be transparent to every incoming register, not only
    # the declared C++ arguments. Native GPU work remains outside this test.
    for mode in ("uninitialized", "inactive", "stale", "sink", "preview", "return"):
        machine.reset()
        machine.case = f"scene-entry-full-context-{mode}"
        if mode != "uninitialized":
            machine.set_ready()
            state("g_ownedSerial", int(mode != "inactive"))
            state("g_swirlWanted", int(mode in ("sink", "return", "stale")))
            state("g_effectTick", 1 if mode == "stale" else 19_200_000, True)
        slot, global_object, owner = HEAP + 0x8000, HEAP + 0x9000, HEAP + 0xa000
        flags = owner + 0xca4
        for at, value in ((BASE + 0x462e188, slot), (slot, global_object), (global_object + 0x128, owner)):
            machine.cpu.mem_write(at, struct.pack("<Q", value))
        machine.cpu.mem_write(flags, struct.pack("<I", 0x24))
        machine.fixture_writes = [(flags, flags + 4)]
        machine.cpu.reg_write(GPRS[0], owner)
        for i in (1, 2, 3):
            machine.cpu.reg_write(GPRS[i], i & 1)
        # The native caller loads S0/S1, which zeros their remaining bits.
        for i in (0, 1):
            machine.cpu.reg_write(VECTORS[i], struct.unpack("<I", struct.pack("<f", 1 / 60))[0])
        before = machine.snapshot()
        machine.stop_at = BASE + machine.report["adapters"]["original_SceneUpdateHook"]
        machine.run(machine.report["adapters"]["entry_SceneUpdateHook"])
        after = machine.snapshot()
        names = {getattr(arm, f"UC_ARM64_REG_Q{i}"): f"q{i}" for i in range(32)}
        changed = [names.get(register, register) for register in before if after[register] != before[register]]
        assert not changed, (machine.case, "incoming CPU state changed", changed)
        expected = 0x8024 if mode in ("sink", "return") else 0x24
        assert struct.unpack("<I", machine.cpu.mem_read(flags, 4))[0] == expected, machine.case
        results.append(machine.case)
    def stop_at_choice(cpu, address, size, context):
        if address in {BASE + x for x in (0xbb5344, 0xbb533c, 0xbb5374, 0xbb53c0)}:
            machine.stop_at = address
            cpu.emu_stop()

    choice_hook = machine.cpu.hook_add(UC_HOOK_CODE, stop_at_choice)
    for hook, end_up, end_down, native_call in (
        ("EffectWipeChoiceHook", 0xbb5344, 0xbb533c, 0x1066da0),
        ("EffectLoopChoiceHook", 0xbb5374, 0xbb53c0, 0xfc2c48),
    ):
        for mode in ("inactive", "sink", "return", "stale"):
            for native in (0, 1):
                machine.reset()
                machine.set_ready()
                machine.case = f"{hook}-{mode}-native{native}"
                state("g_ownedSerial", int(mode != "inactive"))
                state("g_returnEffect", int(mode in ("return", "stale")))
                state("g_effectTick", 1 if mode == "stale" else 19_200_000, True)
                machine.cpu.reg_write(GPRS[24], native)
                wanted = (1 if mode == "return" else 0) if mode in ("sink", "return") else native
                machine.fake_calls[BASE + native_call] = 0x1234
                machine.stop_at = BASE + (end_up if wanted else end_down)
                machine.run(machine.report["adapters"]["entry_" + hook])
                assert machine.cpu.reg_read(GPRS[24]) == wanted, machine.case
                assert machine.stop_at == BASE + (end_up if wanted else end_down), machine.case
                results.append(machine.case)
    machine.cpu.hook_del(choice_hook)
    # Inspect the state both during and after emission. The old temporary bit
    # passed the first assertion but left native scene rendering on the wrong path.
    for mode in ("inactive", "sink", "preview", "return", "stale"):
        for native_flag in (0, 0x8000):
            machine.reset()
            machine.set_ready()
            machine.case = f"effect-flag-{mode}-native{native_flag:x}"
            state("g_ownedSerial", int(mode != "inactive"))
            state("g_swirlWanted", int(mode in ("sink", "return", "stale")))
            state("g_effectTick", 1 if mode == "stale" else 19_200_000, True)
            slot, global_object, owner = HEAP + 0x8000, HEAP + 0x9000, HEAP + 0xa000
            flags = owner + 0xca4
            for address, value in ((BASE + 0x462e188, slot), (slot, global_object), (global_object + 0x128, owner)):
                machine.cpu.mem_write(address, struct.pack("<Q", value))
            machine.cpu.mem_write(flags, struct.pack("<I", native_flag | 0x20))
            machine.fixture_writes = [(flags, flags + 4)]
            original = machine.report["adapters"]["original_EffectCalcHook"]
            machine.stop_at = BASE + original
            machine.run(machine.report["adapters"]["entry_EffectCalcHook"])
            expected = native_flag if mode in ("inactive", "stale") else (0 if mode == "preview" else 0x8000)
            assert struct.unpack("<I", machine.cpu.mem_read(flags, 4))[0] == expected | 0x20, machine.case
            machine.fake_calls[BASE + original] = 0x1234
            machine.stop_at = STOP
            machine.run(original)
            restored = expected
            assert struct.unpack("<I", machine.cpu.mem_read(flags, 4))[0] == restored | 0x20, machine.case
            assert machine.cpu.reg_read(GPRS[0]) == 0x1234, machine.case
            results.append(machine.case)
    # Both native consumers must receive their original arguments, including the
    # scene update's S1 delta time. Cover persists into the later native draw branch.
    for hook in ("EffectCalcHook", "SceneUpdateHook"):
        for ending in ("preview", "stale", "retired", "new-owner", "native-cover"):
            machine.reset()
            machine.set_ready()
            machine.case = f"cover-lifetime-{hook}-{ending}"
            slot, global_object, owner = HEAP + 0x8000, HEAP + 0x9000, HEAP + 0xa000
            flags = owner + 0xca4
            for at, value in ((BASE + 0x462e188, slot), (slot, global_object), (global_object + 0x128, owner)):
                machine.cpu.mem_write(at, struct.pack("<Q", value))
            native = 0x8000 if ending == "native-cover" else 0
            machine.cpu.mem_write(flags, struct.pack("<I", native | 0x20))
            machine.fixture_writes = [(flags, flags + 4)]
            state("g_ownedSerial", 1)
            state("g_swirlWanted", 1)
            state("g_effectTick", 19_200_000, True)
            original = machine.report["adapters"]["original_" + hook]
            entry = machine.report["adapters"]["entry_" + hook]
            before = machine.snapshot()
            machine.stop_at = BASE + original
            machine.run(entry)
            if hook == "SceneUpdateHook":
                assert machine.snapshot() == before, machine.case
            else:
                for reg in GPRS[:4]:
                    assert machine.cpu.reg_read(reg) == before[reg], (machine.case, reg)
            machine.fake_calls[BASE + original] = 0x1234
            machine.stop_at = STOP
            machine.run(original)
            assert struct.unpack("<I", machine.cpu.mem_read(flags, 4))[0] == 0x8020, machine.case
            # Execute the game's actual opaque-pass check, not a copy of its condition.
            machine.cpu.reg_write(GPRS[20], owner)
            machine.stop_at = BASE + 0x946470
            machine.run(0x946450)
            if ending == "preview":
                state("g_swirlWanted", 0)
            elif ending == "stale":
                state("g_effectTick", 1, True)
            else:
                state("g_ownedSerial", 0)
            if ending == "new-owner":
                machine.cpu.mem_write(global_object + 0x128, struct.pack("<Q", HEAP + 0xb000))
                machine.cpu.mem_write(HEAP + 0xbca4, struct.pack("<I", 0x8020))
            machine.cpu.reg_write(GPRS[30], STOP)
            machine.stop_at = STOP
            machine.run(entry)
            expected = 0x8020 if ending in ("native-cover", "new-owner") else 0x20
            assert struct.unpack("<I", machine.cpu.mem_read(flags, 4))[0] == expected, machine.case
            if ending == "new-owner":
                assert struct.unpack("<I", machine.cpu.mem_read(HEAP + 0xbca4, 4))[0] == 0x8020
            else:
                machine.cpu.reg_write(GPRS[20], owner)
                machine.stop_at = BASE + (0x946470 if expected & 0x8000 else 0x946458)
                machine.run(0x946450)
            results.append(machine.case)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"passed": len(results), "cases": results}, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(results)} real compiled effect-choice paths")


if __name__ == "__main__":
    main()
