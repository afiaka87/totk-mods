# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Compose Zonai Fieldworks' game files from each member's accepted standalone capture (design 06, §7).

`compose` writes <output>/romfs, <output>/restbl-forces.tsv and <output>/compose-receipt.json:
- every member's RomFS files, except its own resource-size table;
- the files Bivouac and BotW Abilities both replace, merged against the clean 1.2.1 files with BotW
  Abilities' merge code, which refuses two members changing the same value differently (a text
  language only 1.4.3 has merges against the clean 1.4.3 archive, into <output>/romfs143-extra);
- the eight wheel glyph icons;
- parse-budget floors: every budget a member's own table raises, the merged files' floors and the
  icons' floors.

`budgets` rebuilds the table the way the deploy does and confirms that it covers every budget each
member's own table asks for. `check` reports members whose newest capture of the same build is not
the pinned artifact.

`port143` carries the composed files whose names carry a game version, and the
release table, to the names TotK 1.4.3 reads, into <output>/romfs143. A member's own 1.4.3 copies of
those files are left out of `compose`: the carry runs once, on the merged files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tomllib
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
MOD = HERE.parent
REPO = MOD.parents[2]
BOTW_TOOLS = REPO / "mods/code/botw-abilities/tools"
sys.path[:0] = [str(REPO / "scripts"), str(BOTW_TOOLS)]

RESOURCE_TABLE = "System/Resource/ResourceSizeTable.Product.121.rsizetable.zs"
GAMEDATA = "GameData/GameDataList.Product.110.byml.zs"
TABLES = ("RSDB/ActorInfo.Product.121.rstbl.byml.zs", "RSDB/GameActorInfo.Product.121.rstbl.byml.zs")


def is_mals(path: str) -> bool:
    """A 1.2.1 text archive in any language (was English only until 2026-11-13)."""
    return path.startswith("Mals/") and path.endswith(".Product.121.sarc.zs")


ICON_TEMPLATE_RESOURCE = "UI/Tex/Icon/Obj_Camera.bntx"
# Texture name (and file stem) per glyph. The template bank's name entry holds at most 13 characters.
GLYPHS = {
    "FwPhantom": "phantom-foothold",
    "FwHookshot": "zonai-hookshot",
    "FwFleetSync": "fleet-sync",
    "FwColumns": "material-columns",
    "FwAbilities": "botw-abilities",
    "FwBivouac": "bivouac",
    "FwSelfRecall": "self-recall",
    "FwSurvey": "zonai-survey",
}
PAYLOAD_TOPS = {"exefs", "romfs", "cheats"}


def ported_names() -> dict[str, str]:
    """1.4.3 name -> 1.2.1 name of every file the carry step writes from a 1.2.1 file."""
    from totk_version_port import romfs_rebase as carry

    names = {new: old for old, new in carry.NAMES.items()}
    names.update({new: old for old, (new, _) in carry.REDIRECTS.items()})
    return names


def carried_only() -> set[str]:
    """1.4.3 files with no 1.2.1 file of their own: the edited name table and the 1.4.3 resource table."""
    from totk_version_port import romfs_rebase as carry

    return {carry.ADDRESS_TABLE, carry.TABLE_143}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_members() -> list[dict]:
    members = tomllib.loads((MOD / "members.toml").read_text(encoding="utf-8"))["member"]
    if len({member["slug"] for member in members}) != len(members):
        raise ValueError("members.toml names a member twice")
    return members


def artifact_root(argument: Path | None) -> Path:
    if argument:
        return argument
    from totk_mod_tools import paths

    return paths.artifact_root()


def table_entries(raw: bytes) -> tuple[dict[int, int], dict[str, int]]:
    """A resource-size table's hashed and named budgets."""
    if raw[:6] != b"RESTBL":
        raise ValueError("expected a resource-size table")
    width, count, overflow = struct.unpack_from("<III", raw, 10)
    hashed = dict(struct.unpack_from("<II", raw, 22 + index * 8) for index in range(count))
    named = {}
    for index in range(overflow):
        start = 22 + count * 8 + index * (width + 4)
        named[raw[start : start + width].split(b"\0", 1)[0].decode()] = struct.unpack_from(
            "<I", raw, start + width)[0]
    return hashed, named


