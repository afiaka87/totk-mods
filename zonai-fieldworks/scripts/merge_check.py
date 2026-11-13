"""Check the module's load-time merge code on real game files, outside the game.

Runs the host build of the merge code (tests/merge_tool.cpp) with the spare room the module asks the game
for, and compares the results with files built offline:

  clean file + additions      parses equal to Fieldworks' own file
  other mod's file + additions parses equal to a merged file a boot has proved (--proven, optional)
  Fieldworks' own file, the proved file, and a second pass: unchanged

  uv run --offline --project scripts --with byml --with oead python mods/code/zonai-fieldworks/scripts/merge_check.py
      --tool <merge_tool> --version 121|143 --clean <clean romfs> --fieldworks <Fieldworks romfs for the version>
      --shared <Fieldworks 1.2.1 romfs> --table <Fieldworks' size table .zs> --other <other mod's romfs>
      [--proven <merged romfs>] --merge-data <romfs>/Fieldworks/Merge --work <scratch folder>

The game files, the other mod and the proved files are local test inputs; none is kept in the repository.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(REPO / "scripts"), str(REPO / "mods/code/botw-abilities/tools"),
                str(REPO / "framework/bridgefs/tools")]

BUDGET_MARGIN = 65536  # src/fieldworks/SharedFiles.cpp kBudgetMargin


def planned(entry, unpacked: int) -> int:
    """src/pure/merge/Manifest.hpp plannedBytes."""
    return -(-(unpacked + unpacked * int(entry["Eighths"]) // 8 + int(entry["Fixed"])) // 32) * 32


def main() -> int:
    import byml
    import zstandard
    from totk_mod_tools.zstd_helper import TotkZstd
    from totk_version_port import romfs_rebase as rb

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("tool", "clean", "fieldworks", "shared", "table", "other", "merge-data", "work"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--proven", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    z = TotkZstd(args.clean)
    manifest = byml.Byml((args.merge_data / "manifest.byml").read_bytes()).parse()[args.version]
    args.work.mkdir(parents=True, exist_ok=True)
    bad = 0

    def parsed(kind, raw):
        if kind in ("rows", "gamedata", "tag"):
            return byml.Byml(raw).parse()
        return rb._sarc(raw) if kind == "messages" else raw

    def tagged(raw):
        """Each actor of a tag table with its set of tags, whatever the row order."""
        table = byml.Byml(raw).parse()
        paths, tags, bits = table["PathList"], [str(t) for t in table["TagList"]], bytes(table["BitTable"])
        out = {}
        for row in range(len(paths) // 3):
            at = [row * len(tags) + i for i in range(len(tags))]
            out[tuple(str(x) for x in paths[3 * row:3 * row + 3])] = frozenset(
                tags[i] for i, bit in enumerate(at) if bits[bit // 8] >> (bit % 8) & 1)
        return out, tags

    def run(kind, label, source, delta, entry, extra=()):
        src, out = args.work / f"{kind}.{label}.in", args.work / f"{kind}.{label}.out"
        src.write_bytes(source)
        out.unlink(missing_ok=True)
        spare = planned(entry, len(source)) - len(source)
        r = subprocess.run([str(args.tool), kind, str(src), str(delta), str(out), str(spare), *extra],
                           capture_output=True, text=True)
        print(f"    {label:7} {r.stdout.strip()} {r.stderr.strip()}")
        return (out.read_bytes() if out.is_file() else b""), r.returncode

    def verdict(text: str, ok: bool) -> None:
        nonlocal bad
        bad += not ok
        print(f"      {text}: {ok}")

    foreign = []
    for entry in manifest[1:]:
        kind, rel = str(entry["Kind"]), str(entry["Name"]) + ".zs"
        delta = args.merge_data / str(entry["Delta"]).split("Fieldworks/Merge/", 1)[1]
        root = args.fieldworks if (args.fieldworks / rel).is_file() else args.shared
        clean, mine, other = (z.decompress(base / rel) for base in (args.clean, root, args.other))
        print(f"{args.version} {kind} {rel} additions={delta.stat().st_size:,}")
        verdict("the list's sizes are Fieldworks' own file's",
                len(mine) == int(entry["Size"]) and (root / rel).stat().st_size == int(entry["Packed"]))
        got, code = run(kind, "clean", clean, delta, entry)
        same = code == 0 and parsed(kind, got) == parsed(kind, mine)
        if not same and kind == "tag" and code == 0 and tagged(got) == tagged(mine):
            # Fieldworks' own 1.2.1 tag file keeps its new actors one place off sorted order.
            print("      (row order differs from Fieldworks' file; same actors with the same tags)")
            same = True
        verdict(f"clean + additions == Fieldworks' file (bytes equal: {got == mine})", same)
        got, code = run(kind, "other", other, delta, entry)
        verdict(f"other + additions merged ({len(other):,} -> {len(got):,} of {planned(entry, len(other)):,})", code == 0)
        unchanged = [("own", mine)]
        if args.proven:
            proven = z.decompress(args.proven / rel)
            verdict(f"other + additions == the proved merge (bytes equal: {got == proven})",
                    parsed(kind, got) == parsed(kind, proven))
            unchanged.append(("proved", proven))
        for label, raw in unchanged:
            again, code = run(kind, label, raw, delta, entry)
            verdict(f"{label} file unchanged", code == 0 and again == raw)
        twice, code = run(kind, "twice", got, delta, entry)
        verdict("second pass changes nothing", code == 0 and twice == got)
        if (args.other / rel).stat().st_size != int(entry["Packed"]) or len(other) != len(mine):
            foreign.append((entry, len(other)))

    plain = lambda path: zstandard.ZstdDecompressor().decompress(path.read_bytes(), max_output_size=1 << 28)  # noqa: E731
    entry = manifest[0]
    rel = str(entry["Name"]) + ".zs"
    delta = args.merge_data / str(entry["Delta"]).split("Fieldworks/Merge/", 1)[1]
    clean, mine, other = plain(args.clean / rel), plain(args.table), plain(args.other / rel)
    print(f"{args.version} table {rel}")
    verdict("the list's sizes are Fieldworks' own table's",
            len(mine) == int(entry["Size"]) and args.table.stat().st_size == int(entry["Packed"]))
    got, code = run("table", "clean", clean, delta, entry)
    verdict("clean + additions == Fieldworks' table, byte for byte", code == 0 and got == mine)
    again, code = run("table", "own", mine, delta, entry)
    verdict("own table unchanged", code == 0 and again == mine)
    (other_hashed, other_named), (mine_hashed, _), (clean_hashed, _) = (rb.table_entries(t) for t in (other, mine, clean))
    # The budgets the module plans for the files that are the other mod's (SharedFiles.cpp loadShared).
    budgets = {}
    for shared, unpacked in foreign:
        name = str(shared["Name"])
        had = other_named.get(name, other_hashed.get(zlib.crc32(name.encode()), 0))
        budgets[name] = planned(shared, unpacked) + max(had - unpacked, 0) + BUDGET_MARGIN
    extra = [f"{name}={size}" for name, size in budgets.items()]
    got, code = run("table", "other", other, delta, entry, extra)
    got_hashed, got_named = rb.table_entries(got)
    want = dict(other_hashed)
    for key, size in mine_hashed.items():
        if clean_hashed.get(key) != size:
            want[key] = max(size, want.get(key, 0))
    for name, size in budgets.items():
        want[zlib.crc32(name.encode())] = max(other_hashed.get(zlib.crc32(name.encode()), 0), size)
    verdict(f"other + additions: every budget as planned ({len(other_hashed):,} -> {len(got_hashed):,} entries)",
            code == 0 and got_hashed == want and got_named == other_named)
    verdict("sorted", list(got_hashed) == sorted(got_hashed))
    twice, code = run("table", "twice", got, delta, entry, extra)
    verdict("second pass changes nothing", code == 0 and twice == got)
    for shared, unpacked in foreign:
        name = str(shared["Name"])
        key = zlib.crc32(name.encode())
        print(f"    {name}: unpacked {unpacked:,} asks {planned(shared, unpacked):,} budget clean "
              f"{clean_hashed.get(key, 0):,} other {other_hashed.get(key, 0):,} Fieldworks {mine_hashed.get(key, 0):,} "
              f"-> {got_hashed.get(key, 0):,}")
    if args.proven:
        proven_hashed, _ = rb.table_entries(plain(args.proven / rel))
        skip = {zlib.crc32(str(e["Name"]).encode()) for e in manifest[1:]}
        low = [key for key, size in proven_hashed.items() if got_hashed.get(key, 0) < size and key not in skip]
        risky = [key for key in low if mine_hashed.get(key, 0) != clean_hashed.get(key, 0)]
        print(f"    budgets below the proved table, shared files aside: {len(low):,}")
        verdict("none of them is a budget Fieldworks raises", not risky)
    print("FAILURES", bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
