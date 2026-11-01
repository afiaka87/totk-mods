"""Execute the built patch adapters and initializer in an offline ARM64 emulator."""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

import unicorn as un
from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from unicorn import arm64_const as arm

BASE, HEAP, STACK, STOP = 0x7100000000, 0x7200000000, 0x7300000000, 0x7400000000
SDK = 0x7500000000
GPRS = [getattr(arm, f"UC_ARM64_REG_X{i}") for i in range(31)]
VECTORS = [getattr(arm, f"UC_ARM64_REG_Q{i}") for i in range(32)]
STATUS = [arm.UC_ARM64_REG_NZCV, arm.UC_ARM64_REG_FPCR, arm.UC_ARM64_REG_FPSR]


class Machine:
    def __init__(self, layout: Path, tool_root: Path):
        sys.path.insert(0, str(tool_root / "scripts"))
        from patch_gaps.image import Image
        self.report = json.loads((layout / "report.json").read_text())
        clean = Image.load()
        self.memory = bytearray(clean.data.ljust(0x472b000, b"\0"))
        for address, word in struct.iter_unpack("<II", (layout / "placement.words").read_bytes()):
            struct.pack_into("<I", self.memory, address, word)
        self.cpu = un.Uc(un.UC_ARCH_ARM64, un.UC_MODE_ARM)
        self.cpu.mem_map(BASE, (max(len(self.memory), 0x472b000) + 0xfff) & ~0xfff)
        self.cpu.mem_write(BASE, bytes(self.memory))
        self.cpu.mem_map(HEAP, 0x10000)
        self.cpu.mem_map(STACK, 0x20000)
        self.cpu.mem_map(STOP, 0x1000)
        self.anchor = BASE + int(self.report["anchor"], 16)
        self.data = (layout / "private-data.bin").read_bytes()
        self.services = {BASE + self.report["native_services"][name]: name for name in (
            "patch_allocate_region", "patch_allocate_pages", "patch_free_region", "patch_free_pages",
            "memset", "memcpy", "memmove", "memcmp", "vsnprintf", "svcGetSystemTick")}
        self.native_debug = self.report.get("native_debug")
        if self.native_debug:
            debug = self.native_debug
            self.cpu.mem_map(SDK, 0x200000)
            self.cpu.mem_write(SDK + debug["debug_export"], struct.pack("<2I", *debug["debug_words"]))
            self.cpu.mem_write(BASE + debug["tick_got"], struct.pack("<Q", SDK + debug["tick_export"]))
            self.cpu.hook_add(un.UC_HOOK_INTR, self.debug_interrupt)
        else:
            self.services[BASE + self.report["native_services"]["patch_print"]] = "patch_print"
        self.cpu.hook_add(un.UC_HOOK_CODE, self.instruction)
        self.cpu.hook_add(un.UC_HOOK_MEM_WRITE, self.write)
        self.case = ""
        self.reset()

    def reset(self):
        self.cpu.mem_write(self.anchor, bytes(16))
        self.cpu.mem_write(HEAP, bytes([0xcc]) * 0x10000)
        self.cpu.mem_write(STACK, bytes(0x20000))
        for i, register in enumerate(GPRS):
            self.cpu.reg_write(register, 0x100000000 + i * 0x10101)
        for i, register in enumerate(VECTORS):
            self.cpu.reg_write(register, 0x112233445566778899aabbccddeeff00 ^ (i * 0x101010101))
        self.cpu.reg_write(arm.UC_ARM64_REG_SP, STACK + 0x1f000)
        self.cpu.reg_write(arm.UC_ARM64_REG_X30, STOP)
        for register, value in zip(STATUS, (0xa0000000, 0x400000, 0x9), strict=True):
            self.cpu.reg_write(register, value)
        self.events, self.logs, self.writes = [], [], []
        self.stop_at = None
        self.callback = None
        self.callback_edits = {}
        self.callback_seen = 0
        self.fail_reserve = self.fail_map = False
        self.format_error = False
        self.fake_calls = {}
        self.fixture_writes = []

    def debug_interrupt(self, cpu, interrupt, context):
        assert interrupt == 2 and cpu.reg_read(arm.UC_ARM64_REG_PC) == SDK + self.native_debug["debug_export"] + 4
        pointer, length = cpu.reg_read(GPRS[0]), cpu.reg_read(GPRS[1])
        assert 0 < length <= 511
        self.logs.append(bytes(cpu.mem_read(pointer, length)).decode())
        cpu.reg_write(GPRS[0], 0)

    def snapshot(self):
        registers = GPRS + VECTORS + STATUS + [arm.UC_ARM64_REG_SP]
        return {register: self.cpu.reg_read(register) for register in registers}

    def set_ready(self):
        self.cpu.mem_write(HEAP, self.data)
        self.cpu.mem_write(HEAP + self.report["main_base_offset"], struct.pack("<Q", BASE))
        self.cpu.mem_write(self.anchor, struct.pack("<QII", HEAP, 2, 0))

    def read_string(self, address, limit=4096):
        value = bytes(self.cpu.mem_read(address, limit)).split(b"\0", 1)[0]
        return value.decode("utf-8")

    def write(self, cpu, access, address, size, value, context):
        allowed = (STACK <= address and address + size <= STACK + 0x20000) or (
            HEAP <= address and address + size <= HEAP + 8192) or (
            self.anchor <= address and address + size <= self.anchor + 16) or any(
                start <= address and address + size <= end for start, end in self.fixture_writes)
        if not allowed:
            raise AssertionError(f"{self.case}: unexpected guest write {address:x}+{size}")
        self.writes.append((address, size))

    def instruction(self, cpu, address, size, context):
        if address == self.stop_at:
            cpu.emu_stop()
            return
        if address == self.callback:
            self.callback_seen += 1
            pointer = cpu.reg_read(GPRS[0])
            actual = struct.unpack("<31Q", bytes(cpu.mem_read(pointer, 248)))
            assert actual == tuple(self.expected[register] for register in GPRS), "Incorrect inline context"
            for index, value in self.callback_edits.items():
                cpu.mem_write(pointer + index * 8, struct.pack("<Q", value))
            return_pc = cpu.reg_read(GPRS[30])
            for register in GPRS:
                cpu.reg_write(register, 0xdeadcafe)
            for register in VECTORS:
                cpu.reg_write(register, (1 << 128) - 1)
            for register, value in zip(STATUS, (0x50000000, 0x800000, 0x12), strict=True):
                cpu.reg_write(register, value)
            cpu.reg_write(arm.UC_ARM64_REG_PC, return_pc)
            return
        if address in self.fake_calls:
            cpu.reg_write(GPRS[0], self.fake_calls[address])
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
            return
        name = self.services.get(address)
        if name is None:
            return
        args = [cpu.reg_read(register) for register in GPRS[:4]]
        return_pc = cpu.reg_read(GPRS[30])
        result = 0
        if name == "patch_allocate_region":
            assert args[1] == 0x10000
            self.events.append("reserve")
            result = 0x123 if self.fail_reserve else 0
            if not result:
                cpu.mem_write(args[0], struct.pack("<Q", HEAP))
        elif name == "patch_allocate_pages":
            assert args[:2] == [HEAP, 8192]
            self.events.append("map")
            result = 0x456 if self.fail_map else 0
        elif name == "patch_free_region":
            assert args[0] == HEAP
            self.events.append("free-region")
        elif name == "patch_free_pages":
            assert args[:2] == [HEAP, 8192]
            self.events.append("free-pages")
        elif name == "memset":
            assert (HEAP <= args[0] and args[0] + args[2] <= HEAP + 8192) or (
                STACK <= args[0] and args[0] + args[2] <= STACK + 0x20000)
            cpu.mem_write(args[0], bytes([args[1] & 255]) * args[2])
            result = args[0]
        elif name in ("memcpy", "memmove"):
            cpu.mem_write(args[0], bytes(cpu.mem_read(args[1], args[2])))
            result = args[0]
        elif name == "memcmp":
            a, b = bytes(cpu.mem_read(args[0], args[2])), bytes(cpu.mem_read(args[1], args[2]))
            result = (a > b) - (a < b)
        elif name == "svcGetSystemTick":
            result = 19_200_000
        elif name == "vsnprintf":
            if self.format_error:
                result = -1
            else:
                format_text = self.read_string(args[2], 512)
                stack, gp_top, _fp_top, gp_offset, _fp_offset = struct.unpack("<QQQii", bytes(cpu.mem_read(args[3], 32)))

                def replace(match):
                    nonlocal stack, gp_offset
                    if gp_offset < 0:
                        value = struct.unpack("<Q", bytes(cpu.mem_read(gp_top + gp_offset, 8)))[0]
                        gp_offset += 8
                    else:
                        value = struct.unpack("<Q", bytes(cpu.mem_read(stack, 8)))[0]
                        stack += 8
                    kind = match["kind"]
                    if kind == "s":
                        return self.read_string(value)
                    if not match["length"]:
                        value &= 0xffffffff
                    if kind in ("d", "i") and not match["length"] and value >= 0x80000000:
                        value -= 0x100000000
                    if kind == "x":
                        return format(value, "0" + (match["width"] or "0") + "x")
                    if kind in ("u", "d", "i"):
                        return str(value)
                    raise AssertionError("Unexpected format conversion")

                text = re.sub(r"%(?P<width>0?\d*)(?P<length>ll|l|z)?(?P<kind>[udixs])", replace, format_text)
                encoded = text.encode()
                cpu.mem_write(args[0], encoded[:args[1] - 1] + b"\0")
                result = len(encoded)
        elif name == "patch_print":
            pointer, length = struct.unpack("<Qi", bytes(cpu.mem_read(args[0], 12)))
            assert 0 < length <= 511
            assert bytes(cpu.mem_read(args[0] + 12, 20)) == bytes(20)
            self.logs.append(bytes(cpu.mem_read(pointer, length)).decode())
        cpu.reg_write(GPRS[0], result & ((1 << 64) - 1))
        cpu.reg_write(arm.UC_ARM64_REG_PC, return_pc)

    def run(self, address):
        try:
            self.cpu.emu_start(BASE + address, STOP, count=2_000_000)
        except un.UcError as error:
            raise AssertionError(f"{self.case}: {error}; pc={self.cpu.reg_read(arm.UC_ARM64_REG_PC):x}") from error
        pc = self.cpu.reg_read(arm.UC_ARM64_REG_PC)
        assert pc in (STOP, self.stop_at), f"{self.case}: instruction budget exhausted at {pc:x}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    machine = Machine(args.layout, args.tool_root)
    hooks = json.loads(args.inputs.read_text())["hooks"]
    results = []
    for hook in hooks:
        for ready in (False, True):
            machine.reset()
            machine.case = f"{hook['name']}-{'ready' if ready else 'pending'}"
            if ready:
                machine.set_ready()
            else:
                machine.cpu.mem_write(machine.anchor + 8, struct.pack("<I", 1))
            machine.expected = machine.snapshot()
            name = hook["name"]
            machine.stop_at = BASE + machine.report["adapters"]["original_" + name]
            if ready and hook["kind"] == "INLINE":
                machine.callback = BASE + machine.report["callbacks"][name]
                machine.callback_edits = {2: 0x234567890, 16: 0x234567816, 17: 0x234567817,
                                          24: 0x345678901, 30: 0x234567830}
            elif ready:
                machine.stop_at = BASE + machine.report["callbacks"][name]
            machine.run(machine.report["adapters"]["entry_" + name])
            expected = dict(machine.expected)
            if ready and hook["kind"] == "INLINE":
                expected.update({GPRS[index]: value for index, value in machine.callback_edits.items()})
                assert machine.callback_seen == 1
            assert machine.snapshot() == expected, f"{machine.case}: register preservation failed"
            results.append(machine.case)

    initialize = machine.report["runtime_symbols"]["patch_initialize"]
    for case in ("success", "reserve-failed", "map-failed", "busy", "failed-latch", "checksum-failed", "guard-failed", "bad-metadata", "cyclic-data"):
        machine.reset()
        machine.case = case
        repairs = []

        def change(address, data, repairs=repairs):
            repairs.append((address, bytes(machine.cpu.mem_read(address, len(data)))))
            machine.cpu.mem_write(address, data)

        machine.fail_reserve = case == "reserve-failed"
        machine.fail_map = case == "map-failed"
        head = BASE + machine.report["data_chain_head"]
        second = BASE + struct.unpack("<I", bytes(machine.cpu.mem_read(head, 4)))[0]
        third = BASE + struct.unpack("<I", bytes(machine.cpu.mem_read(second, 4)))[0]
        first_data = BASE + struct.unpack("<I", bytes(machine.cpu.mem_read(third, 4)))[0]
        if case == "busy":
            change(machine.anchor + 8, struct.pack("<I", 1))
        elif case == "failed-latch":
            change(machine.anchor + 8, struct.pack("<I", 3))
        elif case == "checksum-failed":
            change(first_data + 4, b"\xab\xcd\xef\x12")
        elif case == "guard-failed":
            change(BASE + 0x175c194 + 4, bytes(4))
        elif case == "bad-metadata":
            change(head + 4, struct.pack("<I", 8193))
        elif case == "cyclic-data":
            change(first_data, struct.pack("<I", first_data - BASE))
        machine.run(initialize)
        pointer, phase, failure = struct.unpack("<QII", bytes(machine.cpu.mem_read(machine.anchor, 16)))
        if case == "success":
            assert (pointer, phase, failure) == (HEAP, 2, 0)
            expected = bytearray(machine.data)
            struct.pack_into("<Q", expected, machine.report["main_base_offset"], BASE)
            assert bytes(machine.cpu.mem_read(HEAP, len(expected))) == expected
            assert machine.events == ["reserve", "map"]
            assert any(re.search(r"v\d+\.\d+\.\d+ release installed", message) for message in machine.logs), machine.logs
            assert any(f"status=0 code={len(machine.data):08x}" in message for message in machine.logs), machine.logs
            events = list(machine.events)
            machine.cpu.reg_write(GPRS[30], STOP)
            machine.run(initialize)
            assert machine.events == events
        elif case in ("busy", "failed-latch"):
            assert pointer == 0 and not machine.events and not machine.logs
        else:
            code = {"reserve-failed": 2, "map-failed": 3, "checksum-failed": 5,
                    "guard-failed": 6, "bad-metadata": 1, "cyclic-data": 4}[case]
            assert (pointer, phase, failure) == (0, 3, code), (case, pointer, phase, failure, machine.logs)
            assert any(f"status={code}" in message for message in machine.logs), machine.logs
            if case in ("checksum-failed", "guard-failed", "cyclic-data"):
                assert machine.events == ["reserve", "map", "free-pages", "free-region"]
            elif case == "map-failed":
                assert machine.events == ["reserve", "map", "free-region"]
        for address, data in repairs:
            machine.cpu.mem_write(address, data)
        results.append(case)

    for sequence in machine.report["private_address_sequences"]:
        machine.reset()
        machine.case = f"private-address-{sequence['entry']:x}"
        machine.set_ready()
        before = machine.snapshot()
        machine.stop_at = BASE + sequence["continuation"]
        machine.run(sequence["entry"])
        before[GPRS[sequence["register"]]] = HEAP + sequence["offset"]
        assert machine.snapshot() == before, machine.case
        results.append(machine.case)

    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    original_words = {hook["address"] + index * 4: word for hook in hooks
                      for index, word in enumerate(hook["original_words"])}
    replay_at = {item["native_pc"]: item["entry"] for item in machine.report["replay_instructions"]}
    for instruction in machine.report["replay_instructions"]:
        pc = instruction["native_pc"]
        word = original_words[pc]
        decoded = next(decoder.disasm(struct.pack("<I", word), BASE + pc))
        conditional = decoded.mnemonic.startswith("b.") or decoded.mnemonic in ("cbz", "cbnz", "tbz", "tbnz")
        variants = range(16) if decoded.mnemonic.startswith("b.") else range(2 if conditional else 1)
        outcomes = set()
        for variant in variants:
            machine.reset()
            machine.case = f"original-{pc:x}-{variant}"
            for index, register in enumerate(GPRS[:-1]):
                machine.cpu.reg_write(register, HEAP + 0x800 + index * 16)
            if decoded.mnemonic.startswith("b."):
                machine.cpu.reg_write(arm.UC_ARM64_REG_NZCV, variant << 28)
            elif conditional:
                register_index = int(re.search(r"[wx](\d+)", decoded.op_str)[1])
                machine.cpu.reg_write(GPRS[register_index], 0 if variant == 0 else (1 << 64) - 1)
            initial = machine.snapshot()
            patched = bytes(machine.cpu.mem_read(BASE + pc, 4))
            machine.cpu.mem_write(BASE + pc, struct.pack("<I", word))
            machine.cpu.ctl_remove_cache(BASE + pc, BASE + pc + 4)
            machine.cpu.emu_start(BASE + pc, STOP, count=1)
            native_next = machine.cpu.reg_read(arm.UC_ARM64_REG_PC)
            call_target = None
            if decoded.mnemonic in ("bl", "blr"):
                call_target = native_next
                machine.cpu.reg_write(GPRS[0], 0x1234)
                native_next = machine.cpu.reg_read(GPRS[30])
            expected = machine.snapshot()
            heap_after = bytes(machine.cpu.mem_read(HEAP, 8192))
            stack_after = bytes(machine.cpu.mem_read(STACK, 0x20000))
            machine.cpu.mem_write(BASE + pc, patched)
            machine.cpu.ctl_remove_cache(BASE + pc, BASE + pc + 4)
            machine.cpu.mem_write(HEAP, bytes([0xcc]) * 0x10000)
            machine.cpu.mem_write(STACK, bytes(0x20000))
            for register, value in initial.items():
                machine.cpu.reg_write(register, value)
            if call_target is not None:
                machine.fake_calls[call_target] = 0x1234
            outcomes.add(native_next)
            if native_next == BASE + pc + 4:
                machine.stop_at = BASE + instruction["continuation"]
            elif native_next - BASE in replay_at:
                machine.stop_at = BASE + replay_at[native_next - BASE]
            else:
                machine.stop_at = native_next
            machine.run(instruction["entry"])
            assert machine.snapshot() == expected, f"{machine.case}: replay changed registers"
            assert bytes(machine.cpu.mem_read(HEAP, 8192)) == heap_after, machine.case
            assert bytes(machine.cpu.mem_read(STACK, 0x20000)) == stack_after, machine.case
            results.append(machine.case)
        if conditional:
            assert len(outcomes) == 2, f"Conditional fixture did not exercise both exits at {pc:x}"

    for fail in (False, True):
        machine.reset()
        machine.case = "first-input-map-failure" if fail else "first-input-initialization"
        machine.fail_map = fail
        before = machine.snapshot()
        machine.stop_at = BASE + (machine.report["adapters"]["original_NpadCalcHook"] if fail
                                  else machine.report["callbacks"]["NpadCalcHook"])
        machine.run(machine.report["adapters"]["entry_NpadCalcHook"])
        assert machine.snapshot() == before, machine.case
        results.append(machine.case)

    logger = next(value for name, value in machine.report["runtime_symbols"].items() if "PatchLogger3Log" in name)
    for bad_format in (False, True):
        machine.reset()
        machine.case = "logger-format-error" if bad_format else "logger-truncation"
        machine.format_error = bad_format
        machine.cpu.mem_write(STACK + 0x10000, b"%s\0")
        machine.cpu.mem_write(STACK + 0x11000, b"x" * 700 + b"\0")
        machine.cpu.reg_write(GPRS[0], STACK + 0x10000)
        machine.cpu.reg_write(GPRS[1], STACK + 0x11000)
        machine.run(logger)
        assert machine.logs == ([] if bad_format else ["x" * 511]), machine.logs
        results.append(machine.case)
    args.out.mkdir(parents=True, exist_ok=True)
    result = {"passed": len(results), "cases": results, "scope": "Actual placed ARM64; native OS/format/log calls mocked; no game boot"}
    (args.out / "runtime-tests.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": len(results), "scope": result["scope"]}, indent=2))


if __name__ == "__main__":
    main()
