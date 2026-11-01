"""Audit native ADRP uses near the proposed private-state anchor."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from capstone.arm64_const import ARM64_OP_IMM, ARM64_OP_MEM, ARM64_OP_REG


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.tool_root / "scripts"))
    from patch_gaps.image import Image
    image = Image.load()
    anchor, end = 0x472a2b0, 0x472a2c0
    words = np.frombuffer(image.data, dtype="<u4", count=image.text_end // 4).astype(np.int64)
    indices = np.flatnonzero((words & 0x9f000000) == 0x90000000)
    instructions = words[indices]
    immediate = (((instructions >> 5) & 0x7ffff) << 2) | ((instructions >> 29) & 3)
    immediate = np.where(immediate & 0x100000, immediate - 0x200000, immediate)
    pages = ((indices * 4) & ~0xfff) + (immediate << 12)
    selected = (pages >= (anchor & ~0xfff) - 0x8000) & (pages <= anchor & ~0xfff)
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    decoder.detail = True
    accesses, escapes, unknown, final_page_sites = [], [], [], []

    def register_name(instruction, register):
        name = instruction.reg_name(register)
        return "x" + name[1:] if name.startswith("w") else name

    for index, page in zip(indices[selected].tolist(), pages[selected].tolist(), strict=True):
        pc = index * 4
        initial = next(decoder.disasm(image.data[pc:pc + 4], pc))
        values = {initial.op_str.split(",")[0]: page}
        observed = False
        if page == anchor & ~0xfff:
            final_page_sites.append(pc)
        for instruction in decoder.disasm(image.data[pc + 4:pc + 4 + 256], pc + 4):
            updates = {}
            for operand in instruction.operands:
                if operand.type != ARM64_OP_MEM:
                    continue
                base = values.get(register_name(instruction, operand.mem.base))
                if base is None:
                    continue
                observed = True
                if operand.mem.index:
                    unknown.append({"site": hex(pc), "use": hex(instruction.address), "reason": "indexed memory"})
                    continue
                address = base + operand.mem.disp
                first_register = instruction.reg_name(instruction.operands[0].reg)
                width = {"b": 1, "h": 2, "s": 4, "w": 4, "d": 8, "x": 8, "q": 16}.get(first_register[:1], 16)
                if instruction.mnemonic.endswith("b"):
                    width = 1
                elif instruction.mnemonic.endswith("h"):
                    width = 2
                if instruction.mnemonic in ("stp", "ldp", "stnp", "ldnp"):
                    width *= 2
                accesses.append({"site": hex(pc), "use": hex(instruction.address), "address": address, "size": width})
                if address < end and anchor < address + width:
                    raise ValueError(f"Native access overlaps anchor at {instruction.address:x}")
            operands = instruction.operands
            if instruction.mnemonic in ("add", "sub") and len(operands) == 3 and operands[2].type == ARM64_OP_IMM:
                source = values.get(register_name(instruction, operands[1].reg))
                if source is not None:
                    shift = operands[2].shift.value
                    delta = operands[2].imm << shift
                    address = source + (delta if instruction.mnemonic == "add" else -delta)
                    updates[register_name(instruction, operands[0].reg)] = address
                    observed = True
                    if anchor <= address < end:
                        raise ValueError(f"Native address formation overlaps anchor at {instruction.address:x}")
            elif instruction.mnemonic == "mov" and len(operands) == 2 and operands[1].type == ARM64_OP_REG:
                source = values.get(register_name(instruction, operands[1].reg))
                if source is not None:
                    updates[register_name(instruction, operands[0].reg)] = source
            if instruction.mnemonic in ("bl", "blr"):
                for name, value in list(values.items()):
                    if name in {f"x{i}" for i in range(8)}:
                        escapes.append({"site": hex(pc), "call": hex(instruction.address), "argument": name, "address": value})
                    if name in {f"x{i}" for i in range(19)}:
                        values.pop(name, None)
            for register in instruction.regs_access()[1]:
                values.pop(register_name(instruction, register), None)
            values.update(updates)
            if not values or instruction.mnemonic in ("ret", "b", "br"):
                break
        if not observed:
            unknown.append({"site": hex(pc), "reason": "no direct use before block boundary"})
    final_accesses = [entry for entry in accesses if anchor & ~0xfff <= entry["address"] < (anchor & ~0xfff) + 4096]
    final_unresolved = [entry for entry in unknown if int(entry["site"], 16) in final_page_sites]
    if final_unresolved:
        raise ValueError(f"Final-page native references need manual review: {final_unresolved}")
    report = {"scope": "Bounded native address-use audit; indirect callees and arbitrary pointer arithmetic are not a whole-program proof",
              "nearby_page_sites": int(np.count_nonzero(selected)), "final_page_sites": len(final_page_sites),
              "resolved_accesses": len(accesses), "final_page_accesses": final_accesses,
              "highest_final_page_access_end": hex(max((a["address"] + a["size"] for a in final_accesses), default=0)),
              "escaped_arguments": escapes, "unresolved": unknown, "final_page_unresolved": final_unresolved,
              "anchor_overlaps": 0}
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ("final_page_accesses", "escaped_arguments")}, indent=2))


if __name__ == "__main__":
    main()
