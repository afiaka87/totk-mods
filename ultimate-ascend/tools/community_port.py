"""Relocate the pinned community patch without allocating a new text tail.

This produces virtual instructions for the padding placer, not a shippable patch.
Native contracts and the complete combined build remain separate validation gates.
"""
from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from keystone import KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, Ks

from community_profile_data import EXTRA, SITES
from native_profile_data import BINDINGS, VALIDATED_VERSIONS

SOURCE_SHA256 = "562abf3b538b1b4d189ad965bb389ab0b5b4845a27100508c0fb81a8f91439d9"
SOURCE_START, SOURCE_END = 0x2B19A50, 0x2B19FF0
VIRTUAL = 0x7000000
LITERALS = 0x7010000
CONSTANTS = (0x2B19A70, 0x2B19C78, 0x2B19D58, 0x2B19D5C,
             0x2B19F6C, 0x2B19F70, 0x2B19F74, 0x2B19FEC)
CS = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
KS = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)


def assemble(line: str, pc: int) -> int:
    data, count = KS.asm(line, pc)
    if count != 1 or data is None or len(data) != 4:
        raise ValueError(f"Expected one instruction: {line}")
    return int.from_bytes(bytes(data), "little")


def port(path: Path, version: str, image) -> dict:
    from patch_gaps.formats import parse_pchtxt

    if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Unsupported compatibility source")
    _, writes = parse_pchtxt(path.read_text())
    words = {w.off + i: struct.unpack_from("<I", w.data, i)[0]
             for w in writes if w.enabled for i in range(0, len(w.data), 4)}
    native = {a: row[version] for a, row in SITES.items()}
    # Static hooks replace one word. Resume immediately after it, even when the next analogous 1.2.1 instruction was scheduled later in a new build.
    for source in (0xe50444,0x175c3f0,0x175ca00,0x175cc24,0x175d234,
                   0x1aee240,0x1c6448c,0x1c644d0,0x1c64628,0x1c64b70):
        if native[source+4] != native[source]+4:
            raise ValueError(f'Compatibility continuation skips native instructions: {source:x}')
    extra = EXTRA[version]
    newer = version.startswith("1.4.")
    lines: dict[int, list[str]] = {}
    literal_addresses = {a: LITERALS + i * 4 for i, a in enumerate(CONSTANTS)}
    # Each original address remains a label, including replaced instructions. Never execute the source's unused NOP reservation as code.
    for pc in range(SOURCE_START, SOURCE_END, 4):
        if pc in CONSTANTS or 0x2B19E04 <= pc < 0x2B19E70:
            continue
        insn = next(CS.disasm(struct.pack("<I", words[pc]), pc))
        operands = insn.op_str
        if insn.mnemonic in ("b", "bl", "cbz", "cbnz", "tbz", "tbnz") or insn.mnemonic.startswith("b."):
            target = int(operands.rsplit("#", 1)[1], 0)
            operands = re.sub(r"#[^,]+$", f"@{target:x}", operands)
        elif insn.mnemonic == "ldr" and "[" not in operands:
            target = int(operands.rsplit("#", 1)[1], 0)
            operands = re.sub(r"#[^,]+$", f"#{literal_addresses[target]:#x}", operands)
        lines[pc] = [f"{insn.mnemonic} {operands}"]

    # Replay the displaced native instructions, whose registers/globals can move.
    for pc, source in ((0x2B19D34, 0x1C644D0), (0x2B19D60, 0x1C64628),
                       (0x2B19DD8, 0x1C64B70), (0x2B19F78, 0x1AEE240),
                       (0x2B19FE4, 0x6944A8)):
        at = native[source]
        insn = next(CS.disasm(struct.pack("<I", image.word(at)), at))
        lines[pc] = [f"{insn.mnemonic} {insn.op_str}"]
    lines[0x2B19DA4] = [f"ldr s2, [x22, #{BINDINGS['actor::kPosition'][version] + 4}]"]
    lines[0x2B19ED4] = [f"mov w8, #{BINDINGS['player::kReturnTarget'][version]}"]
    lines[0x2B19FDC] = [f"mov w{21 if newer else 22}, #0x2c"]
    for pc, slot in ((0x2B19EA0, native[0x462FF90]), (0x2B19F8C, native[0x462FEC8])):
        lines[pc] = [f"adrp x8, #{slot & ~0xfff:#x}"]
        lines[pc + 4] = [f"ldr x8, [x8, #{slot & 0xfff}]"]
    if newer:
        for pc in (0x2B19D64, 0x2B19D74):
            lines[pc] = [line.replace("[x19,", "[x20,") for line in lines[pc]]
        # The native prologue and speed register changed independently in 1.4.1.
        entry = extra["speedEntry"]
        prologue = [f"{i.mnemonic} {i.op_str}" for i in CS.disasm(image.data[entry:entry + 20], entry)]
        if version == "1.4.1":
            prologue += ["fmov s8, s0", "ldr x8, [x0, #0x18]", "ldr x8, [x8, #0x228]"]
        else:
            prologue += ["fmov s5, #-1.0", "ldp s3, s2, [x1]", "fmul s6, s2, s2",
                         "fmadd s4, s3, s3, s6", "ldr x8, [x0, #0x18]", "ldr x8, [x8, #0x228]"]
        lines[0x2B19DE0] = prologue + [f"b #{native[0xBF8BD0]:#x}"]
        for pc in range(0x2B19DE4, 0x2B19DFC, 4):
            del lines[pc]
        # This old point-readiness helper is folded into newer native callers.
        point = 0x8000000
        native[0x1956E08] = point
        slot = extra["terrainModuleSlot"]
        lines[point] = ["stp x19, x30, [sp, #-16]!", "mov x2, x0",
                       f"adrp x19, #{slot & ~0xfff:#x}", f"ldr x19, [x19, #{slot & 0xfff}]",
                       "ldr x0, [x19]", "mov w1, wzr", "mov w3, #1",
                       f"bl #{extra['terrainPoint']:#x}", "ldr x0, [x19]", "mov w1, wzr",
                       "mov w2, #16", "mov w3, #16", f"bl #{extra['terrainReady']:#x}",
                       "ldp x19, x30, [sp], #16", "ret"]

    labels, cursor = {}, VIRTUAL
    for pc, body in lines.items():
        labels[pc] = cursor
        cursor += len(body) * 4
    def destination(match):
        address = int(match[1], 16)
        if address in labels:
            return f"#{labels[address]:#x}"
        target = native.get(address)
        if target in labels:
            target = labels[target]
        if target is None:
            raise ValueError(f"Unmapped compatibility destination {address:x}")
        return f"#{target:#x}"
    instructions = []
    for source, body in lines.items():
        pc = labels[source]
        for index, line in enumerate(body):
            at = pc + index * 4
            instructions.append((at, assemble(re.sub(r"@([0-9a-f]+)", destination, line), at), "community"))
    hooks = []
    for source, word in words.items():
        if source >= SOURCE_START:
            continue
        at = native[source]
        insn = next(CS.disasm(struct.pack("<I", word), source))
        line = f"{insn.mnemonic} {insn.op_str}"
        if insn.mnemonic in ("b", "bl"):
            target = int(insn.op_str.lstrip("#"), 0)
            line = f"{insn.mnemonic} #{labels[target]:#x}"
        hooks.append(dict(source=source, address=at, original=image.word(at), patched=assemble(line, at)))
    if len(hooks) != 20:
        raise ValueError("Compatibility hook inventory changed")
    verify_native_hooks(image, version, hooks)
    return dict(instructions=instructions, literals={literal_addresses[a]: words[a] for a in CONSTANTS},
                labels=labels, hooks=hooks, source_sha256=SOURCE_SHA256, version=version,
                research_only=version not in VALIDATED_VERSIONS)


