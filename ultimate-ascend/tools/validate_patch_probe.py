"""Exercise the actual offline layout and rejection paths against the compact object."""
from __future__ import annotations

import argparse
import copy
import json
import struct
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("object", "inputs", "out", "tool-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--complete", action="store_true")
    parser.add_argument("--rejections-only", action="store_true", help="Reuse the successful build from the orchestrator")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    tool = Path(__file__).with_name("layout_patch_probe.py")
    inputs = json.loads(args.inputs.read_text())
    results = []

    def run(name: str, inventory: dict, error: str | None = None,
            object_path: Path = args.object, extra: tuple[str, ...] = ()) -> None:
        case = args.out / name
        case.mkdir(exist_ok=True)
        inventory_path = case / "inputs.json"
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        result = subprocess.run([sys.executable, str(tool), "--object", str(object_path),
                                 "--inputs", str(inventory_path), "--out", str(case),
                                 "--tool-root", str(args.tool_root),
                                 *(["--complete"] if args.complete else []), *extra],
                                text=True, capture_output=True, check=False)
        (case / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if error is None:
            if result.returncode:
                raise AssertionError(f"{name} failed; see {case / 'run.log'}")
        elif result.returncode == 0 or error not in result.stderr:
            raise AssertionError(f"{name} did not reject the expected error; see {case / 'run.log'}")
        results.append({"case": name, "result": "pass", "expected_rejection": error})

    if not args.rejections_only:
        run("complete-placement", inputs)
    modified = copy.deepcopy(inputs)
    modified["hooks"][0]["conflicts"] = [[0, 4, "fixture/conflict", "fixture"]]
    run("hook-overlap", modified, "A hook overlaps a known patch")
    modified = copy.deepcopy(inputs)
    anchor = int(inputs["anchor"], 16)
    modified["tail_claims"].append([anchor, anchor + 4, "fixture/conflict", "fixture"])
    run("anchor-overlap", modified, "Proposed anchor is referenced or claimed")
    modified = copy.deepcopy(inputs)
    modified["native_rw"]["mapped_end"] = hex(anchor)
    run("unmapped-anchor", modified, "Anchor lies outside the verified writable padding")
    modified = copy.deepcopy(inputs)
    modified["gaps"] = modified["gaps"][:1]
    run("space-exhaustion", modified, "Ran out of")
    if args.complete:
        modified = copy.deepcopy(inputs)
        modified["gaps"][0] = [4, 12]
        run("invalid-gap", modified, "outside the chosen alignment policy")
        modified = copy.deepcopy(inputs)
        modified["hooks"][0]["original_words"][1] = 0xd503201f
        run("corrupted-replay", modified, "Original replay prefix differs from clean game")
    # Replace an unrelocated RET with SVC. This checks the instruction safety gate, not just a hand-authored toy string passed to its implementation.
    corrupted = bytearray(args.object.read_bytes())
    with args.object.open("rb") as stream:
        elf = ELFFile(stream)
        candidate = next((s["sh_offset"] + offset for s in elf.iter_sections() if s["sh_flags"] & 4
                          for offset in range(0, s["sh_size"], 4)
                          if struct.unpack_from("<I", corrupted, s["sh_offset"] + offset)[0] == 0xd65f03c0), None)
    if candidate is None:
        raise AssertionError("No RET available for the forbidden-instruction fixture")
    struct.pack_into("<I", corrupted, candidate, 0xd4000001)
    corrupted_path = args.out / "forbidden-svc.o"
    corrupted_path.write_bytes(corrupted)
    run("forbidden-svc", inputs, "NCE-unsafe instruction", object_path=corrupted_path)
    (args.out / "validation.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
