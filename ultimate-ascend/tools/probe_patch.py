"""Compile native Descend callbacks and runtime services for static patch placement.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path


def run(args: list[str], out: Path) -> str:
    result = subprocess.run(args, text=True, capture_output=True, check=False)
    with (out / "commands.log").open("a", encoding="utf-8") as log:
        log.write(json.dumps(args) + "\n" + result.stdout + result.stderr)
    if result.returncode:
        errors = [line for line in (result.stderr + result.stdout).splitlines() if "error:" in line]
        raise RuntimeError("\n".join(errors[:8]) or f"Command failed; see {out / 'commands.log'}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--complete", action="store_true", help="Include the real patch runtime services")
    parser.add_argument("--shrine-restricted", action="store_true")
    parser.add_argument("--game-version", default="1.2.1")
    parser.add_argument("--research-profile", action="store_true", help="Offline-only incomplete port; never package")
    args = parser.parse_args()
    mod = Path(__file__).resolve().parents[1]
    repo = mod.parents[2]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shim = out / "include"
    sys.path.insert(0, str(args.tool_root / "scripts"))
    from patch_gaps.image import Image
    from totk_version_port.images import BUILD_IDS
    from native_profiles import write as write_profile
    image = Image.load(BUILD_IDS[args.game_version])
    profile = write_profile(args.game_version, image, shim, research=args.research_profile)
    (shim / "program").mkdir(parents=True, exist_ok=True)
    (shim / "totk/engine").mkdir(parents=True, exist_ok=True)
    (shim / "lib.hpp").write_text(
        '#pragma once\n#include <cstdint>\nusing u64 = std::uint64_t;\n'
        'extern "C" std::uint64_t svcGetSystemTick();\n'
        'namespace descend::patch { struct InlineCtx { std::uintptr_t X[31]; }; }\n', encoding="utf-8")
    (shim / "program/loggers.hpp").write_text(
        '#pragma once\nstruct PatchLogger { static void Log(const char*, ...); };\n'
        'inline PatchLogger Logging;\n', encoding="utf-8")
    (shim / "totk/engine/CodeCheck.hpp").write_text(
        '#pragma once\n#include <cstdint>\nnamespace totk::engine {\n'
        'struct CodeSite { std::uintptr_t offset; std::uint32_t first, second; }; }\n', encoding="utf-8")

    source = (mod / "src/program/main.cpp").read_text(encoding="utf-8")
    source += "\nusing namespace descend_patch_callbacks;\n"
    inventory = json.loads((mod / "src/program/hooks.json").read_text())
    declared = re.findall(r"struct (\w+Hook) \{", source)
    by_name = {hook["name"]: hook for hook in inventory}
    if sorted(declared) != sorted(by_name):
        raise ValueError("Callback declarations do not match the hook inventory")
    hooks = [(by_name[name]["kind"], name) for name in declared]
    source += '\nextern "C" {\n __attribute__((used)) void* patch_callback_roots[] = {\n'
    source += ",\n".join(f"reinterpret_cast<void*>(&{name}::Callback)" for _, name in hooks)
    source += '\n };\n}\n'
    if args.complete:
        source += '\n#include <program/loggers.hpp>\nextern "C" {\n'
        source += '__attribute__((used)) const auto* patch_guard_root = off::kSites;\n'
        source += 'void patch_installed() { Logging.Log("[descend] v" DESCEND_VERSION '
        source += '" release installed: click the left stick while aiming Ascend to aim down"); }\n}\n'
    adapter = out / "callbacks.cpp"
    adapter.write_text(source, encoding="utf-8")
    bin_dir = args.tool_root / "vendor/totk-dkp/root/opt/devkitpro/devkitA64/bin"
    compiler = str(bin_dir / "aarch64-none-elf-g++.exe")
    sources = [mod / "src/feature/Descend.cpp", mod / "src/engine/Game.cpp", adapter]
    if args.complete:
        sources.extend([mod / "patch/Bootstrap.cpp", mod / "patch/Logger.cpp"])
    objects = []
    flags = ["-std=gnu++23", "-Oz", "-g", "-fno-exceptions", "-fno-rtti",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-ffunction-sections",
             "-fdata-sections", "-fno-jump-tables", "-fno-tree-switch-conversion", "-fno-strict-aliasing",
             "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-mno-outline-atomics",
             "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fno-pic",
             "-Wall", "-Wextra", "-Wno-unused-parameter"]
    version = tomllib.loads((mod / "mod.toml").read_text())["mod"]["version"]
    flags.append(f'-DDESCEND_VERSION="{version}"')
    flags.append(f'-DULTIMATE_ASCEND_SHRINE_RESTRICTED={int(args.shrine_restricted)}')
    flags.append(f'-DULTIMATE_ASCEND_TEXT_END={image.text_end}')
    if args.complete:
        flags.append("-Werror")
    includes = [shim, mod / "src", mod.parent / "runtime-support/include"]
    for source_path in sources:
        obj = out / (source_path.stem + ".o")
        specific = {"Bootstrap.cpp": ["-mgeneral-regs-only"],
                    "Game.cpp": ["-fno-inline-functions-called-once"]}.get(source_path.name, [])
        run([compiler, *flags, *specific, *(f"-I{p}" for p in includes), "-c", str(source_path), "-o", str(obj)], out)
        objects.append(obj)
    combined = out / ("descend-patch.o" if args.complete else "descend-probe.o")
    run([str(bin_dir / "aarch64-none-elf-ld.exe"), "-r", *(str(p) for p in objects), "-o", str(combined)], out)
    sizes = run([str(bin_dir / "aarch64-none-elf-size.exe"), "-A", str(combined)], out)
    unresolved = run([str(bin_dir / "aarch64-none-elf-nm.exe"), "-u", "-C", str(combined)], out)
    assembly = run([str(bin_dir / "aarch64-none-elf-objdump.exe"), "-dr", "-C", str(combined)], out)
    (out / "compiled.asm").write_text(assembly, encoding="utf-8")
    totals = {"code": 0, "readonly": 0, "initialized_state": 0, "zero_state": 0}
    for line in sizes.splitlines():
        match = re.match(r"(\S+)\s+(\d+)\s+\d+$", line)
        if not match:
            continue
        name, size = match[1], int(match[2])
        for prefix, bucket in ((".text", "code"), (".rodata", "readonly"), (".data", "initialized_state"), (".bss", "zero_state")):
            if name.startswith(prefix):
                totals[bucket] += size
                break
    evidence = {
        "game_version": args.game_version, "native_profile_sha256": profile["header_sha256"],
        "research_only": profile["research_only"],
        "purpose": "Complete runtime, awaiting static adapters/layout" if args.complete else "Offline feasibility probe; not deployable", "hooks": hooks,
        "source_sha256": {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in [*(p for p in sources if p != adapter), mod / "src/program/main.cpp"]},
        "flags": flags, "section_bytes": totals,
        "unresolved": [s.strip() for s in unresolved.splitlines() if s.strip()],
        "probe_sha256": hashlib.sha256(combined.read_bytes()).hexdigest(),
    }
    (out / "probe.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
