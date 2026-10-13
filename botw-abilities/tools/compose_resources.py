# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Merge ability resources against the clean game while preserving every parse-budget floor."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
import tomllib
import zlib
from pathlib import Path

import byml

ABILITIES = ("totk-stasis", "totk-remote-bomb", "totk-cryonis")
BANK = "Sound/Resource/UI_Static.bars.zs"
TABLES = ("RSDB/ActorInfo.Product.121.rstbl.byml.zs", "RSDB/GameActorInfo.Product.121.rstbl.byml.zs")
SOUND_DONOR = "Sys_HeartRecovery_00"
RESOURCE_TABLE = "System/Resource/ResourceSizeTable.Product.121.rsizetable.zs"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stock_budgets(raw: bytes):
    if raw[:6] != b"RESTBL":
        raise ValueError("expected the clean TotK resource table")
    width, count, overflow = struct.unpack_from("<III", raw, 10)
    by_crc = dict(struct.unpack_from("<II", raw, 22 + index * 8) for index in range(count))
    by_name = {}
    for index in range(overflow):
        start = 22 + count * 8 + index * (width + 4)
        by_name[raw[start : start + width].split(b"\0", 1)[0].decode()] = struct.unpack_from(
            "<I", raw, start + width
        )[0]
    return lambda path: by_name.get(path, by_crc.get(zlib.crc32(path.encode()), 0))


def read_forces(root: Path, deploy: dict) -> dict[str, int]:
    forces = {entry["resource"]: entry["size"] for entry in deploy.get("restbl_force", [])}
    if deploy.get("restbl_force_file"):
        for line in (root / deploy["restbl_force_file"]).read_text(encoding="utf-8").splitlines():
            if line.strip() and not line.lstrip().startswith("#"):
                resource, size = line.split("\t")
                forces[resource.strip()] = max(forces.get(resource.strip(), 0), int(size))
    return forces


def added_sounds(audio_bank, clean, bank):
    """Each sound an ability appended to the clean bank, with its waveform and padding."""
    audio_bank.verify_preserved(clean, bank)
    stock = {entry.name for entry in clean.entries}
    added = sorted((e for e in bank.entries if e.name not in stock), key=lambda e: e.audio_offset)
    stock_end = bank.audio_start + len(clean.data) - clean.audio_start
    ends = [entry.audio_offset for entry in added[1:]] + [len(bank.data)]
    sounds = []
    for entry, end in zip(added, ends, strict=True):
        flags = entry.flags
        if entry.audio_offset < stock_end or not flags & 2 or flags & ~7:
            raise ValueError(f"unsupported added sound {entry.name}")
        wave = bank.data[entry.audio_offset : end]
        peak = struct.unpack_from("<f", entry.metadata, 0x38)[0]
        sounds.append((entry, audio_bank.Addition(entry.name, wave, peak, bool(flags & 1), bool(flags & 4))))
    return sounds


def merge_bank(audio_bank, clean_raw: bytes, banks: dict[str, bytes]):
    clean = audio_bank.read_bank(clean_raw)
    per_ability = {name: added_sounds(audio_bank, clean, audio_bank.read_bank(raw)) for name, raw in banks.items()}
    merged = clean
    for sounds in per_ability.values():
        additions = [addition for _, addition in sounds]
        for start in range(0, len(additions), 8):
            merged = audio_bank.read_bank(
                audio_bank.append_assets(merged, tuple(additions[start : start + 8]), SOUND_DONOR)
            )
    audio_bank.verify_preserved(clean, merged)
    by_name = {entry.name: entry for entry in merged.entries}
    for name, sounds in per_ability.items():
        for original, addition in sounds:
            entry = by_name[original.name]
            if entry.metadata != original.metadata:
                raise ValueError(f"{name}: sound settings changed for {original.name}")
            if merged.data[entry.audio_offset : entry.audio_offset + len(addition.wave)] != addition.wave:
                raise ValueError(f"{name}: waveform changed for {original.name}")
    return merged.data, {name: [entry.name for entry, _ in sounds] for name, sounds in per_ability.items()}


