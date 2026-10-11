# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build private native bomb physics actors from owned TotK data."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib
import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'scripts'))
from totk_mod_tools.zstd_helper import TotkZstd

DONOR = 'BarrelBomb'
VARIANTS = (('Obj_RemoteBombSphere', 'sphere', 12), ('Obj_RemoteBombCube', 'cube', 12),
            ('Obj_RemoteBombSpherePlus', 'sphere', 24), ('Obj_RemoteBombCubePlus', 'cube', 24))
SHADOW_MODELS = {'sphere': 'TotkRemoteBombShadowSphere', 'cube': 'TotkRemoteBombShadowCube'}
MATERIAL_DONOR = 'BombFruit'
# Vanilla thrown objects that must keep updating away from Link (the cannonball, golf ball and
# Korok pot) use this setting; it differs from the barrel's resident default only in EnableCalcSkip.
LOD_DONOR = 'CannonBall_A_01'
LOD_SETTING = 'Component/LODParam/ForbidPushJobDefault_NoCalcSkipForEachFrame.engine__component__LODParam.bgyml'
# Body centre of each shape's collision; the lift turns the bomb about it.
LIFT_CENTERS = {'sphere': .25, 'cube': .2}
# Ultrahand takes an object whose receiver data turns it on, as vanilla's field plants (Materials) do.
# The bombs keep the default's other powers; the StaticPlant file sets the parser budget.
RECEIVER = 'Component/SpecialPowerReceiverParam/RemoteBomb.game__component__SpecialPowerReceiverParam.bgyml'
RECEIVER_BUDGET = 'Component/SpecialPowerReceiverParam/StaticPlant.game__component__SpecialPowerReceiverParam.bgyml'

def f32(value):
    return byml.Float(struct.unpack('<f', struct.pack('<f', value))[0])

def vector(x, y, z):
    return dict(zip('XYZ', (f32(x), f32(y), f32(z))))

def resource_path(link):
    if link.startswith('?'):
        return link[1:]
    if link.startswith('Work/') and link.endswith('.gyml'):
        return link[5:-5] + '.bgyml'
    return link

def rewrite(value, paths):
    if isinstance(value, str):
        return paths.get(value, value)
    if isinstance(value, dict):
        return {key: rewrite(item, paths) for key, item in value.items()}
    if isinstance(value, list):
        return [rewrite(item, paths) for item in value]
    return value

def material_components(files, material, lod):
    root_path = f'Actor/{DONOR}.engine__actor__ActorParam.bgyml'
    root = byml.Byml(files[root_path]).parse()
    source = byml.Byml(material[f'Actor/{MATERIAL_DONOR}.engine__actor__ActorParam.bgyml']).parse()
    def include(link):
        path = resource_path(link)
        if path in files or path not in material:
            return
        files[path] = material[path]
        def visit(value):
            if isinstance(value, str):
                include(value)
            elif isinstance(value, dict):
                for item in value.values(): visit(item)
            elif isinstance(value, list):
                for item in value: visit(item)
        visit(byml.Byml(files[path]).parse())
    root['Category'] = 'ObjectPouchContent'
    # The barrel's lift and grab prompt stay, so a bomb on the ground is lifted and thrown like
    # BotW's; the Material pickup prompt would add stock without removing the world bomb.
    for component in ('PouchContentRef', 'EventPerformerRef', 'PlacementMotionRef'):
        link = source['Components'][component]
        include(link)
        root['Components'][component] = link
    files[RECEIVER] = byml.Writer({
        '$parent': 'Work/Component/SpecialPowerReceiverParam/Default.game__component__SpecialPowerReceiverParam.gyml',
        'AcceptUltraHand': 'True'}, version=7).get_bytes()
    root['Components']['SpecialPowerReceiverRef'] = '?' + RECEIVER
    # No vanilla Material has event-member or navmesh components; all bind the model with the
    # default one-hand bind (the barrel bind targets the lifted-barrel wrist) and material game data.
    for component in ('EventMemberRef', 'NavMeshObjectRef'):
        files.pop(resource_path(root['Components'].pop(component)), None)
    files.pop(resource_path(root['Components']['ModelBindRef']), None)
    for component in ('ModelBindRef', 'ActorGameDataSetting'):
        root['Components'][component] = source['Components'][component]
    # The barrel's default lets the game pause a bomb it judges unimportant, which leaves a blasted
    # bomb's body behind until Link comes close.
    files[LOD_SETTING] = lod[LOD_SETTING]
    root['Components']['LODRef'] = '?' + LOD_SETTING
    game = root['Components']['GameParameterTableRef']
    params = byml.Byml(files[resource_path(game)]).parse()
    material_params = byml.Byml(material[resource_path(source['Components']['GameParameterTableRef'])]).parse()
    for name in ('ThrowBoxParam',):
        link = material_params['Components'][name]
        include(link)
        params['Components'][name] = link
    attachment = resource_path(root['Components']['AttachmentRef'])
    data = byml.Byml(files[attachment]).parse()
    data['AdditionalSubType'] = [v for v in data.get('AdditionalSubType', []) if v != 'ChemicalExplosionBomb']
    files[attachment] = byml.Writer(data, version=7).get_bytes()
    files[resource_path(game)] = byml.Writer(params, version=7).get_bytes()
    files[root_path] = byml.Writer(root, version=7).get_bytes()
    return files

