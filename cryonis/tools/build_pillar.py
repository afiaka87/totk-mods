# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build the private Cryonis collision actor from an owned TotK 1.2.1 dump."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import struct
import copy
import zlib
import re
import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from totk_mod_tools.zstd_helper import TotkZstd

def f32(value):
    return byml.Float(struct.unpack("<f", struct.pack("<f", value))[0])

ACTOR = "Obj_CryonisPillar"
DONOR = "DgnObj_IceBlock"

def tuning():
    source = Path(__file__).resolve().parents[1] / 'src/pure/CryonisTuning.hpp'
    return parse_tuning(source.read_text(encoding='utf-8'))

def parse_tuning(text: str):
    def scalar(name):
        matches = re.findall(r'inline\s+constexpr\s+float\s+' + re.escape(name)
                             + r'\s*=\s*([0-9]+(?:\.[0-9]+)?)f\s*;', text)
        if len(matches) != 1:
            raise ValueError('Expected one literal tuning constant: ' + name)
        return float(matches[0])
    width, height, percent = (scalar(name) for name in ('kOriginalWidth','kOriginalHeight','kSizePercent'))
    submerge = scalar('kSettledSubmergePercent')
    if not (width > 0 and height > 0 and 1 <= percent <= 200 and 0 <= submerge < 25):
        raise ValueError('Unsupported Cryonis dimensions or percentages')
    scale = percent / 100
    return dict(original_dimensions=[width,height,width], size_percent=percent,
                dimensions=[width*scale,height*scale,width*scale],
                settled_submerge_percent=submerge,settled_submerge=height*scale*submerge/100)

def cloned_resource_floor(original_budget: int, original_bytes: int, cloned_bytes: int) -> int:
    if original_budget <= 0:
        raise ValueError("A cloned resource requires its original parse budget")
    # Tiny BGYML files still instantiate fixed-size native parameter objects.
    return max(cloned_bytes * 10 + 4096,
               original_budget + max(0, cloned_bytes - original_bytes) * 10 + 4096)

