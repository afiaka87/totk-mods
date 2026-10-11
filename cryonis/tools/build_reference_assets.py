# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Pack private extracted reference geometry/pixels into an ignored build header."""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image

TEXTURES = ("Obj_IcemakerBlock_A_Emm", "Obj_IcemakerBlock_A_Nrm", "IcemakerEffect_Fx")

def build(source: Path, output: Path):
    mesh = json.loads((source / "original-mesh.json").read_text(encoding="utf-8-sig"))
    vertices, indices = mesh["vertices"], mesh["indices"]
    assert len(vertices) == 587 and len(indices) == 2124 and max(indices) < len(vertices)
    assert len(indices) % 3 == 0 and all(len(v["bones"]) <= 2 for v in vertices)
    lines = ["// Private owned-game reference; generated locally, never distributed.", "#pragma once",
             "#include <cstdint>", "namespace cryonis::assets {", "inline constexpr unsigned vertexCount=587,indexCount=2124;",
             "alignas(16) inline constexpr float mesh[][4]={"]
    def numbers(values):
        return "{" + ",".join(format(float(v), ".9e") + "f" for v in values) + "},"
    for v in vertices:
        weights = v["weights"] + [0] * (2-len(v["weights"]))
        bones = v["bones"] + [0] * (2-len(v["bones"]))
        lines.extend(numbers(row) for row in (v["pos"]+[v["uv"][0]], v["nrm"]+[v["uv"][1]],
                     v["tan"], v["bitan"], bones+weights))
    lines.append("};\nalignas(16) inline constexpr std::uint16_t indices[]={")
    lines.extend(",".join(str(v) for v in indices[i:i+32])+"," for i in range(0,len(indices),32))
    lines.append("};")
    receipt = {"mesh_sha256": hashlib.sha256((source / "original-mesh.json").read_bytes()).hexdigest(), "textures": {}}
    for index, name in enumerate(TEXTURES):
        path = source / (name + ".png")
        image = Image.open(path).convert("RGBA")
        pixels = image.tobytes()
        lines.append(f"inline constexpr unsigned tex{index}Width={image.width},tex{index}Height={image.height};")
        lines.append(f"alignas(16) inline constexpr unsigned char tex{index}[]={{")
        lines.extend(",".join(str(v) for v in pixels[i:i+48])+"," for i in range(0,len(pixels),48))
        lines.append("};")
        receipt["textures"][name] = {"width": image.width, "height": image.height, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    lines.append("}")
    output.mkdir(parents=True,exist_ok=True)
    header=output/"CryonisAssets.hpp"
    header.write_text("\n".join(lines)+"\n",encoding="utf8")
    receipt["header_sha256"]=hashlib.sha256(header.read_bytes()).hexdigest()
    (output/"reference-assets.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt,indent=2))

if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    build(args.source,args.output)