def sphere(radius, center, material, volume=None):
    inertia = .4 * radius * radius
    return {
        'AutoCalc': {'Axis': vector(0, 0, 0), 'Center': vector(0, center, 0),
                     'Min': vector(-radius, center-radius, -radius),
                     'Max': vector(radius, center+radius, radius),
                     'Tensor': vector(inertia, inertia, inertia),
                     'Volume': f32(4 * math.pi * radius**3 / 3 if volume is None else volume)},
        'Sphere': [{'Center': vector(0, center, 0), 'MaterialPresets': [material],
                    'Radius': f32(radius)}],
    }

def box(material):
    width, height, depth = .5, .4, .5
    auto = {'Axis': vector(0, 0, 0), 'Center': vector(0, .2, 0),
                     'Min': vector(-.25, 0, -.25), 'Max': vector(.25, .4, .25),
                     'Tensor': vector(1.87918496/55, 2.2916851/55, 1.87918496/55),
                     'Volume': f32(width*height*depth)}
    return {
        'AutoCalc': auto,
        'Polytope': [{'AutoCalc': copy.deepcopy(auto), 'ConvexRadius': f32(.05),
                      'ShrinkByRadius': False, 'MaterialPresets': [material],
                      'Vertices': [vector(x, y, z) for x in (-.2, .2)
                                   for y in (.05, .35) for z in (-.2, .2)]}],
    }

