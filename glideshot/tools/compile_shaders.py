"""SPDX-License-Identifier: MIT. Copyright (c) Clay Mullis.

Compile the mod's GLSL to NVN shader binaries and emit them as a C++ header; the pinned external
compiler and disassembler are never vendored, and every declared resource, code size and pin is checked first.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

EXPECTED_UAM = "2bf51f6713b0219cdfbee6b8ab3c7b87169b85a001e1466129e44359f13aa6fb"
EXPECTED_NVDISASM = "1138409fc6d4202c533e357a55668e11186a114b84d345a8438a84e6216d58c0"

EXPECTED_BINDINGS = {
    "frag": {"DCL CONST[2][0..13]", "DCL SAMP[0]", "DCL SVIEW[0], 2D, FLOAT"},
}
RESOURCE_PATTERN = r"^\s*(DCL\s+(?:CONST|SAMP|SVIEW|IMAGE|BUFFER)\[[^\r\n]*)"

SIZE_LIMITS = {("frag", "Code"): 47104}
DEFAULT_SIZE_LIMIT = 24576

STAGES = (("vert", "vert", "chain.vert"), ("frag", "frag", "chain.frag"))


def validate_bindings(name: str, ir: str) -> None:
    resources = re.findall(RESOURCE_PATTERN, ir, re.MULTILINE)
    expected = EXPECTED_BINDINGS.get(name, set())
    if not expected:
        if resources:
            raise ValueError(f"{name} must declare no external shader resources, found: {resources}")
        return
    if set(resources) != expected or len(resources) != len(expected):
        raise ValueError(
            f"{name}: unexpected bindings {sorted(resources)}; expected {sorted(expected)}. "
            "Review the uniform block size and the NVN UBO/texture adapter together.")


def validate_gpu_assembly(name: str, assembly: str) -> None:
    instructions = re.findall(
        r"^[ \t]*/\*[0-9a-fA-F]+\*/[ \t]+(?:@!?P(?:[0-7]|T)[ \t]+)?([A-Z][A-Z0-9_]*)",
        assembly, re.MULTILINE)
    if not instructions or "EXIT" not in instructions:
        raise ValueError(f"{name}: missing GPU instructions or EXIT in disassembly")
    if "PRET" in instructions:
        raise ValueError(f"{name}: PRET is unsupported by the host recompiler; "
                         "use single-exit shader control flow")
    if name == "frag" and {"LDL", "STL"}.intersection(instructions):
        raise ValueError(f"{name}: local-memory traffic; index arrays by literal constants only")


def disassemble_shader(name: str, code: Path, nvdisasm: Path) -> Path:
    data = code.read_bytes()
    raw = data[0x80:len(data) & ~31]
    if not raw:
        raise ValueError(f"{name}: missing complete Maxwell instruction bundles")
    raw_path, assembly_path = code.with_suffix(".raw"), code.with_suffix(".sass")
    raw_path.write_bytes(raw)
    result = subprocess.run(
        [str(nvdisasm), "-b", "SM53", "-hex", "-base", "0x80", str(raw_path)],
        capture_output=True, text=True, timeout=60, check=False)
    if result.returncode:
        raise RuntimeError(f"{name}: GPU disassembly failed: {result.stderr}")
    assembly_path.write_text(result.stdout, encoding="utf-8")
    validate_gpu_assembly(name, result.stdout)
    return assembly_path


def verify_tool(path: Path, expected: str, label: str) -> Path:
    resolved = path.resolve(strict=True)
    if hashlib.sha256(resolved.read_bytes()).hexdigest() != expected:
        raise ValueError(f"Unreviewed {label} binary: update provenance deliberately before use")
    return resolved


def compile_shaders(uam: Path, nvdisasm: Path, runtime: str, source: Path, output: Path) -> None:
    uam = verify_tool(uam, EXPECTED_UAM, "compiler")
    nvdisasm = verify_tool(nvdisasm, EXPECTED_NVDISASM, "disassembler")
    output.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    if runtime:
        env["PATH"] = str(Path(runtime).resolve(strict=True)) + os.pathsep + env.get("PATH", "")

    arrays = ["// Generated from this mod's own GLSL by an external pinned compiler.",
              "// Do not edit: rebuild with tools/compile_shaders.py.",
              "#pragma once", "namespace zonai_hookshot::shaders {"]
    receipt = {"compiler_sha256": EXPECTED_UAM,
               "disassembler_sha256": EXPECTED_NVDISASM,
               "stages": {}}

    for name, stage, filename in STAGES:
        shader = (source / filename).resolve(strict=True)
        stage_dir = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=output)).resolve()
        shader_text = shader.read_text(encoding="utf-8")
        if name == "frag":
            shared = (source / "chain_math.inl").read_text(encoding="utf-8")
            if shader_text.count("// @include chain_math.inl") != 1:
                raise ValueError("Missing unique shared chain math include")
            shader_text = shader_text.replace("// @include chain_math.inl", shared)
        compiled_source = stage_dir / shader.name
        compiled_source.write_text(shader_text, encoding="utf-8")

        control, code = stage_dir / "control.bin", stage_dir / "program.bin"
        tgsi = stage_dir / "shader.tgsi"
        result = subprocess.run(
            [str(uam), "--glslcbinds", f"--tgsi={tgsi}", f"--nvnctrl={control}",
             f"--nvngpu={code}", "-s", stage, str(compiled_source)],
            cwd=stage_dir, env=env, capture_output=True, text=True, timeout=60, check=False)
        if result.returncode or not control.is_file() or not code.is_file():
            raise RuntimeError(f"{stage} failed ({result.returncode}): {result.stdout}\n{result.stderr}")

        validate_bindings(name, tgsi.read_text(encoding="utf-8"))
        assembly = disassemble_shader(name, code, nvdisasm)

        outputs = {}
        for label, path in (("Control", control), ("Code", code)):
            data = path.read_bytes()
            limit = SIZE_LIMITS.get((name, label), DEFAULT_SIZE_LIMIT)
            if not data or len(data) > limit:
                raise ValueError(f"Unexpected {stage} {label} size: {len(data)} (cap {limit})")
            outputs[label] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            arrays.append(f"alignas(256) inline const unsigned char {name}{label}[] = {{")
            for offset in range(0, len(data), 24):
                arrays.append(",".join(f"0x{x:02x}" for x in data[offset:offset + 24]) + ",")
            arrays.append("};")

        receipt["stages"][name] = {
            "source_sha256": hashlib.sha256(shader_text.encode("utf-8")).hexdigest(),
            "tgsi_sha256": hashlib.sha256(tgsi.read_bytes()).hexdigest(),
            "sass_sha256": hashlib.sha256(assembly.read_bytes()).hexdigest(),
            "outputs": outputs,
        }

    arrays.append("}")
    (output / "ChainShaders.hpp").write_text("\n".join(arrays) + "\n", encoding="utf-8")
    (output / "shader-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("NVN shader bindings/PRET/local-memory/size checks passed; external tools, no tool sources embedded")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--uam", type=Path, required=True)
    parser.add_argument("--nvdisasm", type=Path, required=True)
    parser.add_argument("--runtime-dir", default="")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compile_shaders(args.uam, args.nvdisasm, args.runtime_dir, args.source, args.output)