def raised(member: bytes, clean: bytes) -> tuple[dict[int, int], dict[str, int]]:
    """Budgets a member's own table raises above the clean table, or adds."""
    (member_hashed, member_named), (clean_hashed, clean_named) = table_entries(member), table_entries(clean)
    return ({key: size for key, size in member_hashed.items() if size > clean_hashed.get(key, 0)},
            {key: size for key, size in member_named.items() if size > clean_named.get(key, 0)})


def resource_names(romfs: Path, z, oead) -> set[str]:
    """Resource paths of every file under a RomFS tree, including the files inside its archives."""
    names: set[str] = set()

    def inner(raw: bytes) -> None:
        if raw[:4] == b"SARC":
            for entry in oead.Sarc(raw).get_files():
                names.add(entry.name)
                inner(bytes(entry.data))

    for path in romfs.rglob("*"):
        if path.is_file():
            names.add(path.relative_to(romfs).as_posix().removesuffix(".zs"))
            inner(z.decompress(path) if path.name.endswith(".zs") else path.read_bytes())
    return names | {name.removesuffix(".mc") for name in names}


def merge_mals_exact(clean: bytes, members: dict[str, bytes]):
    """Merge whole Mals archives; a message file only one member changed keeps that member's bytes."""
    from resource_merge import agree, merge_msbt, sarc_files, write_sarc

    base = sarc_files(clean)
    archives = {name: sarc_files(raw) for name, raw in members.items()}
    if any(base.keys() - files.keys() for files in archives.values()):
        raise ValueError("a member's Mals archive removes stock files")
    merged, owners = dict(base), {}
    for path in sorted(set(base).union(*(files.keys() for files in archives.values()))):
        edits = {name: files[path] for name, files in archives.items()
                 if path in files and files[path] != base.get(path)}
        if not edits:
            continue
        if len(edits) == 1:
            merged[path] = next(iter(edits.values()))
        elif path in base and path.endswith(".msbt"):
            merged[path] = merge_msbt(base[path], edits)
        else:
            merged[path] = agree(edits, path)
        owners[path] = sorted(edits)
    return write_sarc(merged), owners


def merge_shared(path: str, clean: bytes, members: dict[str, bytes]):
    """Merge one file several members replace; returns the bytes and what each member added."""
    import byml
    from compose_resources import merge_table
    from resource_merge import merge_gamedata, sarc_files

    if path == GAMEDATA:
        raw = merge_gamedata(clean, members)
        for name, value in members.items():
            alone = byml.Byml(merge_gamedata(clean, {name: value})).parse()
            if alone != byml.Byml(value).parse():
                raise ValueError(f"{path}: merging {name} alone does not reproduce its GameData")
        return raw, {name: sha256(value) for name, value in members.items()}
    if path in TABLES:
        raw, added = merge_table(clean, members)
        rows = {row["__RowId"]: row for row in byml.Byml(raw).parse()}
        for name, value in members.items():
            mine = byml.Byml(value).parse()
            if any(rows.get(row["__RowId"]) != row for row in mine):
                raise ValueError(f"{path}: the merged table loses a row {name} wrote")
            if [row["__RowId"] for row in mine] != sorted(row["__RowId"] for row in mine):
                raise ValueError(f"{path}: {name}'s rows are not in the sorted order the merge writes")
        return raw, added
    if is_mals(path):
        raw, owners = merge_mals_exact(clean, members)
        files = sarc_files(raw)
        for name, value in members.items():
            for inner, data in sarc_files(value).items():
                if owners.get(inner) == [name] and files[inner] != data:
                    raise ValueError(f"{path}: {inner} from {name} changed in the merge")
        return raw, {inner: names for inner, names in owners.items()}
    raise ValueError(f"{path} is replaced by {sorted(members)} and has no merge")


# The glyph fills this share of the area BotW Abilities' wheel art fills (was 1.0 until 2026-11-11).
GLYPH_SCALE = 0.75
# Members whose glyph reads larger than the rest at the common scale (HanSoloBlaster, 2026-11-11 boot).
GLYPH_SCALES = {"zonai-hookshot": 0.66}


