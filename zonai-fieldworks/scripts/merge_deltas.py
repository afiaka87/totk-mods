#!/usr/bin/env python3
# Copyright (c) HanSoloBlaster, afiaka87
"""Write what Zonai Fieldworks adds to each shared game file, for the module to add as the game loads them.

Another mod may ship its own copy of a file Fieldworks also changes (the actor tables, the save-flag list,
the tag table, the message archive, the interface sound bank, the resource-size table). The game then loads
only one copy. The module (src/fieldworks/SharedFiles.cpp) adds Fieldworks' content to whichever copy was
loaded; this script produces that content from the clean game files and Fieldworks' own, nothing else.

  merge_deltas.py --set 121 <clean romfs> <fieldworks romfs> <fieldworks size table> [--set 143 ...]
                  [--shared <fieldworks romfs>] --output <build>/merge

The build stops when Fieldworks changes a shared file in a way the module cannot repeat on another copy.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(Path(__file__).resolve().parent), str(REPO / "scripts"), str(REPO / "mods/code/botw-abilities/tools"),
                str(REPO / "framework/bridgefs/tools")]

import byml  # noqa: E402
import compose_romfs  # noqa: E402
import oead  # noqa: E402
import resource_merge  # noqa: E402
import zstandard  # noqa: E402
from bridgefs_tools import audio_bank  # noqa: E402
from compose_resources import added_sounds  # noqa: E402
from totk_mod_tools.zstd_helper import TotkZstd  # noqa: E402
from totk_version_port import romfs_rebase as rb  # noqa: E402

LITTLE = b"YB\x07\x00"
ROW_TABLES = ("ActorInfo", "AttachmentActorInfo", "GameActorInfo", "PouchActorInfo")
SCALAR_COST = {"Bool": 8, "Int": 8, "UInt": 8, "Float": 8, "Enum": 8, "Struct": 0}
SLACK = 65536


class Unsupported(ValueError):
    pass


written: set[Path] = set()


def only(root: Path, pattern: str) -> str | None:
    found = sorted(path.relative_to(root).as_posix() for path in root.glob(pattern))
    if len(found) > 1:
        raise Unsupported(f"{pattern}: more than one file in {root}")
    return found[0] if found else None


def rows_delta(clean: bytes, mine: bytes, path: str) -> bytes:
    base, new = byml.Byml(clean).parse(), byml.Byml(mine).parse()
    before = {row["__RowId"]: row for row in base}
    after = {row["__RowId"]: row for row in new}
    if len(after) != len(new) or before.keys() - after.keys():
        raise Unsupported(f"{path}: Fieldworks removes or repeats rows")
    changed = [name for name, row in before.items() if after[name] != row]
    if changed:
        raise Unsupported(f"{path}: Fieldworks changes stock rows {changed[:4]}")
    added = sorted((row for name, row in after.items() if name not in before), key=lambda row: row["__RowId"])
    return rb.encode_byml(added, mine) if added else b""


def gamedata_delta(clean: bytes, mine: bytes, path: str) -> bytes:
    base, new = byml.Byml(clean).parse(), byml.Byml(mine).parse()
    if set(new) != set(base) or set(new["Data"]) != set(base["Data"]):
        raise Unsupported(f"{path}: Fieldworks changes the list's sections")
    steps = {name: {} for name in ("Rows", "Cost", "Choices", "Entries", "Raise", "Lengths")}
    for kind, current in new["Data"].items():
        before = resource_merge.indexed(base["Data"][kind], "Hash", kind)
        after = resource_merge.indexed(current, "Hash", kind)
        if before.keys() - after.keys():
            raise Unsupported(f"{path}: {kind}: stock save flags removed")
        fresh = [row for row in current if row["Hash"] not in before]
        if fresh:
            if kind not in SCALAR_COST:
                raise Unsupported(f"{path}: {kind}: a new flag of this kind has no verified save recipe")
            steps["Rows"][kind] = fresh
            steps["Cost"][kind] = byml.UInt(SCALAR_COST[kind])
        for identity, original in before.items():
            row, where = after[identity], f"{path}: {kind}/{int(identity):#x}"
            if row == original:
                continue
            if kind in resource_merge.GROWN_ARRAYS:
                resource_merge.grown_elements(original, row, where)
                item = {"Hash": row["Hash"], "Length": byml.UInt(len(row["DefaultValue"])),
                        "ElementBytes": byml.UInt(resource_merge.GROWN_ARRAYS[kind])}
                if row["OriginalSize"] != original["OriginalSize"]:
                    item["OriginalSize"] = row["OriginalSize"]
                steps["Lengths"].setdefault(kind, []).append(item)
            elif kind in ("Enum", "EnumArray") and resource_merge.enum_choices_appended(original, row):
                count = len(original["RawValues"])
                steps["Choices"].setdefault(kind, []).append(
                    {"Hash": row["Hash"], "RawValues": row["RawValues"][count:], "Values": row["Values"][count:]})
            elif kind == "Struct":
                keep = ("DefaultValue", "Size")
                if resource_merge.without(row, keep) != resource_merge.without(original, keep) \
                        or int(row.get("Size", 0)) < int(original.get("Size", 0)):
                    raise Unsupported(f"{where}: stock save flag changed")
                old = resource_merge.indexed(original["DefaultValue"], "Hash", where)
                entries = resource_merge.indexed(row["DefaultValue"], "Hash", where)
                if any(entries.get(key) != value for key, value in old.items()):
                    raise Unsupported(f"{where}: stock registry entry changed")
                added = [entry for entry in row["DefaultValue"] if entry["Hash"] not in old]
                if added:
                    steps["Entries"].setdefault(kind, []).append({"Hash": row["Hash"], "DefaultValue": added})
                if int(row.get("Size", 0)) > int(original.get("Size", 0)):
                    if not isinstance(row["Size"], byml.UInt):
                        raise Unsupported(f"{where}: Size is not an unsigned value")
                    steps["Raise"].setdefault(kind, []).append({"Hash": row["Hash"], "Size": row["Size"]})
            else:
                raise Unsupported(f"{where}: stock save flag changed")
    steps = {name: value for name, value in steps.items() if value}
    if "Rows" not in steps:
        steps.pop("Cost", None)
    # The save sizes must be exactly what the shared recipe gives for these changes.
    wanted = resource_merge.saved_metadata(base, new["Data"])
    if new["MetaData"] != wanted:
        raise Unsupported(f"{path}: Fieldworks' save sizes are not what the shared recipe gives")
    return rb.encode_byml(steps, mine) if steps else b""


def tag_delta(clean: bytes, mine: bytes, path: str) -> bytes:
    base, new = byml.Byml(clean).parse(), byml.Byml(mine).parse()
    if new["TagList"] != base["TagList"]:
        raise Unsupported(f"{path}: Fieldworks changes the tag list")
    (old_paths, old_bits), (new_paths, new_bits) = rb._tag_bits(base), rb._tag_bits(new)
    index = {triple: i for i, triple in enumerate(old_paths)}
    if set(old_paths) - set(new_paths) or len(set(new_paths)) != len(new_paths):
        raise Unsupported(f"{path}: Fieldworks removes or repeats actors")
    added = []
    for i, triple in enumerate(new_paths):
        if triple in index:
            if (new_bits[i] != old_bits[index[triple]]).any():
                raise Unsupported(f"{path}: Fieldworks changes the tags of stock actor {triple[1]}")
        else:
            added.append(list(triple) + [tag for tag, bit in zip(new["TagList"], new_bits[i]) if bit])
    return rb.encode_byml(sorted(added, key=lambda row: row[:3]), LITTLE) if added else b""


def mals_delta(clean: bytes, mine: bytes, path: str) -> bytes:
    base, new = rb._sarc(clean), rb._sarc(mine)
    if base.keys() - new.keys() or new.keys() - base.keys():
        raise Unsupported(f"{path}: Fieldworks adds or removes message files")
    files = []
    for name in sorted(new):
        if new[name] == base[name]:
            continue
        _, _, before = resource_merge.read_msbt(base[name])
        _, _, after = resource_merge.read_msbt(new[name])
        if any(after.get(label) != text for label, text in before.items()):
            raise Unsupported(f"{path}: {name}: Fieldworks changes stock text")
        labels = sorted(after.keys() - before.keys())
        if resource_merge.append_msbt(base[name], {label: after[label] for label in labels}) != new[name]:
            raise Unsupported(f"{path}: {name}: Fieldworks' file is not the stock file with labels appended")
        if any(len(label.encode("utf-8")) > 255 for label in labels):
            raise Unsupported(f"{path}: {name}: a label is too long")
        files.append({"File": name, "Labels": labels, "Texts": [bytes(after[label]) for label in labels]})
    return rb.encode_byml(files, LITTLE) if files else b""


def bank_delta(clean: bytes, mine: bytes, path: str) -> bytes:
    try:
        sounds = added_sounds(audio_bank, audio_bank.read_bank(clean), audio_bank.read_bank(mine))
    except ValueError as error:
        raise Unsupported(f"{path}: {error}") from error
    if not sounds:
        return b""
    head = 8 + 20 * len(sounds)
    records, blobs = [], bytearray()
    for entry, sound in sounds:
        if len(sound.metadata) % 4:
            raise Unsupported(f"{path}: {entry.name}: sound settings are not a multiple of four bytes")
        records.append([entry.checksum, head + len(blobs), len(sound.metadata)])
        blobs += sound.metadata
    blobs += bytes(-(head + len(blobs)) % 64)
    for record, (_, sound) in zip(records, sounds, strict=True):
        record += [head + len(blobs), len(sound.wave)]
        blobs += sound.wave + bytes(-len(sound.wave) % 64)
    return b"FWSB" + struct.pack("<I", len(sounds)) + b"".join(struct.pack("<5I", *r) for r in records) + bytes(blobs)


def table_delta(clean: bytes, mine: bytes, path: str, shared: list[str], shipped: set[str]) -> bytes:
    (old_hashed, old_named), (new_hashed, new_named) = rb.table_entries(clean), rb.table_entries(mine)
    if new_named != old_named:
        raise Unsupported(f"{path}: Fieldworks changes a named entry")
    if old_hashed.keys() - new_hashed.keys() or any(new_hashed[key] < size for key, size in old_hashed.items()):
        raise Unsupported(f"{path}: Fieldworks removes or lowers an entry")
    # The shared files are always listed: the module replaces their budgets when another copy is loaded.
    listed = {zlib.crc32(name.encode()) for name in shared}
    if any(name in new_named for name in shared) or listed - new_hashed.keys():
        raise Unsupported(f"{path}: a shared file has no plain entry in Fieldworks' table")
    # So is every file Fieldworks ships: another mod's table may give a stock file less than the clean game does.
    listed |= {zlib.crc32(name.encode()) for name in shipped} & new_hashed.keys()
    changed = sorted((key, size) for key, size in new_hashed.items() if old_hashed.get(key) != size or key in listed)
    return b"FWRT" + struct.pack("<I", len(changed)) + b"".join(struct.pack("<II", *pair) for pair in changed)


def plain(raw: bytes) -> bytes:
    return zstandard.ZstdDecompressor().decompress(raw, max_output_size=256 * 1024 * 1024)


def build_set(version: str, clean_root: Path, mine_root: Path, mine_table: Path, output: Path,
              shared_root: Path | None) -> list[dict]:
    z = TotkZstd(clean_root)
    # kind, file pattern, delta maker, headroom in eighths of the loaded size, fixed headroom per delta byte and flat
    plans = [("rows", f"RSDB/{name}.Product.*.rstbl.byml.zs", rows_delta, 8, 4, SLACK) for name in ROW_TABLES]
    plans += [
        ("gamedata", "GameData/GameDataList.Product.*.byml.zs", gamedata_delta, 1, 4, 4 * SLACK),
        ("tag", "RSDB/Tag.Product.*.rstbl.byml.zs", tag_delta, 10, 4, SLACK),
    ]
    # One entry per text language Fieldworks ships (was English only until 2026-11-13).
    languages = sorted({path.name.split(".")[0] for path in mine_root.glob("Mals/*.Product.*.sarc.zs")})
    plans += [("messages", f"Mals/{lang}.Product.*.sarc.zs", mals_delta, 1, 4, SLACK) for lang in languages]
    plans += [
        ("sounds", "Sound/Resource/UI_Static.bars.zs", bank_delta, 0, 1, SLACK),
    ]
    entries = []
    for kind, pattern, make, eighths, per_byte, flat in plans:
        root = mine_root
        rel = only(root, pattern)  # the clean game keeps older versions' copies; Fieldworks ships one
        if rel is None and shared_root is not None and "Product" not in pattern:
            root, rel = shared_root, only(shared_root, pattern)  # one copy serves every game version
        if rel is None:
            continue
        own = root / rel
        if not (clean_root / rel).is_file():
            raise Unsupported(f"{version}: the clean game files have no {rel}")
        packed = own.read_bytes()
        mine = z.decompress(own)
        delta = make(z.decompress(clean_root / rel), mine, f"{version} {rel}")
        if not delta:
            continue
        name = f"{version}/{Path(rel).name[:-3]}.add" if "Product" in pattern else f"{Path(rel).name[:-3]}.add"
        (output / name).parent.mkdir(parents=True, exist_ok=True)
        if "Product" not in pattern and (output / name) in written and (output / name).read_bytes() != delta:
            raise Unsupported(f"{version} {rel}: Fieldworks adds different content on another game version")
        written.add(output / name)
        (output / name).write_bytes(delta)
        fixed = per_byte * len(delta) + flat
        if kind == "sounds":
            fixed += 12 * struct.unpack_from("<I", delta, 4)[0] + 256
        entries.append({"Kind": kind, "Name": rel[:-3], "Delta": f"Fieldworks/Merge/{name}",
                        "Packed": byml.UInt(len(packed)), "Size": byml.UInt(len(mine)),
                        "Eighths": byml.UInt(eighths), "Fixed": byml.UInt(fixed)})
        print(f"  {version} {kind:9} {rel}: {len(delta):,} bytes to add")
    rel = f"System/Resource/{mine_table.name}"
    if not (clean_root / rel).is_file():
        raise Unsupported(f"{version}: the clean game files have no {rel}")
    packed = mine_table.read_bytes()
    mine = plain(packed)
    shipped = set().union(*(compose_romfs.resource_names(root, z, oead) for root in (mine_root, shared_root) if root))
    delta = table_delta(plain((clean_root / rel).read_bytes()), mine, f"{version} {rel}",
                        [entry["Name"] for entry in entries], shipped)
    name = f"{version}/{Path(rel).name[:-3]}.add"
    (output / name).parent.mkdir(parents=True, exist_ok=True)
    (output / name).write_bytes(delta)
    entries.insert(0, {"Kind": "table", "Name": rel[:-3], "Delta": f"Fieldworks/Merge/{name}",
                       "Packed": byml.UInt(len(packed)), "Size": byml.UInt(len(mine)), "Eighths": byml.UInt(0),
                       "Fixed": byml.UInt(2 * len(delta) + 1024)})
    print(f"  {version} table     {rel}: {struct.unpack_from('<I', delta, 4)[0]:,} budgets to add or raise")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--set", nargs=4, action="append", required=True,
                        metavar=("VERSION", "CLEAN", "FIELDWORKS", "TABLE"))
    parser.add_argument("--shared", type=Path, help="Fieldworks files that serve every game version")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    # Start empty, so a file an earlier build wrote and this one does not is never packaged.
    shutil.rmtree(args.output, ignore_errors=True)
    manifest = {}
    try:
        for version, clean, mine, table in args.set:
            manifest[version] = build_set(version, Path(clean), Path(mine), Path(table), args.output, args.shared)
    except Unsupported as error:
        print(f"merge_deltas: {error}", file=sys.stderr)
        return 1
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "manifest.byml").write_bytes(rb.encode_byml(manifest, LITTLE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
