# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build the public download's resource-size table: the clean 1.2.1 table, every composed RomFS
file sized, then the same parse budgets the local deploy sets."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

TABLE = "System/Resource/ResourceSizeTable.Product.121.rsizetable.zs"


def read_forces(path: Path) -> list[tuple[str, str]]:
    forces = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            resource, size = line.split("\t")
            forces.append((resource, str(int(size))))
    return forces


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True, help="totk-file-tool executable")
    parser.add_argument("--build", type=Path, required=True, help="build directory")
    args = parser.parse_args()
    romfs, out = args.build / "romfs", args.build / "release-restbl"
    receipt_path = args.build / "release-restbl-receipt.json"
    forces = read_forces(args.build / "restbl-forces.tsv")

    inputs = hashlib.sha256(Path(__file__).read_bytes() + args.tool.read_bytes())
    for resource, size in forces:
        inputs.update(f"{resource}\t{size}\n".encode())
    for path in sorted(p for p in romfs.rglob("*") if p.is_file()):
        inputs.update(path.relative_to(romfs).as_posix().encode() + b"\0" + path.read_bytes())
    digest, table = inputs.hexdigest(), out / TABLE
    if receipt_path.exists() and table.is_file():
        previous = json.loads(receipt_path.read_text(encoding="utf-8"))
        if previous.get("inputs_sha256") == digest and previous.get(
            "table_sha256"
        ) == hashlib.sha256(table.read_bytes()).hexdigest():
            print("[botw-abilities] release resource table unchanged")
            return 0

    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    run = lambda *command: subprocess.run([str(args.tool), *map(str, command)], check=True,
                                          stdout=subprocess.DEVNULL)
    run("--restbl-version", "121", "restbl-rebuild", romfs, "--rstb-romfs", out)
    for resource, size in forces:
        run("--restbl-version", "121", "restbl-set", "--rstb-romfs", out, resource, size)
    receipt = {"inputs_sha256": digest, "forces": len(forces),
               "table_sha256": hashlib.sha256(table.read_bytes()).hexdigest()}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"[botw-abilities] release resource table built with {len(forces)} budget floors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