def glyph_rgba(path: Path) -> bytes:
    """Fit a glyph into the wheel icon the way BotW Abilities fits its art, keeping the glyph's colours."""
    import numpy as np
    from PIL import Image
    from wolf_icon import MARGIN, SIZE, STRAY_ALPHA

    pixels = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255
    pixels[..., 3] = np.where(pixels[..., 3] < STRAY_ALPHA, 0.0, pixels[..., 3])
    ys, xs = np.nonzero(pixels[..., 3] > 0.02)
    cropped = pixels[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1].copy()
    cropped[..., :3] *= cropped[..., 3:4]  # premultiply, so the resize does not darken the edges
    side = max(cropped.shape[:2])
    square = np.zeros((side, side, 4), np.float32)
    y, x = (side - cropped.shape[0]) // 2, (side - cropped.shape[1]) // 2
    square[y : y + cropped.shape[0], x : x + cropped.shape[1]] = cropped
    inner = round((SIZE - 2 * MARGIN) * GLYPH_SCALES.get(path.stem, GLYPH_SCALE))
    margin = (SIZE - inner) // 2
    small = np.stack([np.asarray(Image.fromarray(square[..., channel], "F").resize((inner, inner), Image.LANCZOS))
                      for channel in range(4)], axis=-1)
    small = np.clip(small, 0, 1)
    alpha = small[..., 3:4]
    small[..., :3] = np.where(alpha > 1e-4, np.clip(small[..., :3] / np.maximum(alpha, 1e-4), 0, 1), 0)
    output = np.zeros((SIZE, SIZE, 4), np.float32)
    output[margin : margin + inner, margin : margin + inner] = small
    return (output * 255 + 0.5).astype(np.uint8).tobytes()


def build_icons(clean_romfs: Path, z, target_root: Path) -> dict[str, dict]:
    import astc_encoder as astc
    from build_wheel_icons import ASTC_SRGB, SIZE, TEMPLATE, TEMPLATE_NAME, encode, icon_bank
    from totk_mod_tools.textures.bntx import Bank

    template = z.decompress(clean_romfs / TEMPLATE)
    info = Bank(template).textures[0]
    if (info.name, info.width, info.height, info.format_id, info.mip_count) != (
            TEMPLATE_NAME, SIZE, SIZE, ASTC_SRGB, 1):
        raise ValueError(f"unexpected icon template {info}")
    icons = {}
    for name, member in GLYPHS.items():
        source = MOD / "assets/glyphs" / f"{member}.png"
        rgba = glyph_rgba(source)
        blocks, decoded = encode(rgba, astc)
        error = max(abs(a - b) for a, b in zip(rgba, decoded, strict=True))
        bank = bytes(icon_bank(template, blocks, name))
        target = target_root / f"UI/Tex/Icon/{name}.bntx.zs"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(bank, target.name))
        if z.decompress(target) != bank:
            raise ValueError(f"{name}: the compressed icon does not round-trip")
        icons[f"UI/Tex/Icon/{name}.bntx.zs"] = {"glyph": source.relative_to(REPO).as_posix(),
                                                "bntx_bytes": len(bank), "astc_max_channel_error": error}
    return icons


def member_payloads(root: Path, members: list[dict]):
    """Each member's retained payload, after checking that it still matches its artifact identity."""
    from totk_mod_tools.artifact_store import tree_id

    for member in members:
        payload = root / "objects/sha256" / member["artifact"].removeprefix("sha256:") / "payload"
        if not payload.is_dir():
            raise ValueError(f"{member['slug']}: artifact {member['artifact']} is not in {root}")
        if tree_id(payload) != member["artifact"]:
            raise ValueError(f"{member['slug']}: the retained payload does not match its artifact identity")
        tops = {path.name for path in payload.iterdir()}
        if tops - PAYLOAD_TOPS:
            raise ValueError(f"{member['slug']}: unexpected payload folders {sorted(tops - PAYLOAD_TOPS)}")
        yield member, payload


