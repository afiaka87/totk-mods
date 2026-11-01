"""Build the complete Descend patch and validate it before making it packageable."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--nso", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "build-patch")
    args = parser.parse_args()
    tool_dir = Path(__file__).resolve().parent
    repo = tool_dir.parents[3]
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Capture provenance names the worktree copy; tools/data actually loaded must agree.
    shared = repo / "scripts/patch_gaps"
    dependencies = [shared / name for name in ("__init__.py", "image.py", "formats.py", "gaps.py", "place.py", "registry.py")]
    dependencies.extend((shared / "data").glob("*.json"))
    dependencies.append(repo / "scripts/nso_decompress.py")
    for path in dependencies:
        if path.read_text(encoding="utf-8") != (args.tool_root / path.relative_to(repo)).read_text(encoding="utf-8"):
            raise ValueError(f"Tool-root differs from captured source: {path.relative_to(repo)}")
    steps = []

    def run(name: str, script: str, *options: object) -> None:
        command = [sys.executable, str(tool_dir / script), *map(str, options)]
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        log = output / f"{name}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name} failed; inspect {log}")
        steps.append(name)
        print(f"PASS {name}", flush=True)

    runtime = output / "native/descend-patch.o"
    inventory = output / "inputs.json"
    run("compile", "probe_patch.py", "--complete", "--tool-root", args.tool_root, "--out", output / "native")
    run("inventory", "inspect_patch_inputs.py", "--tool-root", args.tool_root, "--object", runtime,
        "--nso", args.nso, "--out", inventory)
    run("storage", "audit_patch_storage.py", "--tool-root", args.tool_root, "--out", output / "storage-audit.json")
    run("layout", "layout_patch_probe.py", "--complete", "--tool-root", args.tool_root, "--object", runtime,
        "--inputs", inventory, "--out", output)
    run("arm64", "test_patch_runtime.py", "--tool-root", args.tool_root, "--layout", output,
        "--inputs", inventory, "--out", output / "validation")
    run("effects", "test_patch_effects.py", "--tool-root", args.tool_root, "--layout", output,
        "--out", output / "validation/effect-tests.json")
    run("visuals", "test_patch_visuals.py", "--tool-root", args.tool_root, "--layout", output,
        "--out", output / "validation/visual-tests.json")
    run("rejections", "validate_patch_probe.py", "--complete", "--rejections-only", "--tool-root", args.tool_root,
        "--object", runtime, "--inputs", inventory, "--out", output / "rejections")
    payload = [output / "descend.pchtxt", output / "9B4E43650501A4D4489B4BBFDB740F26AF3CF850.ips"]
    result = {"verdict": "Offline validated; observed game boot required", "steps": steps,
              "packages": {name: importlib.metadata.version(name) for name in
                           ("numpy", "pyelftools", "capstone", "keystone-engine", "unicorn", "lz4")},
              "payload_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in payload}}
    (output / "validated.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
