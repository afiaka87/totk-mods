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
from patch_registry import owned_sources


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--object", type=Path, required=True)
    parser.add_argument("--nso", type=Path, required=True, help="Original clean target NSO, for real segment boundaries")
    parser.add_argument("--direct-branches", action="store_true", help="Also consider verified alignment after unconditional B")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--community", type=Path, help="Exact compatibility input for shrine gates")
    parser.add_argument("--relocate-community", action="store_true", help="Research: place ported compatibility code inside native padding")
    args = parser.parse_args()
    sys.path.insert(0, str(args.tool_root / "scripts"))
    from patch_gaps import registry
    from patch_gaps.gaps import find_gaps, overlaps, referenced_offsets, unreferenced
    from patch_gaps.image import FULL_BUILD_ID, Image
    profile_path = args.object.parent / "native-profile.json"
    profile = json.loads(profile_path.read_text())
    compiled = json.loads((args.object.parent / "probe.json").read_text())
    restricted = '-DULTIMATE_ASCEND_SHRINE_RESTRICTED=1' in compiled['flags']
    if (compiled["probe_sha256"] != hashlib.sha256(args.object.read_bytes()).hexdigest()
            or compiled["native_profile_sha256"] != profile["header_sha256"]
            or compiled["game_version"] != profile["game_version"]):
        raise ValueError("Compiled object and native profile receipts disagree")
    image = Image.load(profile["build_id"])
    header_path = args.object.parent / "include/engine/DescendOffsets.hpp"
    if hashlib.sha256(header_path.read_text().encode()).hexdigest() != profile["header_sha256"]:
        raise ValueError("Compiled native header differs from profile receipt")
    community_sites = []
    community_sha256 = None
    community_port = None
    if args.relocate_community:
        if not args.community:
            raise ValueError("Community relocation needs the pinned source patch")
        from community_port import port
        from totk_version_port.images import MainImage
        community_port = port(args.community, profile["game_version"], MainImage(profile["game_version"]))
        community_sites = community_port["hooks"]
        community_sha256 = community_port["source_sha256"]
    elif args.community:
        if profile["game_version"] != "1.2.1":
            raise ValueError("The original community patch is only for 1.2.1; use the reviewed port")
        from patch_gaps.formats import parse_pchtxt
        community_sha256 = hashlib.sha256(args.community.read_bytes()).hexdigest()
        if community_sha256 != "562abf3b538b1b4d189ad965bb389ab0b5b4845a27100508c0fb81a8f91439d9":
            raise ValueError("Unsupported community compatibility input")
        _, writes = parse_pchtxt(args.community.read_text())
        for write in writes:
            if not write.enabled or write.off >= image.text_end:
                continue
            if write.off & 3 or len(write.data) % 4:
                raise ValueError("Community instruction patch is unaligned")
            for index in range(0, len(write.data), 4):
                pc = write.off + index
                community_sites.append({"address": pc, "original": image.word(pc),
                                        "patched": struct.unpack_from("<I", write.data, index)[0]})
        if len(community_sites) != 20:
            raise ValueError("Community hook inventory changed")
    data = image.data
    mod = struct.unpack_from("<I", data, 4)[0]
    _, dynamic, bss_start, bss_end = struct.unpack_from("<4siii", data, mod)
    with args.nso.open("rb") as stream:
        header = stream.read(0x100)
    if header[:4] != b"NSO0" or header[0x40:0x54].hex().upper() != FULL_BUILD_ID[image.build_id]:
        raise ValueError("Original NSO is not the selected native profile build")
    _, rw_start, rw_size, bss_size = struct.unpack_from("<IIII", header, 0x30)
    mapped_end = (rw_start + rw_size + bss_size + 0xfff) & ~0xfff
    if struct.unpack_from("<I", header, 0x18)[0] != image.text_end or hashlib.sha256(data[:image.text_end]).digest() != header[0xa0:0xc0]:
        raise ValueError("Original NSO text disagrees with the working clean image")
    if not rw_start + rw_size <= mod + bss_start < mod + bss_end < mapped_end:
        raise ValueError("Original NSO does not back the proposed writable tail")
    anchor = (mod + bss_end + 15) & ~15
    if anchor + 16 > mapped_end:
        raise ValueError("No complete aligned anchor fits in the native writable tail")
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
    words = np.frombuffer(data, dtype="<u4", count=image.text_end // 4).astype(np.int64)
    candidates = np.flatnonzero((words[:-3] & 0x9f00001f) == 0x90000010)
    for index in candidates.tolist():
        pc = index * 4
        adr, ldr, add, branch = struct.unpack_from("<4I", data, pc)
        if ldr & 0xffc003ff != 0xf9400211 or add & 0xffc003ff != 0x91000210 or branch != 0xd61f0220:
            continue
        imm = (((adr >> 5) & 0x7ffff) << 2) | ((adr >> 29) & 3)
        if imm & 0x100000:
            imm -= 0x200000
        got = (pc & ~0xfff) + (imm << 12) + ((ldr >> 10) & 0xfff) * 8
        if got in imports and ((add >> 10) & 0xfff) == got & 0xfff:
            if imports[got] in resolved:
                raise ValueError(f"Duplicate native import stub: {imports[got]}")
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
    claims = registry.all_ranges(reg, skip=owned_sources(reg))
    ranges = [(a, b) for a, b, _, _ in claims]
    anchors = [pc for pc in range(anchor,mapped_end-15,16)
               if not any(pc <= r < pc+16 for r in refs)
               and not any(a < pc+16 and pc < b for a,b in ranges)]
    if not anchors:
        raise ValueError('No unreferenced, unclaimed 16-byte anchor fits the native writable tail')
    anchor = anchors[0]
    gaps = [g for g in unreferenced(find_gaps(image, min_size=4, direct_branches=args.direct_branches), refs)
            if not overlaps(g, ranges)]
    mod_root = Path(__file__).resolve().parents[1]
    offsets = profile["bindings"]
    inventory = json.loads((mod_root / "src/program/hooks.json").read_text())
    hooked = [(hook["name"], hook["offset"]) for hook in inventory]
    kinds = {hook["name"]: hook["kind"] for hook in inventory}
    hooks = []
    for name, constant in hooked:
        pc = offsets[constant]
        hooks.append({"name": name, "address": pc, "word": image.word(pc), "kind": kinds[name],
                      "original_words": [image.word(pc + n) for n in range(0, 20, 4)],
                      "conflicts": [entry for entry in claims if entry[0] < pc + 4 and pc < entry[1]]})
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
        "game_version": profile["game_version"], "build_id": image.build_id,
        "research_only": profile["research_only"], "native_profile": profile,
        "shrine_restricted": restricted,
        "anchor": hex(anchor), "direct_branch_padding": args.direct_branches,
        "text_end": image.text_end,
        "registry_sha256": hashlib.sha256(registry.path_for(image.build_id).read_bytes()).hexdigest(),
        "clean_image_sha256": hashlib.sha256(data).hexdigest(),
        "native_header_sha256": hashlib.sha256(header).hexdigest(),
        "native_rw": {"start": hex(rw_start), "initialized_size": hex(rw_size),
                      "header_bss_size": hex(bss_size), "mapped_end": hex(mapped_end)},
        "bss": [hex(mod + bss_start), hex(mod + bss_end)],
        "tail_references": [hex(int(r)) for r in refs if anchor <= r < anchor+16],
        "other_tail_references": [hex(int(r)) for r in refs if mod+bss_end <= r < mapped_end and not anchor <= r < anchor+16],
        "tail_page_adrp_sites": tail_page_adrs,
        "tail_claims": [entry for entry in claims if entry[0] < anchor+16 and anchor < entry[1]],
        "imports": resolved,
        "native_debug": native_debug,
        "gap_counts": dict(Counter(g.size for g in gaps)),
        "gaps": [(g.start, g.size) for g in gaps],
        "relocation_counts": dict(relocs), "readonly_sections": readonly,
        "hooks": hooks,
        "community_sites": community_sites, "community_sha256": community_sha256,
        "community_port": community_port,
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