def read_forces(path: Path) -> dict[str, int]:
    forces = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            resource, size = line.split("\t")
            forces[resource] = int(size)
    return forces


def budgets(args) -> int:
    """Rebuild the table the way the deploy does and compare it with every member's own table.

    The deploy adds to the shared table already in the emulator, which only grows. BotW Abilities' own
    table carries budgets whose resource names no shipped file or floor gives; they reach the shared table
    through its earlier deploys, so pass --shared-table to start from that table as the deploy does.
    """
    from totk_mod_tools.restbl_floors import grow_restbl
    from totk_mod_tools.zstd_helper import TotkZstd

    z = TotkZstd(args.romfs)
    clean_table = z.decompress(args.romfs / RESOURCE_TABLE)
    work = args.output / "restbl-check"
    shutil.rmtree(work, ignore_errors=True)
    (work / RESOURCE_TABLE).parent.mkdir(parents=True)
    if args.shared_table:
        shutil.copyfile(args.shared_table, work / RESOURCE_TABLE)
    subprocess.run([str(args.file_tool), "--restbl-version", "121", "restbl-rebuild", str(args.output / "romfs"),
                    "--rstb-romfs", str(work)], check=True, stdout=subprocess.DEVNULL)
    hashed, named = table_entries(grow_restbl(z.decompress(work / RESOURCE_TABLE),
                                              read_forces(args.output / "restbl-forces.tsv")))
    failed = 0
    for member, payload in member_payloads(artifact_root(args.artifacts), read_members()):
        table = payload / "romfs" / RESOURCE_TABLE
        if not table.is_file():
            continue
        member_hashed, member_named = raised(z.decompress(table), clean_table)
        short = [f"{key:08x}" for key, size in member_hashed.items() if hashed.get(key, 0) < size]
        short += [key for key, size in member_named.items()
                  if named.get(key, hashed.get(zlib.crc32(key.encode()), 0)) < size]
        print(f"{member['slug']}: {len(member_hashed) + len(member_named)} own budgets, {len(short)} not covered"
              + (f" {short[:8]}" if short else ""))
        failed += bool(short)
    print("start table: " + (str(args.shared_table) if args.shared_table else "clean 1.2.1"))
    return 1 if failed else 0


def raise_entries(raw: bytes, hashed: dict[int, int], named: dict[str, int]) -> bytes:
    """A table with the given hashed and named budgets raised to at least the given sizes."""
    width, count, overflow = struct.unpack_from("<III", raw, 10)
    entries = dict(struct.unpack_from("<II", raw, 22 + index * 8) for index in range(count))
    names = bytearray(raw[22 + count * 8:])
    seen = set()
    for index in range(overflow):
        start = index * (width + 4)
        name = bytes(names[start : start + width]).split(b"\0", 1)[0].decode()
        seen.add(name)
        if name in named:
            size = max(named[name], struct.unpack_from("<I", names, start + width)[0])
            struct.pack_into("<I", names, start + width, size)
    for key, size in hashed.items():
        entries[key] = max(size, entries.get(key, 0))
    for name in named.keys() - seen:  # a name the clean table hashes: no collision, so its hash entry serves
        key = zlib.crc32(name.encode())
        entries[key] = max(named[name], entries.get(key, 0))
    header = bytearray(raw[:22])
    struct.pack_into("<I", header, 14, len(entries))
    return bytes(header) + b"".join(struct.pack("<II", *item) for item in sorted(entries.items())) + bytes(names)


