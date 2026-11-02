"""Check generated profiles and fail-closed layout witnesses against owned images."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.tool_root / "scripts"))
    from patch_gaps.image import Image
    from totk_version_port.images import BUILD_IDS
    from native_profile_data import VALIDATED_VERSIONS, VERSIONS
    from native_profiles import render
    from native_contracts import verify

    cases, profiles = [], []
    for index, version in enumerate(VERSIONS):
        image = Image.load(BUILD_IDS[version])
        header, report = render(version, image, research=True)
        profiles.append(report)
        cases.append(version + " generated words and layout")
        if version == "1.2.1":
            original = Path(__file__).resolve().parents[1] / "src/engine/DescendOffsets.hpp"
            assert header == original.read_text(), "Baseline header changed"
        if version not in VALIDATED_VERSIONS:
            try:
                render(version, image)
            except ValueError as error:
                assert "under review" in str(error)
            else:
                raise AssertionError("Research profile escaped its packaging gate")
            cases.append(version + " research gate")
        try:
            render(VERSIONS[(index + 1) % len(VERSIONS)], image, research=True)
        except ValueError as error:
            assert "identity differ" in str(error)
        else:
            raise AssertionError("Wrong image accepted")
        cases.append(version + " wrong image rejected")
        changed = dict(report["bindings"])
        changed["player::kReturnTarget"] += 4
        try:
            verify(image, changed)
        except ValueError as error:
            assert "cancel layout differs" in str(error)
        else:
            raise AssertionError("Moved member accepted")
        cases.append(version + " wrong return member rejected")
        for key in ("prompt::kSetupMessage", "prompt::kRefreshLabel", "prompt::kId",
                    "kEffectWipeRegister", "kEffectLoopRegister", "main_stage::kState",
                    "cancel_stage::kSpeed", "voxel_up::kNextSolid", "kPhysicsWorldFromWrapper",
                    "kWorldPositionMethod", "game_camera::kPosition", "node::kOwnerActor"):
            step = 8 if key in ('kPhysicsWorldFromWrapper','kWorldPositionMethod','node::kOwnerActor') else 1
            changed = report["bindings"] | {key: report["bindings"][key] + step}
            try:
                verify(image, changed)
            except ValueError:
                pass
            else:
                raise AssertionError(f"Wrong native contract accepted: {version} {key}")
            cases.append(version + " wrong " + key + " rejected")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"passed": len(cases), "cases": cases, "profiles": profiles}, indent=2) + "\n")
    print(json.dumps({"passed": len(cases), "profiles": len(profiles),
                      "scope": "Owned-image bindings and listed layout witnesses; incomplete ports remain gated"}))


if __name__ == "__main__":
    main()
