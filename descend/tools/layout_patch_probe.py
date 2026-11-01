"""Place and verify all payload instructions; --complete emits the full patch runtime.

Without --complete, retain the historical capacity probe with an adapter reserve.
The complete build requires test_patch_runtime.py before packaging.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from keystone import KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, Ks
from patch_stubs import adapters

CS = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
KS = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)
CODE = 0x5000000
DATA = 0x6000000
ANCHOR = 0x472A2B0


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & -boundary


def encode(text: str, pc: int) -> int:
    values, count = KS.asm(text, pc)
    if count != 1 or values is None or len(values) != 4:
        raise ValueError(f"Not one instruction at {pc:x}: {text}")
    return int.from_bytes(bytes(values), "little")


def decode(word: int, pc: int):
    result = list(CS.disasm(struct.pack("<I", word), pc))
    if len(result) != 1:
        raise ValueError(f"Undecodable word {word:x} at {pc:x}")
    return result[0]


def target(insn) -> int:
    return int(insn.op_str.rsplit("#", 1)[1], 0)


def branch_kind(insn) -> str | None:
    if insn.mnemonic in ("b", "bl", "cbz", "cbnz", "tbz", "tbnz") or insn.mnemonic.startswith("b."):
        return insn.mnemonic
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--complete", action="store_true", help="Place the real runtime and emit both patch formats")
    parser.add_argument("--reserve", type=int, default=6144,
                        help="Explicit effective-byte budget for unimplemented adapters")
    args = parser.parse_args()
    if args.complete:
        args.reserve = 0
    args.out.mkdir(parents=True, exist_ok=True)
    inputs = json.loads(args.inputs.read_text())
    if not int(inputs["bss"][1], 16) <= ANCHOR < ANCHOR + 16 <= int(inputs["native_rw"]["mapped_end"], 16):
        raise ValueError("Anchor lies outside the verified writable padding")
    if any(h["conflicts"] for h in inputs["hooks"]):
        raise ValueError("A hook overlaps a known patch")
    if inputs["tail_references"] or any(a < ANCHOR + 16 and ANCHOR < b for a, b, *_ in inputs["tail_claims"]):
        raise ValueError("Proposed anchor is referenced or claimed")
    gaps = {size: sorted(a for a, n in inputs["gaps"] if n == size) for size in (8, 12)}
    used_gaps = []

    def take(size: int | None = None) -> tuple[int, int]:
        size = size or (12 if gaps[12] else 8)
        if not gaps[size]:
            raise ValueError(f"Ran out of {size}-byte gaps")
        address = gaps[size].pop(0)
        used_gaps.append((address, size))
        return address, size

    # Reserve actual gap capacity before placement, not an optimistic final subtraction.
    reserved = []
    capacity = 0
    while capacity < args.reserve:
        address, size = take(12 if capacity < args.reserve // 2 else 8)
        capacity += size - 4
        reserved.append((address, size))
    external = {name: int(address, 16) for name, address in inputs["imports"].items()}
    external["svcGetSystemTick"] = external["_ZN2nn2os13GetSystemTickEv"]
    # Reserve logger service entry. It is deliberately not filled with a fake implementation.
    if not args.complete:
        external["_ZN11PatchLogger3LogEPKcz"] = reserved[0][0]
    else:
        for alias, imported in {
            "patch_allocate_region": "_ZN2nn2os21AllocateAddressRegionEPmm",
            "patch_allocate_pages": "_ZN2nn2os19AllocateMemoryPagesEmm",
            "patch_free_region": "_ZN2nn2os17FreeAddressRegionEm",
            "patch_free_pages": "_ZN2nn2os15FreeMemoryPagesEmm",
        }.items():
            external[alias] = external[imported]
        external["patch_anchor"] = ANCHOR

    static_blocks, static_addresses, replay = [], {}, {}
    symbol_values, callbacks = {}, {}
    data_chain = []

    with args.object.open("rb") as stream:
        elf = ELFFile(stream)
        sections = list(elf.iter_sections())
        symtab = elf.get_section_by_name(".symtab")
        bases = {}
        code_cursor, data_cursor = CODE, DATA
        text_sections = []
        blob = bytearray()
        bss_bytes = 0
        for index, section in enumerate(sections):
            size = section["sh_size"]
            if not size or not section["sh_flags"] & 2 or section.name in (".data.patch_callback_roots", ".data.patch_guard_root"):
                continue
            if section["sh_flags"] & 4:
                code_cursor = align(code_cursor, max(4, section["sh_addralign"]))
                bases[index] = code_cursor
                text_sections.append((index, section.name, code_cursor, bytearray(section.data())))
                code_cursor += size
            else:
                data_cursor = align(data_cursor, max(1, section["sh_addralign"]))
                bases[index] = data_cursor
                end = data_cursor - DATA + size
                blob.extend(bytes(end - len(blob)))
                if section["sh_type"] != "SHT_NOBITS":
                    blob[data_cursor - DATA:end] = section.data()
                else:
                    bss_bytes += size
                data_cursor += size
        if len(blob) > 8192:
            raise ValueError("State and constants exceed the proposed 8 KiB allocation")
        for symbol in symtab.iter_symbols():
            if symbol["st_shndx"] in bases:
                symbol_values[symbol.name] = bases[symbol["st_shndx"]] + symbol["st_value"]
        chain_blob = bytes(blob)
        if args.complete:
            guard_symbol = next(s for s in symtab.iter_symbols() if s.name.endswith("6kSitesE"))
            guard_offset = symbol_values[guard_symbol.name] - DATA
            guard_count = guard_symbol["st_size"] // 16
            base_symbol = next(name for name in symbol_values if name.endswith("8mainBaseE"))
            base_offset = symbol_values[base_symbol] - DATA
            blob_hash = 2166136261
            for byte in blob:
                blob_hash = ((blob_hash ^ byte) * 16777619) & 0xffffffff
            chain_blob = struct.pack("<6I", len(blob), base_offset, guard_offset, guard_count, blob_hash, 0x44504331) + blob
            for hook in inputs["hooks"]:
                matches = [value for name, value in symbol_values.items() if hook["name"] in name and "8CallbackE" in name]
                if len(matches) != 1:
                    raise ValueError(f"Callback identity ambiguous: {hook['name']}")
                callbacks[hook["name"]] = matches[0]
            static_blocks, static_addresses, replay = adapters(
                code_cursor, inputs["hooks"], callbacks, symbol_values["patch_initialize"], ANCHOR, KS.asm, inputs["native_debug"])
            code_cursor = max(address + len(data) for _, address, data in static_blocks)
            external["patch_main_base"] = static_addresses["patch_main_base"]
            external["patch_debug_print"] = static_addresses["patch_debug_print"]
        for offset in range(0, len(chain_blob), 8):
            address, _ = take(12)
            data_chain.append((address, offset, bytes(chain_blob[offset:offset + 8]).ljust(8, b"\0")))
        if args.complete:
            external["patch_chain_header"] = data_chain[0][0]
        # All Orig entry addresses receive real one-word trampolines below.
        original_stubs = []
        for symbol in symtab.iter_symbols():
            if symbol["st_shndx"] == "SHN_UNDEF" and "4OrigE" in symbol.name:
                hook = next(h for h in inputs["hooks"] if h["name"] in symbol.name)
                address = static_addresses["original_" + hook["name"]] if args.complete else code_cursor
                if not args.complete:
                    code_cursor += 8
                external[symbol.name] = address
                if not args.complete:
                    original_stubs.append((hook["name"], address, hook["address"], hook["word"]))

        def sym_address(index: int) -> int:
            symbol = symtab.get_symbol(index)
            section = symbol["st_shndx"]
            if section == "SHN_UNDEF":
                if symbol.name not in external:
                    raise ValueError(f"Unresolved service: {symbol.name}")
                return external[symbol.name]
            return bases[section] + symbol["st_value"]

        code_buffers = {index: data for index, _, _, data in text_sections}
        data_adrs = {}
        for section in sections:
            if not isinstance(section, RelocationSection) or section["sh_info"] not in bases:
                continue
            owner = section["sh_info"]
            for relocation in section.iter_relocations():
                kind, offset = relocation["r_info_type"], relocation["r_offset"]
                destination = sym_address(relocation["r_info_sym"]) + relocation["r_addend"]
                pc = bases[owner] + offset
                if owner not in code_buffers:
                    raise ValueError("Unexpected pointer relocation in state/constants")
                buf = code_buffers[owner]
                word = struct.unpack_from("<I", buf, offset)[0]
                if kind in (282, 283):
                    word = encode(f"{'bl' if kind == 283 else 'b'} #{destination:#x}", pc)
                elif kind == 275:
                    if DATA <= destination < data_cursor:
                        data_adrs[pc] = destination & ~0xfff
                    elif not args.complete or not 0 <= destination < 0x472b000:
                        raise ValueError("Unexpected ADRP outside private data")
                    word = encode(f"adrp x{word & 31}, #{destination & ~0xfff:#x}", pc)
                elif kind == 277:
                    word = (word & ~(0xfff << 10)) | ((destination & 0xfff) << 10)
                elif kind in (278, 284, 285, 286, 299):
                    shift = {278: 0, 284: 1, 285: 2, 286: 3, 299: 4}[kind]
                    if destination & ((1 << shift) - 1):
                        raise ValueError("Misaligned data relocation")
                    word = (word & ~(0xfff << 10)) | (((destination & 0xfff) >> shift) << 10)
                else:
                    raise ValueError(f"Unsupported relocation {kind}")
                struct.pack_into("<I", buf, offset, word)

    original = []
    function_boundaries = set()
    for _, name, base, buf in text_sections:
        function_boundaries.add(base)
        for offset in range(0, len(buf), 4):
            original.append((base + offset, struct.unpack_from("<I", buf, offset)[0], name))
    for name, pc, native_pc, word in original_stubs:
        insn = decode(word, native_pc)
        if insn.mnemonic == "adrp":
            word = encode(f"adrp {insn.op_str.split(',')[0]}, #{target(insn):#x}", pc)
        elif branch_kind(insn):
            operands = re.sub(r"#[^,]+$", f"#{target(insn):#x}", insn.op_str)
            word = encode(f"{insn.mnemonic} {operands}", pc)
        function_boundaries.add(pc)
        original.extend([(pc, word, name + "/Orig"), (pc + 4, encode(f"b #{native_pc + 4:#x}", pc + 4), name + "/Orig")])
    for name, address, data in static_blocks:
        function_boundaries.add(address)
        original.extend((address + offset, struct.unpack_from("<I", data, offset)[0], name)
                        for offset in range(0, len(data), 4))

    def source_instruction(word: int, pc: int):
        reference = replay.get(pc)
        native_pc = reference[0] + pc - reference[1] if reference else pc
        return decode(word, native_pc)

    def source_target(insn, pc: int) -> int:
        destination = target(insn)
        reference = replay.get(pc)
        if reference and branch_kind(insn) and reference[0] <= destination < reference[0] + reference[2]:
            return reference[1] + destination - reference[0]
        return destination

    # Each expanded conditional needs a complete 12-byte slot. Reserve these before
    # packing ordinary instructions so a late conditional cannot lose its slot.
    conditional_addresses = [take(12)[0] for pc, word, _ in original
                             if branch_kind(source_instruction(word, pc)) not in (None, "b", "bl")]
    conditional_slots = iter(conditional_addresses)

    mapped = {}
    emissions = []
    slot = None
    transformations = Counter()
    for index, (pc, word, name) in enumerate(original):
        insn = source_instruction(word, pc)
        if re.search(r"^(svc|brk|hvc|smc|hlt)$", insn.mnemonic) or (
            insn.mnemonic in ("mrs", "msr") and re.search(r"tpidr|cntpct|cntvct|cntfrq", insn.op_str)):
            raise ValueError(f"NCE-unsafe instruction in {name}: {insn.mnemonic} {insn.op_str}")
        conditional = branch_kind(insn) not in (None, "b", "bl")
        if pc in function_boundaries and slot:
            raise ValueError("Function falls through into another section")
        if conditional:
            if slot:
                emissions.append((slot[0], "connector", pc, None))
                slot = None
            address = next(conditional_slots)
            mapped[pc] = address
            emissions.extend([(address, "condition", pc, word), (address + 4, "taken", source_target(insn, pc), None),
                              (address + 8, "connector", pc + 4, None)])
            transformations["expanded_conditionals"] += 1
            continue
        generated = [("original", word)]
        if pc in replay and insn.mnemonic in ("bl", "blr"):
            native_base, virtual_base, _ = replay[pc]
            native_return = native_base + pc - virtual_base + 4
            generated += [("replay_lr_page", native_return), ("replay_lr_low", native_return)]
        if pc in data_adrs:
            rd = word & 31
            generated = [("data_page", rd), ("data_base", rd)]
            page = data_adrs[pc] - DATA
            if page:
                generated.append(("data_offset", (rd, page)))
            transformations["heap_address_sequences"] += 1
        for part, (kind, value) in enumerate(generated):
            terminal = kind == "original" and insn.mnemonic in ("b", "br", "ret")
            if slot is None:
                address, size = take()
                slot = [address, address + size]
            if slot[0] == slot[1] - 4 and not terminal:
                old = slot[0]
                address, size = take()
                emissions.append((old, "physical_connector", address, None))
                slot = [address, address + size]
            address = slot[0]
            if not part:
                mapped[pc] = address
            emissions.append((address, kind, pc, value))
            slot[0] += 4
            if terminal or slot[0] == slot[1]:
                slot = None
    if slot:
        raise ValueError("Last function falls through")

    def placed(destination: int) -> int:
        if CODE <= destination < code_cursor:
            return mapped[destination]
        return destination

    output = {}
    evidence = []
    for address, kind, pc, value in emissions:
        if kind in ("connector", "taken", "physical_connector"):
            destination = pc if kind == "physical_connector" else placed(pc)
            word = encode(f"b #{destination:#x}", address)
            if target(decode(word, address)) != destination:
                raise ValueError("Connector target mismatch")
        elif kind == "condition":
            insn = source_instruction(value, pc)
            if insn.mnemonic.startswith("b.") and value & 15 >= 14:
                raise ValueError("Always/never conditional requires a separate relocation rule")
            # Invert the condition to jump over the taken branch to the local fallthrough branch.
            inverted = value ^ (1 if insn.mnemonic.startswith("b.") else 0x1000000)
            shift, bits = (5, 14) if insn.mnemonic in ("tbz", "tbnz") else (5, 19)
            word = (inverted & ~(((1 << bits) - 1) << shift)) | (2 << shift)
            decoded = decode(word, address)
            if target(decoded) != address + 8 or (word ^ value) & ~((((1 << bits) - 1) << shift) | (1 if insn.mnemonic.startswith("b.") else 0x1000000)):
                raise ValueError("Conditional operands changed")
        elif kind == "data_page":
            word = encode(f"adrp x{value}, #{ANCHOR & ~0xfff:#x}", address)
            if target(decode(word, address)) != ANCHOR & ~0xfff:
                raise ValueError("Anchor page mismatch")
        elif kind == "data_base":
            word = encode(f"ldr x{value}, [x{value}, #{ANCHOR & 0xfff}]", address)
        elif kind == "data_offset":
            rd, page = value
            word = encode(f"add x{rd}, x{rd}, #{page >> 12}, lsl #12", address)
        elif kind == "replay_lr_page":
            word = encode(f"adrp x30, #{value & ~0xfff:#x}", address)
        elif kind == "replay_lr_low":
            word = encode(f"add x30, x30, #{value & 0xfff}", address)
        else:
            insn = source_instruction(value, pc)
            if branch_kind(insn) or insn.mnemonic in ("adr", "adrp"):
                destination = source_target(insn, pc)
                destination = destination if insn.mnemonic == "adrp" else placed(destination)
                operands = re.sub(r"#[^,]+$", f"#{destination:#x}", insn.op_str)
                word = encode(f"{insn.mnemonic} {operands}", address)
                decoded = decode(word, address)
                if decoded.mnemonic != insn.mnemonic or target(decoded) != destination or decoded.op_str.split("#")[0] != insn.op_str.split("#")[0]:
                    raise ValueError("Relocated instruction changed meaning")
            else:
                if insn.mnemonic in ("ldr", "ldrsw", "prfm") and "#" in insn.op_str and "[" not in insn.op_str:
                    raise ValueError("Unsupported literal load")
                word = value
                decoded = decode(word, address)
                if (decoded.mnemonic, decoded.op_str) != (insn.mnemonic, insn.op_str):
                    raise ValueError("Non-relative instruction changed")
        if address in output:
            raise ValueError("Overlapping output")
        output[address] = word
        decoded = decode(word, address)
        evidence.append(f"{address:08x}: {word:08x}  {decoded.mnemonic} {decoded.op_str}  [{kind}]")
    if set(mapped) != {pc for pc, _, _ in original}:
        raise ValueError("Not every compiler instruction was placed")
    owned = {}
    connectors = {a for a, kind, *_ in emissions if kind in ("connector", "physical_connector")}
    for address, kind, pc, _ in emissions:
        if kind not in ("connector", "physical_connector", "taken"):
            owned.setdefault(pc, []).append(address)
    fallthroughs = 0
    for pc, word, _ in original:
        insn = source_instruction(word, pc)
        if branch_kind(insn) not in (None, "b", "bl"):
            continue  # Both conditional exits were individually verified above.
        sequence = owned[pc]
        for index, address in enumerate(sequence):
            if index == len(sequence) - 1 and insn.mnemonic in ("b", "br", "ret"):
                continue
            expected = sequence[index + 1] if index + 1 < len(sequence) else mapped[pc + 4]
            successor = address + 4
            seen = set()
            while successor in connectors:
                if successor in seen:
                    raise ValueError("Cyclic fallthrough connector")
                seen.add(successor)
                decoded = decode(output[successor], successor)
                if decoded.mnemonic != "b":
                    raise ValueError("Fallthrough connector is not an unconditional branch")
                successor = target(decoded)
            if successor != expected:
                raise ValueError(f"Fallthrough changed at {pc:x}: {successor:x} != {expected:x}")
            fallthroughs += 1
    reconstructed = bytearray()
    for index, (address, offset, chunk) in enumerate(data_chain):
        next_address = data_chain[index + 1][0] if index + 1 < len(data_chain) else 0
        for delta, word in enumerate(struct.unpack("<III", struct.pack("<I", next_address) + chunk)):
            if address + 4 * delta in output:
                raise ValueError("Code overlaps data")
            output[address + 4 * delta] = word
        reconstructed.extend(chunk)
    if reconstructed[:len(chain_blob)] != chain_blob:
        raise ValueError("Data reconstruction mismatch")
    if args.complete:
        for hook in inputs["hooks"]:
            entry = placed(static_addresses["entry_" + hook["name"]])
            output[hook["address"]] = encode(f"b #{entry:#x}", hook["address"])
        for address in range(ANCHOR, ANCHOR + 16, 4):
            output[address] = 0
    serialized = b"".join(struct.pack("<II", a, w) for a, w in sorted(output.items()))
    (args.out / "placement.words").write_bytes(serialized)
    (args.out / "placement.asm").write_text("\n".join(evidence) + "\n", encoding="utf-8")
    # GNU objdump is an independent decoder. The local image contains only our
    # scattered words and zeroes, never the complete game executable.
    binary = args.out / "placement-check.bin"
    with binary.open("wb") as stream:
        for address, word in sorted(output.items()):
            stream.seek(address)
            stream.write(struct.pack("<I", word))
    objdump = args.tool_root / "vendor/totk-dkp/root/opt/devkitpro/devkitA64/bin/aarch64-none-elf-objdump.exe"
    result = subprocess.run([str(objdump), "-D", "-b", "binary", "-m", "aarch64", str(binary)],
                            text=True, capture_output=True, check=True)
    (args.out / "objdump.asm").write_text(result.stdout, encoding="utf-8")
    expected_code = {a for a, *_ in emissions}
    decoded_code = set()
    for line in result.stdout.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)", line)
        if match and (address := int(match[1], 16)) in expected_code:
            if int(match[2], 16) != output[address] or match[3] in (".inst", ".word"):
                raise ValueError(f"GNU objdump rejected code at {address:x}")
            decoded_code.add(address)
    if decoded_code != expected_code:
        raise ValueError("GNU objdump did not decode all emitted code")
    report = {
        "verdict": "Complete patch layout; runtime validation required" if args.complete else "Offline capacity/relocation proof only; adapters and initialization remain unimplemented",
        "input_sha256": hashlib.sha256(args.object.read_bytes()).hexdigest(),
        "inventory_sha256": hashlib.sha256(args.inputs.read_bytes()).hexdigest(),
        "registry_sha256": inputs["registry_sha256"],
        "clean_image_sha256": inputs["clean_image_sha256"],
        "native_header_sha256": inputs["native_header_sha256"],
        "placement_sha256": hashlib.sha256(serialized).hexdigest(),
        "compiler_and_orig_instructions": len(original), "emitted_code_instructions": len(emissions),
        "private_data_bytes": len(blob), "zero_state_bytes": bss_bytes,
        "allocation_bytes": 8192, "anchor": hex(ANCHOR), "anchor_bytes": 16,
        "reserved_adapter_effective_bytes": capacity, "reserved_adapter_gaps": len(reserved),
        "used_gap_counts": dict(Counter(size for _, size in used_gaps)),
        "remaining_gap_counts": {str(size): len(values) for size, values in gaps.items()},
        "remaining_effective_bytes": sum((size - 4) * len(values) for size, values in gaps.items()),
        "data_chunks": len(data_chain), "transformations": dict(transformations),
        "placement_range": [hex(min(a for a, _ in used_gaps)), hex(max(a + n for a, n in used_gaps))],
        "all_hooks_registry_conflict_free": True, "all_compiler_instructions_covered": True,
        "objdump_decoded_code_instructions": len(decoded_code),
        "verified_fallthroughs": fallthroughs,
    }
    if args.complete:
        sys.path.insert(0, str(args.tool_root / "scripts"))
        from patch_gaps import registry
        from patch_gaps.gaps import find_gaps, referenced_offsets, unreferenced
        from patch_gaps.image import FULL_BUILD_ID, Image
        from patch_gaps.place import apply_both, write_ips32, write_pchtxt
        clean = Image.load()
        if hashlib.sha256(clean.data).hexdigest() != inputs["clean_image_sha256"]:
            raise ValueError("Clean game image changed after inventory")
        if hashlib.sha256(registry.path_for(clean.build_id).read_bytes()).hexdigest() != inputs["registry_sha256"]:
            raise ValueError("Registry changed after inventory")
        ranges = registry.all_ranges(registry.load(clean.build_id))
        fresh_gaps = {(g.start, g.size) for g in unreferenced(find_gaps(clean), referenced_offsets(clean))}
        if len(set(used_gaps)) != len(used_gaps) or any(g not in fresh_gaps for g in used_gaps):
            raise ValueError("Selected gap is duplicated, referenced or not clean RET alignment padding")
        for hook in inputs["hooks"]:
            actual = [clean.word(hook["address"] + n) for n in range(0, 20, 4)]
            if actual != hook["original_words"] or actual[0] != hook["word"]:
                raise ValueError("Original replay prefix differs from clean game")
        for address in output:
            if any(a < address + 4 and address < b for a, b, *_ in ranges):
                raise ValueError(f"Final output overlaps registry at {address:x}")
        patch = args.out / "descend.pchtxt"
        ips = args.out / f"{FULL_BUILD_ID[clean.build_id]}.ips"
        write_pchtxt(patch, FULL_BUILD_ID[clean.build_id], sorted(output.items()), image_offsets=True)
        write_ips32(ips, sorted(output.items()))
        mapped_image = Image(clean.build_id, clean.data.ljust(int(inputs["native_rw"]["mapped_end"], 16), b"\0"), clean.text_end)
        applied = apply_both(mapped_image, patch, ips)
        report.update({"pchtxt_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
                       "native_debug": inputs["native_debug"],
                       "ips_sha256": hashlib.sha256(ips.read_bytes()).hexdigest(),
                       "applied_image_sha256": hashlib.sha256(applied).hexdigest(),
                       "native_services": {name: placed(value) for name, value in external.items()},
                       "callbacks": {name: placed(value) for name, value in callbacks.items()},
                       "adapters": {name: placed(value) for name, value in static_addresses.items()},
                       "runtime_symbols": {name: placed(value) for name, value in symbol_values.items()
                                           if name.startswith("patch_") or "PatchLogger" in name or
                                           "afterCameraUpdate" in name or "beforeCameraUpdate" in name},
                       "data_chain_head": data_chain[0][0], "main_base_offset": base_offset,
                       "guard_offset": guard_offset, "guard_count": guard_count})
        source_words = {pc: word for pc, word, _ in original}
        report["private_address_sequences"] = [
            {"entry": placed(pc), "continuation": placed(pc + 4), "register": source_words[pc] & 31,
             "offset": page - DATA} for pc, page in data_adrs.items()]
        report["replay_instructions"] = [
            {"entry": placed(pc), "native_pc": native + pc - virtual, "continuation": placed(pc + 4)}
            for pc, (native, virtual, _) in replay.items()]
        (args.out / "private-data.bin").write_bytes(blob)
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ("native_services", "callbacks", "adapters", "runtime_symbols", "private_address_sequences", "replay_instructions")}, indent=2))


if __name__ == "__main__":
    main()