def table(args) -> int:
    """Build the resource-size table that ships in the package (design 06, section 7).

    Start from the clean table, size every composed file, apply the floors, then raise every budget that a
    member's own released table raises. Other players do not have the developer's shared table add-on.
    """
    import zstandard
    from totk_mod_tools.restbl_floors import grow_restbl
    from totk_mod_tools.zstd_helper import TotkZstd

    z = TotkZstd(args.romfs)
    clean_table = z.decompress(args.romfs / RESOURCE_TABLE)
    work, out = args.output / "release-restbl-work", args.output / "release-restbl" / RESOURCE_TABLE
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    subprocess.run([str(args.file_tool), "--restbl-version", "121", "restbl-rebuild", str(args.output / "romfs"),
                    "--rstb-romfs", str(work)], check=True, stdout=subprocess.DEVNULL)
    raw = grow_restbl(z.decompress(work / RESOURCE_TABLE), read_forces(args.output / "restbl-forces.tsv"))
    lines = []
    for member, payload in member_payloads(artifact_root(args.artifacts), read_members()):
        source = payload / "romfs" / RESOURCE_TABLE
        if source.is_file():
            hashed, named = raised(z.decompress(source), clean_table)
            raw = raise_entries(raw, hashed, named)
            lines.append(f"{member['slug']}: {len(hashed) + len(named)} own budgets")
    final_hashed, final_named = table_entries(raw)
    clean_hashed, clean_named = table_entries(clean_table)
    lowered = [key for key, size in clean_hashed.items() if final_hashed.get(key, 0) < size]
    lowered += [key for key, size in clean_named.items() if final_named.get(key, 0) < size]
    if lowered:
        raise ValueError(f"the release table lowers {len(lowered)} clean budgets")
    packed = zstandard.ZstdCompressor(level=16).compress(raw)  # plain zstd: the table is read with no dictionary
    if zstandard.ZstdDecompressor().decompress(packed) != raw:
        raise ValueError("release table compression readback failed")
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.is_file() or zstandard.ZstdDecompressor().decompress(out.read_bytes()) != raw:
        out.write_bytes(packed)
    changed = sum(size != clean_hashed.get(key) for key, size in final_hashed.items()) + sum(
        size != clean_named.get(key) for key, size in final_named.items())
    print(f"[fieldworks] release resource table: {changed} budgets differ from clean; " + "; ".join(lines))
    return 0


