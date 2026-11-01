"""Read-only inventory for the offline patch feasibility proof."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from native_debug import inspect as inspect_native_debug


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--object", type=Path, required=True)
    parser.add_argument("--nso", type=Path, required=True, help="Original clean 1.2.1 NSO, for real segment boundaries")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.tool_root / "scripts"))
    from patch_gaps import registry
    from patch_gaps.gaps import find_gaps, overlaps, referenced_offsets, unreferenced
    from patch_gaps.image import FULL_BUILD_ID, Image
    image = Image.load()
    data = image.data
    mod = struct.unpack_from("<I", data, 4)[0]
    _, dynamic, bss_start, bss_end = struct.unpack_from("<4siii", data, mod)
    with args.nso.open("rb") as stream:
        header = stream.read(0x100)
    if header[:4] != b"NSO0" or header[0x40:0x54].hex().upper() != FULL_BUILD_ID[image.build_id]:
        raise ValueError("Original NSO is not the expected 1.2.1 build")
    _, rw_start, rw_size, bss_size = struct.unpack_from("<IIII", header, 0x30)
    mapped_end = (rw_start + rw_size + bss_size + 0xfff) & ~0xfff
    if struct.unpack_from("<I", header, 0x18)[0] != image.text_end or hashlib.sha256(data[:image.text_end]).digest() != header[0xa0:0xc0]:
        raise ValueError("Original NSO text disagrees with the working clean image")
    if not rw_start + rw_size <= mod + bss_start < mod + bss_end < mapped_end:
        raise ValueError("Original NSO does not back the proposed writable tail")
    tags = {}
    cursor = mod + dynamic
    while True:
        tag, value = struct.unpack_from("<qQ", data, cursor)
        if not tag:
            break
        tags[tag] = value
        cursor += 16
    imports = {}
    for cursor in range(tags[23], tags[23] + tags[2], 24):
        got, info, _ = struct.unpack_from("<QQq", data, cursor)
        name_offset = struct.unpack_from("<I", data, tags[6] + 24 * (info >> 32))[0]
        name_start = tags[5] + name_offset
        name = data[name_start:data.index(b"\0", name_start)].decode()
        imports[got] = name
    tick_slots = [got for got, name in imports.items() if name == "_ZN2nn2os13GetSystemTickEv"]
    if len(tick_slots) != 1:
        raise ValueError("Native tick import is not unique")
    native_debug = inspect_native_debug(args.nso.with_name("sdk"), tick_slots[0])
    resolved = {}
    for pc in range(0x2B17240, image.text_end, 16):
        adr, ldr = struct.unpack_from("<II", data, pc)
        imm = (((adr >> 5) & 0x7ffff) << 2) | ((adr >> 29) & 3)
        if imm & 0x100000:
            imm -= 0x200000
        got = (pc & ~0xfff) + (imm << 12) + ((ldr >> 10) & 0xfff) * 8
        if got in imports:
            resolved[imports[got]] = hex(pc)
    refs = referenced_offsets(image)
    words = np.frombuffer(data, dtype="<u4", count=image.text_end // 4).astype(np.int64)
    adr_indices = np.flatnonzero((words & 0x9f000000) == 0x90000000)
    adr_words = words[adr_indices]
    immediates = (((adr_words >> 5) & 0x7ffff) << 2) | ((adr_words >> 29) & 3)
    immediates = np.where(immediates & 0x100000, immediates - 0x200000, immediates)
    pages = ((adr_indices * 4) & ~0xfff) + (immediates << 12)
    tail_page_adrs = [hex(int(index * 4)) for index in adr_indices[pages == ((mod + bss_end) & ~0xfff)]]
    reg = registry.load(image.build_id)
    ranges = [(a, b) for a, b, _, _ in registry.all_ranges(reg)]
    gaps = [g for g in unreferenced(find_gaps(image), refs) if not overlaps(g, ranges)]
    mod_root = Path(__file__).resolve().parents[1]
    offsets = {name: int(value, 16) for name, value in re.findall(
        r"(k\w+)\s*=\s*(0x[0-9A-Fa-f]+)", (mod_root / "src/engine/DescendOffsets.hpp").read_text())}
    inventory = json.loads((mod_root / "src/program/hooks.json").read_text())
    hooked = [(hook["name"], hook["offset"]) for hook in inventory]
    kinds = {hook["name"]: hook["kind"] for hook in inventory}
    hooks = []
    for name, constant in hooked:
        pc = offsets[constant]
        hooks.append({"name": name, "address": pc, "word": image.word(pc), "kind": kinds[name],
                      "original_words": [image.word(pc + n) for n in range(0, 20, 4)],
                      "conflicts": [entry for entry in registry.all_ranges(reg) if entry[0] < pc + 4 and pc < entry[1]]})
    with args.object.open("rb") as stream:
        elf = ELFFile(stream)
        relocs = Counter()
        readonly = []
        for section in elf.iter_sections():
            if isinstance(section, RelocationSection):
                relocs.update(r["r_info_type"] for r in section.iter_relocations())
            elif section.name.startswith(".rodata"):
                readonly.append((section.name, section["sh_size"]))
    result = {
        "registry_sha256": hashlib.sha256(registry.path_for(image.build_id).read_bytes()).hexdigest(),
        "clean_image_sha256": hashlib.sha256(data).hexdigest(),
        "native_header_sha256": hashlib.sha256(header).hexdigest(),
        "native_rw": {"start": hex(rw_start), "initialized_size": hex(rw_size),
                      "header_bss_size": hex(bss_size), "mapped_end": hex(mapped_end)},
        "bss": [hex(mod + bss_start), hex(mod + bss_end)],
        "tail_references": [hex(int(r)) for r in refs if mod + bss_end <= r < ((mod + bss_end + 0xfff) & ~0xfff)],
        "tail_page_adrp_sites": tail_page_adrs,
        "tail_claims": [entry for entry in registry.all_ranges(reg) if entry[0] < 0x472b000 and 0x472a2b0 < entry[1]],
        "imports": resolved,
        "native_debug": native_debug,
        "gap_counts": dict(Counter(g.size for g in gaps)),
        "gaps": [(g.start, g.size) for g in gaps],
        "relocation_counts": dict(relocs), "readonly_sections": readonly,
        "hooks": hooks,
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"bss": result["bss"], "gap_counts": result["gap_counts"],
                      "tail_references": result["tail_references"],
                      "tail_page_adrp_sites": tail_page_adrs,
                      "claimed_tail_spans": sorted({(hex(a), hex(b)) for a, b, *_ in result["tail_claims"]}),
                      "hook_conflicts": sum(len(h["conflicts"]) for h in hooks),
                      "relocation_counts": result["relocation_counts"]}, indent=2))


if __name__ == "__main__":
    main()
