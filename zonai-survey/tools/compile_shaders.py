"""SPDX-License-Identifier: MIT. Invoke an external compiler; embed only our shaders."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

EXPECTED_UAM = "2bf51f6713b0219cdfbee6b8ab3c7b87169b85a001e1466129e44359f13aa6fb"
EXPECTED_NVDISASM = "1138409fc6d4202c533e357a55668e11186a114b84d345a8438a84e6216d58c0"

def validate_bindings(name: str, ir: str) -> None:
    if name in ("vert", "calibration"):
        if re.search(r"\bDCL\s+(?:CONST|SAMP|SVIEW|IMAGE|BUFFER)\[", ir):
            raise ValueError(f"{name} must have no external shader resources")
    elif name in ("frag", "uniformCheck", "rawDepth", "labelVert", "labelFrag"):
        resources = re.findall(r"^\s*(DCL\s+(?:CONST|SAMP|SVIEW|IMAGE|BUFFER)\[[^\r\n]*)", ir, re.MULTILINE)
        expected = {
            "frag": {"DCL CONST[2][0..13]", "DCL SAMP[0]", "DCL SVIEW[0], 2D, FLOAT"},
            "uniformCheck": {"DCL CONST[2][0]"},
            "rawDepth": {"DCL SAMP[0]", "DCL SVIEW[0], 2D, FLOAT"},
            "labelVert": {"DCL CONST[2][0..255]"},
            "labelFrag": {"DCL CONST[2][0..3391]"},
        }[name]
        if set(resources) != expected or len(resources) != len(expected):
            raise ValueError("Unexpected fragment bindings: review the NVN UBO/texture adapter")
    else:
        raise ValueError(f"Unknown shader: {name}")

def validate_gpu_assembly(name: str, assembly: str) -> None:
    instructions = re.findall(
        r"^[ \t]*/\*[0-9a-fA-F]+\*/[ \t]+(?:@!?P(?:[0-7]|T)[ \t]+)?([A-Z][A-Z0-9_]*)",
        assembly, re.MULTILINE)
    if not instructions or "EXIT" not in instructions:
        raise ValueError(f"{name}: missing GPU instructions or EXIT in disassembly")
    if "PRET" in instructions:
        raise ValueError(f"{name}: PRET is unsupported by Eden; use single-exit shader control flow")
    if name in ("labelVert", "labelFrag") and "IMAD" in instructions:
        raise ValueError(f"{name}: IMAD is unsupported by Eden; use bounded low-product indexing")
    if name == "frag" and {"LDL", "STL"}.intersection(instructions):
        raise ValueError("frag: local-memory traffic is outside the scan shader budget")
    if name == "labelFrag":
        if re.search(r"\b(?:FADD|IADD)(?:\.[A-Z0-9]+)*\.SAT\b", assembly):
            raise ValueError("labelFrag: integer bounds became saturation; use explicit comparisons")
        loads = re.findall(r"\bLDC R\d+, c\[0x3\]\[R\d+(?:\+0x([48c]))?\]", assembly)
        if sorted(loads) != sorted(["", "4", "8", "c"] * 4):
            raise ValueError("labelFrag: four bilinear taps must each load all four atlas words")

def validate_label_ir(ir: str) -> None:
    if re.search(r"\b[A-Z]+_SAT\b", ir):
        raise ValueError("labelFrag: integer clamp was lowered to saturation")
    loads = re.findall(r"MOV (TEMP\[\d+\]), CONST\[2\]\[ADDR\[0\]\.x\](?=\s*$)", ir, re.MULTILINE)
    if len(loads) != 4:
        raise ValueError("labelFrag: expected four complete atlas-vector loads")
    for register in loads:
        for lane in "xyzw":
            if not re.search(r"MOV TEMP\[\d+\]\.x, " + re.escape(register + "." + lane*4) + r"\b", ir):
                raise ValueError("labelFrag: atlas vector component was lost")
    for comparison in ("ISLT", "ISGE"):
        if len(re.findall(r"\b"+comparison+r"\b", ir)) != 8:
            raise ValueError("labelFrag: each tap needs signed lower and upper bounds on both axes")

def disassemble_shader(name: str, code: Path, nvdisasm: Path) -> Path:
    data = code.read_bytes()

    # UAM stores an 0x30-byte prefix and an 0x50-byte program header before the code.
    raw = data[0x80:len(data) & ~31]
    if not raw:
        raise ValueError(f"{name}: missing complete Maxwell instruction bundles")
    raw_path, assembly_path = code.with_suffix(".raw"), code.with_suffix(".sass")
    raw_path.write_bytes(raw)
    result = subprocess.run([str(nvdisasm), "-b", "SM53", "-hex", "-base", "0x80", str(raw_path)],
                            capture_output=True, text=True, timeout=60, check=False)
    if result.returncode:
        raise RuntimeError(f"{name}: GPU disassembly failed: {result.stderr}")
    assembly_path.write_text(result.stdout, encoding="utf-8")
    validate_gpu_assembly(name, result.stdout)
    return assembly_path

def compile_shaders(uam: Path, nvdisasm: Path, runtime: str, source: Path, output: Path, production: bool = False, constrained: bool = False, atlas: bool = False) -> None:
    uam = uam.resolve(strict=True)
    if hashlib.sha256(uam.read_bytes()).hexdigest() != EXPECTED_UAM:
        raise ValueError("Unreviewed compiler binary: update provenance deliberately before use")
    nvdisasm = nvdisasm.resolve(strict=True)
    if hashlib.sha256(nvdisasm.read_bytes()).hexdigest() != EXPECTED_NVDISASM:
        raise ValueError("Unreviewed disassembler binary: update provenance deliberately before use")
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    if runtime:
        env["PATH"] = str(Path(runtime).resolve(strict=True)) + os.pathsep + env.get("PATH", "")
    arrays = ["// Generated from first-party GPLv2 GLSL by an external compiler.",
              "#pragma once", "namespace survey_fidelity::shaders {"]
    receipt = {"compiler_sha256": EXPECTED_UAM, "disassembler_sha256": EXPECTED_NVDISASM,
               "profile": "production" if production else "playground", "constrained": constrained, "stages": {}}
    stages = [("vert", "vert", "diagnostic.vert"),
                                  ("frag", "frag", "diagnostic.frag"),
                                  ("calibration", "frag", "calibration.frag"),
                                  ("uniformCheck", "frag", "uniform_check.frag"),
                                  ("rawDepth", "frag", "raw_depth.frag")]
    if atlas:
        stages += [("labelVert", "vert", "labels.vert"), ("labelFrag", "frag", "labels.frag")]
    for name, stage, filename in stages:
        shader = (source / filename).resolve(strict=True)

        stage_dir = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=output)).resolve()
        shader_text = shader.read_text(encoding="utf-8")
        if name == "frag":
            if constrained:
                shader_text = shader_text.replace("#version 450", "#version 450\n#define SF_CONSTRAINED 1")
            if production:
                shader_text = shader_text.replace("#version 450", "#version 450\n#define SF_DIAGNOSTICS 0")
            shared = (source / "aesthetic_math.inl").read_text(encoding="utf-8")
            if shader_text.count("// @include aesthetic_math.inl") != 1:
                raise ValueError("Missing unique shared aesthetic math include")
            shader_text = shader_text.replace("// @include aesthetic_math.inl", shared)
        compiled_source = stage_dir / shader.name
        compiled_source.write_text(shader_text, encoding="utf-8")
        control, code = stage_dir / "control.bin", stage_dir / "program.bin"
        tgsi = stage_dir / "shader.tgsi"
        result = subprocess.run([str(uam), "--glslcbinds", f"--tgsi={tgsi}", f"--nvnctrl={control}",
            f"--nvngpu={code}", "-s", stage, str(compiled_source)], cwd=stage_dir, env=env,
            capture_output=True, text=True, timeout=60, check=False)
        if result.returncode or not control.is_file() or not code.is_file():
            raise RuntimeError(f"{stage} failed ({result.returncode}): {result.stdout}\n{result.stderr}")
        ir = tgsi.read_text(encoding="utf-8")
        validate_bindings(name, ir)
        if name == "labelFrag":
            validate_label_ir(ir)
        assembly = disassemble_shader(name, code, nvdisasm)
        outputs = {}
        for label, path in (("Control", control), ("Code", code)):
            data = path.read_bytes()

            limit = 47104 if name == "frag" and label == "Code" else 24576
            if not data or len(data) > limit:
                raise ValueError(f"Unexpected {stage} {label} size: {len(data)}")
            outputs[label] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            arrays.append(f"alignas(256) inline const unsigned char {name}{label}[] = {{")
            for offset in range(0, len(data), 24):
                arrays.append(",".join(f"0x{x:02x}" for x in data[offset:offset+24]) + ",")
            arrays.append("};")
        receipt["stages"][name] = {"source_sha256": hashlib.sha256(shader.read_bytes()).hexdigest(),
                                     "expanded_source_sha256": hashlib.sha256(compiled_source.read_bytes()).hexdigest(),
                                     "tgsi_sha256": hashlib.sha256(tgsi.read_bytes()).hexdigest(),
                                     "sass_sha256": hashlib.sha256(assembly.read_bytes()).hexdigest(),
                                     "outputs": outputs}
    arrays.append("}")
    (output / "FidelityShaders.hpp").write_text("\n".join(arrays) + "\n", encoding="utf-8")
    (output / "shader-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("NVN shader compilation/bindings/PRET checks passed; external tools, no tool sources embedded")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--uam", type=Path, required=True)
    parser.add_argument("--nvdisasm", type=Path, required=True)
    parser.add_argument("--runtime-dir", default="")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--constrained", action="store_true")
    parser.add_argument("--atlas", action="store_true")
    args = parser.parse_args()
    compile_shaders(args.uam, args.nvdisasm, args.runtime_dir, args.source, args.output, args.production, args.constrained, args.atlas)