def compose(args) -> int:
    import oead
    from totk_mod_tools.zstd_helper import TotkZstd

    root = artifact_root(args.artifacts)
    members = read_members()
    inputs = hashlib.sha256()
    for path in (Path(__file__), BOTW_TOOLS / "resource_merge.py", BOTW_TOOLS / "compose_resources.py",
                 BOTW_TOOLS / "build_wheel_icons.py", BOTW_TOOLS / "wolf_icon.py", MOD / "members.toml",
                 args.romfs / RESOURCE_TABLE, *(MOD / "assets/glyphs" / f"{m}.png" for m in GLYPHS.values())):
        inputs.update(path.read_bytes())
    digest = inputs.hexdigest()
    romfs_out, receipt_path, forces_path = args.output / "romfs", args.output / "compose-receipt.json", \
        args.output / "restbl-forces.tsv"
    extra_out = args.output / "romfs143-extra" / "romfs"  # 1.4.3-only files: `port143` writes them as given
    if receipt_path.is_file() and forces_path.is_file():
        previous = json.loads(receipt_path.read_text(encoding="utf-8"))
        if previous.get("inputs_sha256") == digest and all(
                (romfs_out / path).is_file() and sha256((romfs_out / path).read_bytes()) == value
                for path, value in previous["files"].items()) and all(
                (extra_out / path).is_file() and sha256((extra_out / path).read_bytes()) == value
                for path, value in previous.get("extra143", {}).items()):
            print("[fieldworks] game files unchanged")
            return 0

    z = TotkZstd(args.romfs)
    from compose_resources import stock_budgets

    clean_table = z.decompress(args.romfs / RESOURCE_TABLE)
    budget = stock_budgets(clean_table)
    owners: dict[str, list[tuple[str, Path]]] = {}
    member_tables: dict[str, bytes] = {}
    forces: dict[str, int] = {}
    unnamed: dict[str, int] = {}
    ported, carried, left_out = ported_names(), carried_only(), {}
    from totk_version_port import romfs_rebase as carry
    extras: dict[str, list[tuple[str, Path]]] = {}
    for member, payload in member_payloads(root, members):
        romfs = payload / "romfs"
        if not romfs.is_dir():
            continue
        shipped = {path.relative_to(romfs).as_posix() for path in romfs.rglob("*") if path.is_file()}
        for source in sorted(path for path in romfs.rglob("*") if path.is_file()):
            path = source.relative_to(romfs).as_posix()
            if path == RESOURCE_TABLE:
                member_tables[member["slug"]] = z.decompress(source)
                continue
            # A member's 1.4.3 copy of a file it also ships for 1.2.1: `port143` carries the merged file.
            if path in carried or (path in ported and ported[path] in shipped):
                left_out.setdefault(member["slug"], []).append(path)
                continue
            # A text language only 1.4.3 has (Portuguese): no 1.2.1 file to merge, so several members' copies
            # merge against the clean 1.4.3 one (was one member only until 2026-11-13).
            if carry.EXTRA_143.fullmatch(path) and path not in ported:
                extras.setdefault(path, []).append((member["slug"], source))
                continue
            if path in ported:
                raise ValueError(f"{member['slug']}: {path} has no 1.2.1 file to carry it from")
            owners.setdefault(path, []).append((member["slug"], source))
        if member["slug"] in member_tables:
            hashed, named = raised(member_tables[member["slug"]], clean_table)
            names = {zlib.crc32(name.encode()): name for name in resource_names(romfs, z, oead)}
            for key, size in [(names[key], size) for key, size in hashed.items() if key in names] + list(named.items()):
                forces[key] = max(forces.get(key, 0), size)
            unnamed[member["slug"]] = sum(key not in names for key in hashed)

    shutil.rmtree(romfs_out, ignore_errors=True)
    shutil.rmtree(extra_out.parent, ignore_errors=True)
    extra143, files, merges = {}, {}, {}
    for path, sources in sorted(extras.items()):
        target = extra_out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        if len(sources) == 1:
            shutil.copyfile(sources[0][1], target)
        else:
            if not args.romfs143:
                raise ValueError(f"{path} is shipped by {[name for name, _ in sources]}: merging it needs --romfs143")
            z143 = TotkZstd(args.romfs143)
            raw, owners143 = merge_mals_exact(z143.decompress(args.romfs143 / path),
                                              {name: z143.decompress(source) for name, source in sources})
            target.write_bytes(z143.compress(raw, target.name))
            if z143.decompress(target) != raw:
                raise ValueError(f"{path} does not decompress to the merged bytes")
            merges[path] = {"members": [name for name, _ in sources], "added": owners143, "raw_bytes": len(raw)}
        extra143[path] = sha256(target.read_bytes())
    for path, sources in sorted(owners.items()):
        target = romfs_out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        if len(sources) == 1:
            shutil.copyfile(sources[0][1], target)
        else:
            clean = z.decompress(args.romfs / path)
            raw, added = merge_shared(path, clean, {name: z.decompress(source) for name, source in sources})
            target.write_bytes(z.compress(raw, target.name))
            if z.decompress(target) != raw:
                raise ValueError(f"{path} does not decompress to the merged bytes")
            resource = path.removesuffix(".zs")
            growth = len(raw) - len(clean)
            if path in TABLES:
                floor = max(budget(resource), len(raw)) + max(4096, growth * 10)
            elif is_mals(path):
                floor = max(budget(resource), len(raw)) + max(65536, growth * 4)
            else:
                floor = max(budget(resource), len(raw)) + max(65536, growth * 10)
            forces[resource] = max(forces.get(resource, 0), floor)
            merges[path] = {"members": [name for name, _ in sources], "added": added, "raw_bytes": len(raw),
                            "budget": forces[resource]}
        files[path] = sha256(target.read_bytes())
    icons = build_icons(args.romfs, z, romfs_out)
    for path in icons:
        if path in owners:
            raise ValueError(f"{path} is also shipped by {[name for name, _ in owners[path]]}")
        files[path] = sha256((romfs_out / path).read_bytes())
        forces[path.removesuffix(".zs")] = max(budget(ICON_TEMPLATE_RESOURCE), icons[path]["bntx_bytes"] + 4096)

    header = "# Zonai Fieldworks parse-budget floors: every member's own budgets, merged files and wheel icons.\n"
    forces_path.write_text(header + "".join(f"{resource}\t{size}\n" for resource, size in sorted(forces.items())),
                           encoding="utf-8", newline="\n")
    receipt = {"inputs_sha256": digest,
               "members": {member["slug"]: member["artifact"] for member in members},
               "files": files, "extra143": extra143, "merges": merges, "icons": icons, "forces": len(forces),
               "member_budgets_without_a_resource_name": unnamed,
               "member_143_files_left_to_port143": left_out}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"[fieldworks] {len(files)} game files, {len(merges)} merged, {len(icons)} icons, "
          f"{len(forces)} budget floors; member budgets without a resource name: {unnamed}")
    return 0


