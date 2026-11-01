"""Static ARM64 entry adapters. All addresses here are image-relative."""
from __future__ import annotations

import re
import struct


def save_context() -> list[str]:
    lines = ["sub sp, sp, #0x320"]
    lines += [f"stp x{i}, x{i + 1}, [sp, #{i * 8}]" for i in range(0, 30, 2)]
    lines += ["str x30, [sp, #0xf0]"]
    for system, offset in (("nzcv", 0xf8), ("fpcr", 0x100), ("fpsr", 0x108)):
        lines += [f"mrs x0, {system}", f"str x0, [sp, #{offset}]"]
    lines += [f"stp q{i}, q{i + 1}, [sp, #{0x120 + i * 16}]" for i in range(0, 32, 2)]
    return lines


def restore_context() -> list[str]:
    lines = [f"ldp q{i}, q{i + 1}, [sp, #{0x120 + i * 16}]" for i in range(0, 32, 2)]
    for system, offset in (("fpcr", 0x100), ("fpsr", 0x108), ("nzcv", 0xf8)):
        lines += [f"ldr x0, [sp, #{offset}]", f"msr {system}, x0"]
    lines += [f"ldp x{i}, x{i + 1}, [sp, #{i * 8}]" for i in range(0, 30, 2)]
    return lines + ["ldr x30, [sp, #0xf0]", "add sp, sp, #0x320"]


def adapters(start: int, hooks: list[dict], callbacks: dict[str, int], initialize: int, anchor: int,
             assemble, native_debug: dict) -> tuple[list[tuple[str, int, bytes]], dict[str, int], dict[int, tuple[int, int, int]]]:
    blocks: list[tuple[str, list[str]]] = []
    for hook in hooks:
        blocks.append(("original_" + hook["name"], ["nop"] * 6))
    blocks.append(("patch_main_base", ["adrp x0, #0", "ret"]))
    got, delta = native_debug["tick_got"], native_debug["debug_from_tick"]
    if not 0 <= delta < 0x1000000 or got & 7:
        raise ValueError("Native debug adapter displacement/alignment is unsupported")
    blocks.append(("patch_debug_print", [f"adrp x16, #{got & ~0xfff:#x}",
        f"ldr x16, [x16, #{got & 0xfff}]", f"add x16, x16, #{delta >> 12}, lsl #12",
        f"add x16, x16, #{delta & 0xfff}", "br x16"]))
    # Share the full-state machinery. The outer pair carries native x16/x30 across
    # the dispatch branches; callback edits to either register are copied back.
    blocks.append(("inline_save", save_context() + ["mov x16, x30",
        "ldr x0, [sp, #0x328]", "str x0, [sp, #0xf0]", "mov x0, sp", "br x16"]))
    restore = [line for line in restore_context() if line not in (
        "ldp x16, x17, [sp, #128]", "ldr x30, [sp, #0xf0]", "add sp, sp, #0x320")]
    blocks.append(("inline_restore", ["str x30, [sp, #0x110]", "ldr x0, [sp, #0x80]",
        "ldr x1, [sp, #0xf0]", "str x0, [sp, #0x320]", "str x1, [sp, #0x328]"] + restore +
        ["ldr x17, [sp, #0x88]", "ldr x16, [sp, #0x110]", "add sp, sp, #0x320", "br x16"]))
    for hook in hooks:
        name = hook["name"]
        if hook["kind"] == "INLINE":
            blocks.append(("inline_" + name, ["stp x16, x30, [sp, #-16]!", "bl @inline_save",
                f"bl #{callbacks[name]:#x}", "bl @inline_restore", "ldp x16, x30, [sp], #16",
                f"b @original_{name}"]))
        destination = f"@inline_{name}" if hook["kind"] == "INLINE" else f"#{callbacks[name]:#x}"
        lines = ["stp x16, x17, [sp, #-16]!", f"adrp x16, #{anchor & ~0xfff:#x}",
                 f"add x16, x16, #{anchor & 0xfff}", "ldar x17, [x16]"]
        if name == "NpadCalcHook":
            lines += ["cbnz x17, ready", "ldr w17, [x16, #8]", "cbnz w17, vanilla",
                      "ldp x16, x17, [sp], #16", "b @initialize_entry", "ready:",
                      "ldp x16, x17, [sp], #16", f"b {destination}", "vanilla:",
                      "ldp x16, x17, [sp], #16", f"b @original_{name}"]
        else:
            lines += ["cbz x17, vanilla", "ldp x16, x17, [sp], #16", f"b {destination}",
                      "vanilla:", "ldp x16, x17, [sp], #16", f"b @original_{name}"]
        blocks.append(("entry_" + name, lines))
    blocks.append(("initialize_entry", save_context() + [f"bl #{initialize:#x}"] + restore_context()
                   + ["b @entry_NpadCalcHook"]))
    addresses = {}
    cursor = start
    for name, lines in blocks:
        addresses[name] = cursor
        cursor += 4 * sum(not line.endswith(":") for line in lines)
    result = []
    replay = {}
    for name, lines in blocks:
        address = addresses[name]
        if name.startswith("original_"):
            hook = next(h for h in hooks if name == "original_" + h["name"])
            words = hook["original_words"]
            if len(words) != 5:
                raise ValueError("Original-code adapters must preserve 20 bytes")
            data = struct.pack("<5I", *words)
            data += bytes(assemble(f"b #{hook['address'] + 20:#x}", address + 20)[0])
            for offset in range(0, 20, 4):
                replay[address + offset] = (hook["address"], address, 20)
        else:
            source = "\n".join(lines)
            source = re.sub(r"@(\w+)", lambda m: f"#{addresses[m[1]]:#x}", source)
            data = bytes(assemble(source, address)[0])
        expected_size = 4 * sum(not line.endswith(":") for line in lines)
        if len(data) != expected_size:
            raise ValueError(f"Assembler changed adapter size: {name}")
        result.append((name, address, data))
    return result, addresses, replay