def build(romfs: Path, output: Path) -> dict:
    settings=tuning();width,height,depth=settings['dimensions'];half=width/2
    z = TotkZstd(romfs)
    info = romfs / 'RSDB/ActorInfo.Product.121.rstbl.byml.zs'
    stock_info = byml.Byml(z.decompress(info)).parse()
    donor_info = next(row for row in stock_info if row['__RowId'] == DONOR)
    model_bounds = {side: {axis: float(donor_info['ModelAabb' + side][axis]) for axis in 'XYZ'}
                    for side in ('Min', 'Max')}
    # Model-only shadow proxy: physics always stays at actor scale 1.
    model_scale = {axis: dimension / (model_bounds['Max'][axis] - model_bounds['Min'][axis])
                   for axis, dimension in zip('XYZ', settings['dimensions'])}
    assert all(0 < scale < 10 for scale in model_scale.values())
    source = romfs / f"Pack/Actor/{DONOR}.pack.zs"
    archive = oead.Sarc(z.decompress(source))
    files = {entry.name: bytes(entry.data) for entry in archive.get_files()}
    paths = {name: str(Path(name).with_name(ACTOR + "_" + Path(name).name)).replace("\\", "/")
             for name in files if name.endswith(".bgyml")}
    root = f"Actor/{DONOR}.engine__actor__ActorParam.bgyml"
    paths[root] = f"Actor/{ACTOR}.engine__actor__ActorParam.bgyml"
    links = {"Work/" + name.removesuffix(".bgyml") + ".gyml": "Work/" + dest.removesuffix(".bgyml") + ".gyml"
             for name, dest in paths.items()}
    links.update({"?" + name: "?" + dest for name, dest in paths.items()})
    links.update(paths)
    def rewrite(value):
        if isinstance(value, str): return links.get(value, value)
        if isinstance(value, dict): return {key: rewrite(item) for key, item in value.items()}
        if isinstance(value, list): return [rewrite(item) for item in value]
        return value
    out = {}
    for name, raw in files.items():
        if name not in paths:
            out[name] = raw
            continue
        data = rewrite(byml.Byml(raw).parse())
        if name == root:
            # The clone is controlled by the module; no melting, carry, or fuse path.
            for key in ("AIInfoRef", "ChemicalRef", "LifeRef", "LiftableRef", "SpecialPowerReceiverRef",
                        "AttentionSenderRef", "AttachmentRef", "WorldInfoRef", "ActorGameDataSettingRef"):
                data["Components"].pop(key, None)
        if name.endswith("/DgnObj_IceBlock.engine__component__ModelInfo.bgyml"):
            data["DebugModelScale"] = {axis: f32(model_scale[axis]) for axis in "XYZ"}
        if name.endswith("/IceBlock_Body_0.phive__ShapeParam.bgyml"):
            box = data["Box"][0]
            box["HalfExtents"] = {"X": f32(half), "Y": f32(height/2), "Z": f32(half)}
            box["OffsetTranslation"]["Y"] = f32(height/2)
            ac = data["AutoCalc"]
            ac["Center"] = {"X": f32(0), "Y": f32(height/2), "Z": f32(0)}
            ac["Min"] = {"X": f32(-half), "Y": f32(0), "Z": f32(-half)}
            ac["Max"] = {"X": f32(half), "Y": f32(height), "Z": f32(half)}
            ac["Volume"] = f32(width*height*depth)
            ac["Tensor"] = {"X": f32((height*height+depth*depth)/12), "Y": f32((width*width+depth*depth)/12), "Z": f32((width*width+height*height)/12)}
        if name.endswith("/IceBlock_Body_0.phive__RigidBodyEntityParam.bgyml"):
            data["MotionType"] = "Keyframed"
            data["Mass"] = f32(1000000)
        dest = paths[name]
        out[dest] = byml.Writer(data, version=7).get_bytes()
        assert byml.Byml(out[dest]).parse() == data, dest
    actor = byml.Byml(out[paths[root]]).parse()
    physics_ref = actor["Components"]["PhysicsRef"]
    def resolve(link):
        if link.startswith("?"): return link[1:]
        if link.endswith(".bgyml"): return link
        return link.removeprefix("Work/").removesuffix(".gyml") + ".bgyml"
    physics = byml.Byml(out[resolve(physics_ref)]).parse()
    controller = byml.Byml(out[resolve(physics["ControllerSetPath"])]).parse()
    body = byml.Byml(out[resolve(controller["RigidBodyEntityNamePathAry"][0]["FilePath"])]).parse()
    shape = byml.Byml(out[resolve(next(item["FilePath"] for item in controller["ShapeNamePathAry"] if item["Name"] == body["ShapeName"]))]).parse()
    assert body["MotionType"] == "Keyframed" and body["Mass"] == 1000000
    assert all(abs(actual-expected)<1e-6 for actual,expected in
               zip(shape["Box"][0]["HalfExtents"].values(),[half,height/2,half]))
    assert all(path not in out for path in paths if path != paths[path])
    writer = oead.SarcWriter(oead.Endianness.Little)
    for name, raw in out.items(): writer.files[name] = raw
    packed = bytes(writer.write()[1])
    dest = output / f"romfs/Pack/Actor/{ACTOR}.pack.zs"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(z.compress(packed, dest.name))
    assert {entry.name: bytes(entry.data) for entry in oead.Sarc(z.decompress(dest)).get_files()} == out
    table=romfs/'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'
    restbl=z.decompress(table);assert restbl[:6]==b'RESTBL'
    width,count,overflow=struct.unpack_from('<III',restbl,10)
    crc_sizes=dict(struct.unpack_from('<II',restbl,22+i*8) for i in range(count))
    name_sizes={}
    for i in range(overflow):
        start=22+count*8+i*(width+4)
        name=restbl[start:start+width].split(b'\0',1)[0].decode('utf8')
        name_sizes[name]=struct.unpack_from('<I',restbl,start+width)[0]
    def budget(name):return name_sizes.get(name,crc_sizes.get(zlib.crc32(name.encode()),0))
    floors={f'Pack/Actor/{ACTOR}.pack':max(len(packed)+65536,
        budget(f'Pack/Actor/{DONOR}.pack')+max(0,len(packed)-len(z.decompress(source)))+65536)}
    parameter_budgets=[]
    for original, cloned in paths.items():
        original_budget=budget(original)
        floor=cloned_resource_floor(original_budget,len(files[original]),len(out[cloned]))
        floors[cloned]=floor
        parameter_budgets.append(dict(original=original,cloned=cloned,
            original_budget=original_budget,original_bytes=len(files[original]),
            cloned_bytes=len(out[cloned]),floor=floor))
    registration=[]
    for family in ('ActorInfo','GameActorInfo'):
        original=romfs/f'RSDB/{family}.Product.121.rstbl.byml.zs'
        raw=z.decompress(original);rows=byml.Byml(raw).parse();assert isinstance(rows,list)
        donor=next(row for row in rows if row['__RowId']==DONOR)
        row=copy.deepcopy(donor);row['__RowId']=ACTOR
        if family=='ActorInfo':
            row['ModelAabbMin']={axis:f32(-half if axis!='Y' else 0) for axis in 'XYZ'}
            row['ModelAabbMax']={axis:f32(half if axis!='Y' else height) for axis in 'XYZ'}
        else:row['HasAutoAnim']=False
        assert not any(row['__RowId']==ACTOR for row in rows)
        rows.append(row);rows.sort(key=lambda item:item['__RowId'])
        encoded=byml.Writer(rows,version=7).get_bytes();assert byml.Byml(encoded).parse()==rows
        relative=original.relative_to(romfs).as_posix();target=output/'romfs'/relative;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(z.compress(encoded,original.name));assert byml.Byml(z.decompress(target)).parse()==rows
        resource=relative.removesuffix('.zs');stock_budget=budget(resource)
        assert stock_budget>0,resource
        floors[resource]=max(stock_budget,len(encoded))+max(4096,(len(encoded)-len(raw))*10)
        registration.append({'resource':resource,'stock_rows':len(rows)-1,'original_budget':stock_budget,'floor':floors[resource],
            'payload_sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    receipt = {"actor": ACTOR, "totk_version": "1.2.1", "registration_product": 121,
               "donor_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "pack_sha256": hashlib.sha256(dest.read_bytes()).hexdigest(), "raw_bytes": len(packed),
               "members": len(out), **settings, "motion": "Keyframed", "mass": 1000000,
               "model_only_scale": model_scale, "model_bounds": model_bounds,
               "shadow_mode": "GameModelController DepthShadowOnly", "physics_ref": physics_ref,'registration':registration,'restbl_floors':floors,
               'parameter_budgets':parameter_budgets,
               'resource_table_sha256':hashlib.sha256(table.read_bytes()).hexdigest()}
    (output / "pillar-recipe.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(build(args.romfs, args.output), indent=2))