def merge_table(clean_raw: bytes, tables: dict[str, bytes]):
    clean = {row["__RowId"]: row for row in byml.Byml(clean_raw).parse()}
    merged, owner = dict(clean), {}
    for name, raw in tables.items():
        rows = byml.Byml(raw).parse()
        ids = [row["__RowId"] for row in rows]
        if len(set(ids)) != len(ids) or clean.keys() - set(ids):
            raise ValueError(f"{name}: table drops or repeats rows")
        for row in rows:
            row_id = row["__RowId"]
            if clean.get(row_id) == row:
                continue
            if row_id in owner and merged[row_id] != row:
                raise ValueError(f"{name} and {owner[row_id]} both change {row_id}")
            merged[row_id], owner[row_id] = row, name
    rows = sorted(merged.values(), key=lambda row: row["__RowId"])
    encoded = byml.Writer(rows, version=7).get_bytes()
    if byml.Byml(encoded).parse() != rows:
        raise ValueError("merged table does not read back")
    added = {name: sorted(row for row, who in owner.items() if who == name) for name in tables}
    return encoded, added


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    parser.add_argument("--output", type=Path, required=True, help="build directory")
    parser.add_argument("--own", type=Path, action="append", default=[],
                        help="a romfs tree only the combined mod ships, such as its wheel icons")
    args = parser.parse_args()
    sys.path[:0] = [str(args.repo / "scripts"), str(args.repo / "framework/bridgefs/tools")]
    from bridgefs_tools import audio_bank
    from totk_mod_tools.zstd_helper import TotkZstd

    owners: dict[str, list[tuple[str, Path]]] = {}
    forces: dict[str, int] = {}
    inputs = hashlib.sha256(Path(__file__).read_bytes())
    for name in ABILITIES:
        root = args.repo / "mods/code" / name
        manifest = tomllib.loads((root / "mod.toml").read_text(encoding="utf-8"))
        for entry in manifest["payload"]["file"]:
            if entry["dest"].startswith("romfs/"):
                source = root / entry["source"]
                owners.setdefault(entry["dest"].removeprefix("romfs/"), []).append((name, source))
                inputs.update(entry["dest"].encode() + b"\0" + source.read_bytes())
        for resource, size in read_forces(root, manifest.get("deploy", {})).items():
            forces[resource] = max(forces.get(resource, 0), size)
    for tree in args.own:
        for source in sorted(p for p in tree.rglob("*") if p.is_file()):
            path = source.relative_to(tree).as_posix()
            if path in owners:
                raise ValueError(f"{path} is shipped by {owners[path][0][0]} and by the combined mod")
            owners[path] = [("botw-abilities", source)]
            inputs.update(path.encode() + b"\0" + source.read_bytes())

    romfs_out = args.output / "romfs"
    receipt_path = args.output / "compose-receipt.json"
    digest = inputs.hexdigest()
    if receipt_path.exists() and (args.output / "restbl-forces.tsv").exists():
        previous = json.loads(receipt_path.read_text(encoding="utf-8"))
        if previous.get("inputs_sha256") == digest and all(
            (romfs_out / path).is_file() and sha256((romfs_out / path).read_bytes()) == value
            for path, value in previous["files"].items()
        ):
            print("[botw-abilities] game data unchanged")
            return 0

    shutil.rmtree(romfs_out, ignore_errors=True)
    z = TotkZstd(args.romfs)
    budget = stock_budgets(z.decompress(args.romfs / RESOURCE_TABLE))
    merges, files = {}, {}
    for path, sources in sorted(owners.items()):
        target = romfs_out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        if len(sources) == 1:
            shutil.copyfile(sources[0][1], target)
            files[path] = sha256(target.read_bytes())
            continue
        clean = z.decompress(args.romfs / path)
        members = {name: z.decompress(source) for name, source in sources}
        if path == BANK:
            raw, added = merge_bank(audio_bank, clean, members)
            floor = len(raw) + 65536
        elif path in TABLES:
            raw, added = merge_table(clean, members)
            resource = path.removesuffix(".zs")
            floor = max(budget(resource), len(raw)) + max(4096, (len(raw) - len(clean)) * 10)
        else:
            raise ValueError(f"{path} is shipped by {[name for name, _ in sources]} and has no merge")
        target.write_bytes(z.compress(raw, target.name))
        if z.decompress(target) != raw:
            raise ValueError(f"{path} does not decompress to the merged bytes")
        resource = path.removesuffix(".zs")
        forces[resource] = max(forces.get(resource, 0), floor)
        files[path] = sha256(target.read_bytes())
        merges[path] = {"abilities": [name for name, _ in sources], "added": added,
                        "raw_bytes": len(raw), "budget": forces[resource]}

    header = "# BotW Abilities parse-budget floors: the largest each ability asks for, raised for merged files.\n"
    (args.output / "restbl-forces.tsv").write_text(
        header + "".join(f"{resource}\t{size}\n" for resource, size in sorted(forces.items())),
        encoding="utf-8", newline="\n")
    receipt = {"inputs_sha256": digest, "abilities": list(ABILITIES), "files": files, "merges": merges,
               "forces": len(forces)}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"[botw-abilities] {len(files)} game files, {len(merges)} merged, {len(forces)} budget floors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