def clone(files, actor, form, damage):
    root = f'Actor/{DONOR}.engine__actor__ActorParam.bgyml'
    renames = {name: str(Path(name).with_name(actor + '_' + Path(name).name)).replace('\\', '/')
               for name in files if name.endswith('.bgyml')}
    renames[root] = f'Actor/{actor}.engine__actor__ActorParam.bgyml'
    refs = dict(renames)
    refs.update({'?' + old: '?' + new for old, new in renames.items()})
    refs.update({'Work/' + old[:-6] + '.gyml': 'Work/' + new[:-6] + '.gyml'
                 for old, new in renames.items()})
    records = {}
    for name, raw in files.items():
        if name not in renames:
            records[name] = raw
            continue
        data = rewrite(byml.Byml(raw).parse(), refs)
        if name == root:
            # Every vanilla Material names its pouch identity; ours is the actor itself.
            data['Components']['ActorNameRef'] = f'?ActorSystem/ActorName/{actor}.engine__actor__ActorName.bgyml'
        elif name == 'Phive/ShapeParam/BarrelBomb_Body_0.phive__ShapeParam.bgyml':
            material = 'Material_Bomb'
            data = sphere(.25, .25, material, .0654499978) if form == 'sphere' else box(material)
        elif name == 'Phive/ShapeParam/BarrelBomb_Tgt_0.phive__ShapeParam.bgyml':
            data = sphere(.35, .3125, 'Material_Bomb')
        elif name == 'Phive/RigidBodyEntityParam/BarrelBomb_Body_0.phive__RigidBodyEntityParam.bgyml':
            data['Mass'] = f32(55)
            data['BuoyancySettingMethod'] = 'Auto'
            data.pop('DensityForBuoyancy', None)
            data['BuoyancyScale'] = f32(1 if form == 'sphere' else .7)
            data['WaterFlowScale'] = f32(1)
        elif name == 'GameBalance/AttackParam/BarrelBomb.game__gamebalance__AttackParam.bgyml':
            data['BaseAttackPower'] = type(data['BaseAttackPower'])(damage)
        elif name == 'Life/DamageParameters/BarrelBomb.game__life__DamageParameters.bgyml':
            # Only remote detonation explodes the bomb: like SpObj_TimerBomb_A_01, nothing damages it.
            # Lava still destroys it.
            external, internal = data['ExternalDamageParameters'], data['InternalDamageParameters']
            def assign(section, **values):
                for key, value in values.items():
                    section[key] = type(section[key])(value)
            external['ImpulseDamageParameters']['IsImpulseDamageEnabled'] = False
            assign(external['DamageRates'], **dict.fromkeys(external['DamageRates'], 0))
            external['Brittleness'] = 'Normal'
            internal['DrownDamageParameters']['IsDrownDamageEnabled'] = False
            assign(internal['BurnDamageParameters'], BoostBurnContinuousDamageRate=0, BoostIgnitionDamageRate=0,
                   BurnContinuousDamage=0, IgnitionDamageLv1=-1, IgnitionDamageLv2=-1, IgnitionDamageLv3=-1)
            assign(internal['FrozenDamageParameters'], OnTrigFrozenDamage=0)
            assign(internal['LightningDamageParameters'], Damage=0, SageDamage=0)
            assign(internal['Underwater'], ColdWaterDamage=0)
        elif name == 'Chemical/MaterialParam/BarrelBomb.game__chm__MaterialParam.bgyml':
            data['IsBurn'] = False
        elif name == 'Component/LiftableParam/BarrelBomb.game__component__LiftableParam.bgyml':
            data['IsDeathAtLanding'] = False
            # The small pot (vanilla Pot, 0.54 m wide) is the closest lifted size; the barrel
            # pose spread Link's arms for a 1 m barrel.
            data['LiftableType'] = 'Pot_S'
            data['RotateCenterPosOffset'] = vector(0, LIFT_CENTERS[form], 0)
        elif name == 'Chemical/ElementParam/DefaultExplodeBlast.game__chm__ElementParam.bgyml':
            data['Size'] = f32(6)
            data['FireLevel'] = 'None'
        elif name == 'Component/SLink/BarrelBomb.engine__component__SLinkParam.bgyml':
            data['UserName'] = 'TotkRemoteBomb'
        elif name == 'Component/ELink/BarrelBomb.engine__component__ELinkParam.bgyml':
            data['UserName'] = 'TotkRemoteBomb'
        elif name == 'Component/AttachmentParam/BarrelBomb.game__component__AttachmentParam.bgyml':
            data['CommonName'] = actor
            data['ReplacementActorName'] = f'Work/Actor/{actor}.engine__actor__ActorParam.gyml'
            # Thrown or fired bombs land intact (vanilla SpObj_TimerBomb_A_01/TimerBarrelBomb value).
            data['RequestDeathOnHit'] = 'Never'
        elif name == 'Component/ModelInfo/SS_object_BarrelBomb.engine__component__ModelInfo.bgyml':
            data['ModelProjectName'] = data['FmdbName'] = SHADOW_MODELS[form]
            data['DebugModelScale'] = vector(1, 1, 1)
        encoded = byml.Writer(data, version=7).get_bytes()
        if byml.Byml(encoded).parse() != data:
            raise ValueError('BYML round trip: ' + name)
        records[renames[name]] = encoded
    return records, renames