def port143(args) -> int:
    """Carry the composed files and the release table to the names TotK 1.4.3 reads (<output>/romfs143)."""
    from totk_version_port import romfs_rebase as carry

    try:
        receipt = carry.run(args.output / "romfs", args.romfs, args.romfs143, args.output / "romfs143",
                            converter=args.xlink, sound_names=args.xlink_names,
                            table121=args.output / "release-restbl" / RESOURCE_TABLE,
                            extra=args.output / "romfs143-extra")
    except carry.RebaseError as error:
        print(f"[fieldworks] 1.4.3 game files FAILED: {error}")
        return 1
    print(f"[fieldworks] 1.4.3 game files: {len(receipt['files'])} written, "
          f"{receipt['resource_table']['raised']} budgets raised in the 1.4.3 table")
    return 0


def check(args) -> int:
    """Report members whose newest capture of the pinned build is a different artifact."""
    root = artifact_root(args.artifacts)
    stale = 0
    for member in read_members():
        newest = None
        captures = root / "captures" / member["slug"]
        receipts = [*captures.glob("v*/*/receipt.json"), *captures.glob("diagnostics/*/*/receipt.json")]
        for receipt_path in receipts:
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            if receipt.get("build", {}).get("binary_directory_identity") != member["build"]:
                continue
            if newest is None or receipt["captured_at"] > newest["captured_at"]:
                newest = receipt
        if newest is None:
            print(f"{member['slug']}: no capture of {member['build']} found")
            stale += 1
        elif newest["artifact"]["id"] != member["artifact"]:
            print(f"{member['slug']}: pinned {member['version']} {member['artifact'][:19]}, newest capture "
                  f"{newest['version']} {newest['artifact']['id'][:19]} ({newest['captured_at'][:16]})")
            stale += 1
        else:
            print(f"{member['slug']}: {member['version']} is the newest capture")
    return 1 if stale else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--artifacts", type=Path, help="artifact store (default: TOTK_ARTIFACT_ROOT or .artifacts)")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("compose")
    build.add_argument("--romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    build.add_argument("--output", type=Path, required=True, help="build directory")
    build.add_argument("--romfs143", type=Path, help="clean TotK 1.4.3 files, to merge a 1.4.3-only text language "
                                                     "several members ship")
    check_budgets = commands.add_parser("budgets")
    check_budgets.add_argument("--romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    check_budgets.add_argument("--output", type=Path, required=True, help="build directory")
    check_budgets.add_argument("--file-tool", type=Path, required=True)
    check_budgets.add_argument("--shared-table", type=Path, help="the emulator's shared resource-size table")
    release_table = commands.add_parser("table")
    release_table.add_argument("--romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    release_table.add_argument("--output", type=Path, required=True, help="build directory")
    release_table.add_argument("--file-tool", type=Path, required=True)
    carry = commands.add_parser("port143")
    carry.add_argument("--romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    carry.add_argument("--romfs143", type=Path, required=True, help="clean TotK 1.4.3 files")
    carry.add_argument("--output", type=Path, required=True, help="build directory")
    carry.add_argument("--xlink", type=Path, required=True, help="the reviewed sound-link converter")
    carry.add_argument("--xlink-names", type=Path, required=True, help="the converter's user-name list")
    commands.add_parser("check")
    args = parser.parse_args()
    return {"compose": compose, "budgets": budgets, "table": table, "port143": port143,
            "check": check}[args.command](args)


if __name__ == "__main__":
    raise SystemExit(main())