def verify_native_hooks(image, version, hooks):
    from totk_version_port.images import MainImage
    from totk_version_port import arm64
    reference = MainImage('1.2.1')
    calls = {0x175c260:0x175c99c,0x175c2b8:0x175d0b4,0x1c57754:0x1c57848}
    floats = {0xe50444,0x175c3f0,0x175d234,0x1c64628,0x1c64ba0}
    for hook in hooks:
        source, at, word = hook['source'],hook['address'],hook['original']
        original = reference.word(source)
        if source in calls:
            valid = arm64.is_bl(word) and arm64.branch_target(word,at)==SITES[calls[source]][version]
        elif source in floats:
            valid = word & 0xffc003ff == original & 0xffc003ff
        elif source == 0x1c64b70:
            valid = arm64.adrp_page(word,at) is not None and word & 31 == 19
        elif source == 0x1c64bbc:
            valid = word & 0xfc000000 == 0x14000000
            if version.startswith('1.4.'):
                valid &= arm64.branch_target(word,at)==EXTRA[version]['speedEntry']
            else:
                valid &= arm64.branch_target(word,at)+0x30==SITES[0xbf8bd0][version]
        elif source == 0x6944a8 and version.startswith('1.4.'):
            valid = word == 0xf9400e74
        else:
            valid = word == original
        if not valid:
            raise ValueError(f'Native compatibility hook contract differs: {version} {source:x}')
    floor = SITES[0x175d0b4][version]
    cache = [pc for pc in range(floor,floor+0x200,4) if image.word(pc)==0x5284a108]
    if len(cache)!=1:
        raise ValueError('Native cached ceiling query is no longer player+2508')