def budgets(raw):
    if raw[:6] != b'RESTBL':
        raise ValueError('Expected owned TotK resource table')
    width, count, overflow = struct.unpack_from('<III', raw, 10)
    crc = dict(struct.unpack_from('<II', raw, 22+index*8) for index in range(count))
    names = {}
    for index in range(overflow):
        start = 22+count*8+index*(width+4)
        key = raw[start:start+width].split(b'\0', 1)[0].decode()
        names[key] = struct.unpack_from('<I', raw, start+width)[0]
    return lambda path: names.get(path, crc.get(zlib.crc32(path.encode()), 0))

def build(romfs: Path, output: Path, shadow_dir: Path):
    z = TotkZstd(romfs)
    source = romfs / f'Pack/Actor/{DONOR}.pack.zs'
    original = z.decompress(source)
    files = {entry.name: bytes(entry.data) for entry in oead.Sarc(original).get_files()}
    material = {entry.name: bytes(entry.data) for entry in oead.Sarc(
        z.decompress(romfs/f'Pack/Actor/{MATERIAL_DONOR}.pack.zs')).get_files()}
    lod = {entry.name: bytes(entry.data) for entry in oead.Sarc(
        z.decompress(romfs/f'Pack/Actor/{LOD_DONOR}.pack.zs')).get_files()}
    files = material_components(files, material, lod)
    stock = z.decompress(romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    budget = budgets(stock)
    floors, actors, shadows = {}, [], {}
    shadow_budget = budget('Model/Obj_BoxIce_A.Obj_BoxIce_A_01.bfres')
    if shadow_budget <= 0:
        raise ValueError('Missing native shadow model parser budget')
    for form, model in SHADOW_MODELS.items():
        folder = shadow_dir/form
        proof = json.loads((folder/'shadow-receipt.json').read_text())
        payload = folder/f'{model}.{model}.bfres.mc'
        expanded = folder/'round-trip.bfres'
        if proof['model'] != model or not proof['topology_preserved'] or proof['vertex_max_error'] > .001:
            raise ValueError('Shadow model proof does not match: '+form)
        if payload.read_bytes()[:4] != b'MCPK' or expanded.read_bytes()[:4] != b'FRES':
            raise ValueError('Invalid native shadow model container: '+form)
        growth = max(0, expanded.stat().st_size-(folder/'donor-expanded.bfres').stat().st_size)
        floors[f'Model/{model}.{model}.bfres'] = max(expanded.stat().st_size*2+65536,
                                                     shadow_budget+growth*4+65536)
        shadows[form] = {**proof, 'sha256': hashlib.sha256(payload.read_bytes()).hexdigest(),
                         'expanded_bytes': expanded.stat().st_size}
    for actor, form, damage in VARIANTS:
        records, renames = clone(files, actor, form, damage)
        archive = oead.SarcWriter(oead.Endianness.Little)
        for path, raw in records.items():
            archive.files[path] = raw
        packed = bytes(archive.write()[1])
        relative = f'Pack/Actor/{actor}.pack.zs'
        target = output / 'romfs' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(packed, target.name))
        reopened = {entry.name: bytes(entry.data) for entry in oead.Sarc(z.decompress(target)).get_files()}
        if reopened != records:
            raise ValueError('Actor pack round trip: ' + actor)
        floors[relative[:-3]] = max(len(packed)+65536,
                                    budget(f'Pack/Actor/{DONOR}.pack')+
                                    max(0, len(packed)-len(original))+65536)
        for old, new in renames.items():
            stock_budget = budget(RECEIVER_BUDGET if old == RECEIVER else old)
            if stock_budget <= 0:
                raise ValueError('Missing original parser budget: ' + old)
            floors[new] = max(len(records[new])*10+4096,
                              stock_budget+max(0, len(records[new])-len(files[old]))*10+4096)
        actors.append({'actor': actor, 'form': form, 'damage': damage, 'mass': 55,
                       'pack_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
                       'members': len(records), 'renamed_parameters': len(renames)})
    registration = []
    for family in ('ActorInfo', 'GameActorInfo', 'PouchActorInfo', 'AttachmentActorInfo'):
        relative = f'RSDB/{family}.Product.121.rstbl.byml.zs'
        raw = z.decompress(romfs / relative)
        rows = byml.Byml(raw).parse()
        donor_name = MATERIAL_DONOR if family == 'PouchActorInfo' else DONOR
        donor = next(row for row in rows if row['__RowId'] == donor_name)
        for actor, form, _ in VARIANTS:
            if any(row['__RowId'] == actor for row in rows):
                raise ValueError('Custom actor already in clean table: ' + actor)
            row = copy.deepcopy(donor)
            row['__RowId'] = actor
            if family == 'ActorInfo':
                row['ActorName'] = actor
                row['Category'] = 'ObjectPouchContent'
                row['InstanceHeapSize'] = type(row['InstanceHeapSize'])(int(row['InstanceHeapSize']) + 131072)
                row['ModelProjectName'] = row['FmdbName'] = SHADOW_MODELS[form]
                row['ModelAabbMin'] = vector(*shadows[form]['minimum'])
                row['ModelAabbMax'] = vector(*shadows[form]['maximum'])
            elif family == 'PouchActorInfo':
                row['BuyingPrice'] = type(row['BuyingPrice'])(0)
                row['SellingPrice'] = type(row['SellingPrice'])(0)
            elif family == 'AttachmentActorInfo':
                row['AttachmentCommonName'] = actor
                row['AttachmentAdditionalSubType'] = ','.join(
                    v for v in row['AttachmentAdditionalSubType'].split(',') if v != 'ChemicalExplosionBomb')
            rows.append(row)
        rows.sort(key=lambda row: row['__RowId'])
        encoded = byml.Writer(rows, version=7).get_bytes()
        target = output / 'romfs' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(encoded, target.name))
        if byml.Byml(z.decompress(target)).parse() != rows:
            raise ValueError('Registration round trip: ' + relative)
        floors[relative[:-3]] = (max(budget(relative[:-3]), len(encoded)) +
                                 max(4096, (len(encoded)-len(raw))*10))
        registration.append({'resource': relative, 'original_rows': len(rows)-4,
                              'custom_rows': 4, 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    receipt = {'donor': DONOR, 'donor_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'actors': actors, 'registration': registration, 'shadows': shadows, 'restbl_floors': floors,
               'status': 'original physics dimensions, target sensor, inertia, volume and buoyancy; native Bomb material and per-body damping/limits; blast and uncommon contact parity remain open'}
    output.mkdir(parents=True, exist_ok=True)
    (output / 'actor-recipe.json').write_text(json.dumps(receipt, indent=2))
    from build_inventory import build_registration
    inventory = build_registration(romfs, output, z, budget)
    floors.update(inventory['restbl_floors'])
    receipt['inventory'] = inventory
    (output / 'actor-recipe.json').write_text(json.dumps(receipt, indent=2))
    (output / 'restbl-forces.tsv').write_text(''.join(f'{path}\t{size}\n' for path, size in sorted(floors.items())))
    print(f'Built {len(actors)} private actors, four registration tables, {len(floors)} parser floors.')
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--romfs', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--shadows', required=True, type=Path)
    args = parser.parse_args()
    build(args.romfs, args.output, args.shadows)
