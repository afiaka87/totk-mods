# SPDX-License-Identifier: MIT
"""Fail closed on module growth or accidentally linked legacy/diagnostic data."""
import argparse
from pathlib import Path
import subprocess
from profile_memory import nso_profile

def validate(profile, symbols, compact=True, production=True, atlas=False):
    if profile["mapped_bytes"] > 1024*1024:
        raise ValueError("Depth Survey exceeded its 1 MiB mapped-module budget")
    forbidden = []
    if compact:
        forbidden.append("zonai_survey::glyphs::kPlacements")
    if production:
        forbidden += [f"survey_fidelity::shaders::{name}" for name in
                      ("calibrationCode", "uniformCheckCode", "rawDepthCode")]
        forbidden.append("survey_fidelity::trace::begin()")
    for name in forbidden:
        if any(line.endswith(" "+name) for line in symbols.splitlines()):
            raise ValueError(f"Unused data or diagnostic linked into compact Survey: {name}")
    if atlas:
        for name in ("TextWriter", "DebugFont", "BootupInitDebugDrawersHook", "DebugDrawHook", "openSurveyAsset"):
            if name in symbols:
                raise ValueError(f"Native font dependency linked into atlas Survey: {name}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--nso", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--atlas", action="store_true")
    args = parser.parse_args()
    result = subprocess.run([str(args.nm), "--defined-only", "--demangle", str(args.elf)],
                            capture_output=True, text=True, check=True, timeout=30)
    profile = nso_profile(args.nso.read_bytes())
    validate(profile, result.stdout, args.compact, args.production, args.atlas)
    print(f"Survey budget OK: mapped={profile['mapped_bytes']} ceiling=1048576; not total runtime RAM")
