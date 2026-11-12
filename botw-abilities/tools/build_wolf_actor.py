# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Assemble a private Wolf Link resource draft; gameplay and allocation checks remain open."""

import argparse
from copy import deepcopy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys

import byml
import oead
from wolf_movement import read_movement

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'scripts'))
from totk_mod_tools.zstd_helper import TotkZstd

ACTOR = 'BotwWolfLink'
DONOR = 'Animal_Wolf_A'
GLOBAL_GRAPHS = 'Pack/AI.Global.Product.120.pack.zs'
LOCK_TEMPLATE = 'Pack/Actor/Enemy_Bokoblin_Junior.pack.zs'
LOCK_CLIENT = 'Attention/ClientParam/Bokoblin_LockOn.game__attention__ClientParam.bgyml'
SAGE_TEMPLATE = 'Pack/Actor/LightningStrike_Sage.pack.zs'
SAGE_AFFILIATION = 'Component/AffiliationParam/SageCommon.game__component__AffiliationParam.bgyml'
WAIT_CONTAINER = 'Work/Game/BSA/ActionSeqContainer/QuadrupedCommon_Wait.game__bsa__ActionSeqContainer.gyml'
# Pruning stops if a reference walk would lose anything but unused hunting graphs, replaced parameters or
# the donor's drop tables.
PRUNABLE = ('AI/', 'Game/BSA/ActionSeqContainer/', 'Drop/', 'Component/DropParam/')
DONOR_DROP = '?Component/DropParam/Animal_Small2.game__component__DropParam.bgyml'
# A stock animal drop set that drops nothing, carried unchanged by 47 stock packs.
NO_DROP_TEMPLATE = 'Pack/Actor/Item_Enemy_15.pack.zs'
NO_DROP_TABLE = 'Drop/DropTable/Animal_NoDrop_Normal.game__drop__DropTable.bgyml'
NO_DROP_ELEMENTS = 'Drop/DropTableElementList/Animal_NoDrop_Normal_00.game__drop__DropTableElementList.bgyml'
# The sage companions' attack layer (Yunobo's body hitbox, Npc_Goron020_Atk_0): it reaches enemies, never Link.
BITE_LAYER = 'FriendlyAttack'
STOCK_BITE_SENSOR = {'LayerSensor': 'EnemyAttack', 'MotionType': 'Kinematic', 'ShapeName': 'Atk_Body',
                     'SubLayerSensor': 'Unspecified'}
# The bite's native share; equals kNativeBitePower in src/wolf/Companion.hpp, which adds the rest.
NATIVE_BITE_POWER = 1
# Wild wolves and bears (PredatorCommon) ignore hits carrying this identifier.
ANIMAL_ATTACK = '戦闘動物の攻撃'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def checked(path, expected):
    data = path.read_bytes()
    if sha(data) != expected:
        raise ValueError(f'Converted resource no longer matches its receipt: {path.name}')
    return data


def encode(node):
    raw = byml.Writer(node, version=7).get_bytes()
    if byml.Byml(raw).parse() != node:
        raise ValueError('BYML round-trip changed the authored values')
    return raw


def members(raw):
    return {entry.name: bytes(entry.data) for entry in oead.Sarc(raw).get_files()}


def root_motion_contract(files, graph):
    path = f'Actor/{ACTOR}.engine__actor__ActorParam.bgyml'
    visited = set()
    while path not in visited:
        visited.add(path)
        actor = byml.Byml(files[path]).parse()
        if 'AnimeDrivenMoveRef' in actor.get('Components', {}):
            reference = actor['Components']['AnimeDrivenMoveRef']
            break
        parent = actor.get('$parent', '')
        if not parent.startswith('Work/Actor/') or not parent.endswith('.gyml'):
            raise ValueError('Wolf Link has no inherited animation-driven movement component')
        path = parent.removeprefix('Work/')[:-5] + '.bgyml'
    else:
        raise ValueError('Actor inheritance cycle while resolving animation-driven movement')
    parameter = reference.removeprefix('?')
    if not reference.startswith('?') or parameter not in files:
        raise ValueError('Animation-driven movement parameter is missing or disabled')
    settings = byml.Byml(files[parameter]).parse()
    if settings != {'PartialSlotName': 'Root'}:
        raise ValueError('Animation-driven movement no longer uses the verified native defaults')
    slots = graph['Partials']
    matches = [i for i, slot in enumerate(slots) if slot['Name'] == settings['PartialSlotName']]
    if len(matches) != 1 or slots[matches[0]]['Is Material Slot']:
        raise ValueError('Animation-driven movement requires one skeletal Root slot')
    return dict(inherited_from=path, parameter=parameter, slot=matches[0],
                parameter_sha256=sha(files[parameter]))


def fork_actor(files, projects, ainb, movement, lock_client, sage, no_drop):
    files = dict(files)
    actor_path = f'Actor/{DONOR}.engine__actor__ActorParam.bgyml'
    actor = byml.Byml(files[actor_path]).parse()
    if actor['Category'] != 'Animal':
        raise ValueError('Expected the verified native animal host')
    changed = {}
    for component, values in (
        ('ModelInfoRef', {'FmdbName': ACTOR, 'ModelProjectName': ACTOR, 'ModelVariationAnims': []}),
        ('AnimationRef', {'AnimationResources': projects}),
    ):
        source = actor['Components'][component].removeprefix('?')
        destination = source.replace('/' + DONOR + '.', '/' + ACTOR + '.')
        if source == destination or destination in files:
            raise ValueError(f'Component fork is not private: {source}')
        node = byml.Byml(files[source]).parse()
        if set(node) != set(values):
            raise ValueError(f'Native component fields changed: {source}')
        files[destination] = encode(values)
        actor['Components'][component] = '?' + destination
        changed[destination] = source
    source = actor['Components']['QuadrupedRef'].removeprefix('?')
    destination = f'Component/QuadrupedParam/{ACTOR}.game__component__QuadrupedParam.bgyml'
    quadruped = byml.Byml(files[source]).parse()
    if len(quadruped['AnimeSpeeds']) != 6 or destination in files:
        raise ValueError('Native quadruped speed layout or private destination changed')
    quadruped.update(movement)
    quadruped['BasePlayRate'] = byml.Float(movement['BasePlayRate'])
    quadruped['AnimeSpeeds'] = [{field: [byml.Float(value) for value in values]
                                 for field, values in row.items()} for row in movement['AnimeSpeeds']]
    files[destination] = encode(quadruped)
    actor['Components']['QuadrupedRef'] = '?' + destination
    changed[destination] = source
    actor['Components']['ASRef'] = f'?AS/{ACTOR}.root.asb'
    bsa_source = actor['Components']['BSARef'].removeprefix('?')
    bsa = byml.Byml(files[bsa_source]).parse()
    if bsa['BrainAinPath'] != 'Work/AI/Root/BattleAnimalCommon.brain.root.ain':
        raise ValueError('The donor brain no longer matches the inspected native graph')
    brain_source = 'AI/BattleAnimalCommon.brain.root.ainb'
    brain = ainb.from_binary(files[brain_source]).as_dict()
    idle = deepcopy(next(node for node in brain['Nodes'] if node.get('Name') == 'ExecuteGenericDummyAction'))
    if idle['Queries'] or idle['Attachments'] or idle['Plugs'] or idle['Parameters']['Inputs']:
        raise ValueError('The native idle node acquired an external dependency')
    if len(brain['Commands']) != 1 or brain['Commands'][0]['Name'] != 'Root':
        raise ValueError('Expected the donor brain to expose only Root')
    idle['Node Index'], idle['Flags'] = 0, ['Is Root Node']
    brain.update(Filename=f'{ACTOR}.brain.root', Nodes=[idle], Modules=[], Expressions={})
    brain['Commands'][0]['Root Node Index'] = 0
    brain['Replacement Table'], brain['Unknown Section 0x58'] = [], {}
    brain_raw = ainb.from_dict(brain).to_binary()
    if ainb.from_binary(brain_raw).as_dict() != brain:
        raise ValueError('Private brain round-trip changed its single idle node')
    brain_destination = f'AI/{ACTOR}.brain.root.ainb'
    files[brain_destination] = brain_raw
    changed[brain_destination] = brain_source
    bsa['BrainAinPath'] = f'Work/AI/Root/{ACTOR}.brain.root.ain'
    bsa['UtilityAinPath'] = ''
    bsa_destination = f'Component/BSAParam/{ACTOR}.game__component__BSAParam.bgyml'
    files[bsa_destination] = encode(bsa)
    actor['Components']['BSARef'] = '?' + bsa_destination
    changed[bsa_destination] = bsa_source
    wait_scheduler(files, changed, bsa_destination)
    attention(files, changed, actor, lock_client)
    allegiance(files, changed, actor, sage)
    leave_nothing(files, changed, actor, no_drop)
    sound_user(files, changed, actor)
    bite_hitbox(files, changed, actor)
    bite_power(files, changed, actor)
    destination = actor_path.replace('/' + DONOR + '.', '/' + ACTOR + '.')
    files[destination] = encode(actor)
    changed[destination] = actor_path
    return files, changed


def attention(files, changed, actor, lock_client):
    """Replace the stock wolf's bow-aim marker with a lock-on target using the original's reach."""
    sender_source = 'Component/AttentionSenderParam/Hunting_Ground.game__component__AttentionSenderParam.bgyml'
    client_source = 'Attention/ClientParam/Hunting_AutoAim.game__attention__ClientParam.bgyml'
    sender = byml.Byml(files[sender_source]).parse()
    if len(sender['ClientList']) != 1 or 'AttentionSenderRef' in actor['Components']:
        raise ValueError('The donor attention sender changed')
    client_path = f'Attention/ClientParam/{ACTOR}_Lock.game__attention__ClientParam.bgyml'
    client = deepcopy(lock_client)
    if client['AttentionType'] != 'LockOn' or client['ActionType'] != 'None':
        raise ValueError('The lock-on template changed')
    fans = [check for check in client['CheckList'] if check['CheckType'] == 'AreaFan']
    if len(fans) != 1:
        raise ValueError('Expected one lock-on fan')
    fans[0]['CheckTypeAreaFan'].update(AttentionFanAngle=byml.Float(60.0), AttentionFanRadius=byml.Float(10.0),
                                       AttentionFanTop=byml.Float(10.0), AttentionFanBottom=byml.Float(10.0))
    files[client_path] = encode(client)
    changed[client_path] = client_source
    sender['ClientList'] = [f'Work/{client_path.removesuffix(".bgyml")}.gyml']
    sender_path = f'Component/AttentionSenderParam/{ACTOR}.game__component__AttentionSenderParam.bgyml'
    files[sender_path] = encode(sender)
    changed[sender_path] = sender_source
    actor['Components']['AttentionSenderRef'] = '?' + sender_path


def leave_nothing(files, changed, actor, no_drop):
    """Wolf Link leaves nothing behind when he falls: his drop component names the stock empty table.

    The reference must stay: the pack carries a copy of the parent animal's parameters, whose own drop
    reference names a file this pack never held, and the game reads that copy when the child says nothing.
    """
    if actor['Components'].get('DropRef') != DONOR_DROP:
        raise ValueError('The donor drop component changed')
    source = DONOR_DROP.removeprefix('?')
    drop = byml.Byml(files[source]).parse()
    if set(drop) != {'$parent', 'DropTableResourceList'}:
        raise ValueError('The donor drop layout changed')
    drop['DropTableResourceList'] = ['Work/' + NO_DROP_TABLE.removesuffix('.bgyml') + '.gyml']
    destination = f'Component/DropParam/{ACTOR}.game__component__DropParam.bgyml'
    files[destination] = encode(drop)
    actor['Components']['DropRef'] = '?' + destination
    changed[destination] = source
    for path, raw in no_drop.items():
        if files.get(path, raw) != raw:
            raise ValueError('The stock empty drop table differs from the donor copy: ' + path)
        files[path] = raw


def allegiance(files, changed, actor, sage):
    """Stand with Link: enemies target and hear the game's Sage companions, never the stock wolf's Predator side."""
    source = actor['Components']['AffiliationRef'].removeprefix('?')
    donor = byml.Byml(files[source]).parse()
    if donor['DefaultAffiliation'] != 'Predator' or sage['DefaultAffiliation'] != 'Sage' or set(donor) != set(sage):
        raise ValueError('The donor or Sage affiliation layout changed')
    destination = f'Component/AffiliationParam/{ACTOR}.game__component__AffiliationParam.bgyml'
    files[destination] = encode(sage)
    actor['Components']['AffiliationRef'] = '?' + destination
    changed[destination] = source


def sound_user(files, changed, actor):
    """Name the wolf's own sound user (tools/build_wolf_sound.py), a copy of the stock wolf's plus the bite calls."""
    source = actor['Components']['SLinkRef'].removeprefix('?')
    node = byml.Byml(files[source]).parse()
    if node != {'UserName': DONOR}:
        raise ValueError('The donor sound component changed')
    destination = f'Component/SLink/{ACTOR}.engine__component__SLinkParam.bgyml'
    files[destination] = encode({'UserName': ACTOR})
    actor['Components']['SLinkRef'] = '?' + destination
    changed[destination] = source


def work_path(path):
    return 'Work/' + path.removesuffix('.bgyml') + '.gyml'


def fork(files, changed, source, node, destination):
    if destination in files:
        raise ValueError('Private destination already exists: ' + destination)
    files[destination] = encode(node)
    changed[destination] = source


def bite_hitbox(files, changed, actor):
    """The bite keeps the stock wolf's hit window and hitbox shape; only the hitbox layer changes, to the sages'."""
    physics_source = actor['Components']['PhysicsRef'].removeprefix('?')
    physics = byml.Byml(files[physics_source]).parse()
    set_source = member_path(physics['ControllerSetPath'])
    controllers = byml.Byml(files[set_source]).parse()
    bodies = [row for row in controllers['RigidBodySensorNamePathAry'] if row['Name'] == 'Body']
    if len(bodies) != 1:
        raise ValueError('Expected one stock bite hitbox')
    sensor_source = member_path(bodies[0]['FilePath'])
    sensor = byml.Byml(files[sensor_source]).parse()
    if sensor != STOCK_BITE_SENSOR:
        raise ValueError('The stock bite hitbox changed')
    sensor['LayerSensor'] = BITE_LAYER
    sensor_destination = f'Phive/RigidBodySensorParam/{ACTOR}_Atk_0.phive__RigidBodySensorParam.bgyml'
    fork(files, changed, sensor_source, sensor, sensor_destination)
    bodies[0]['FilePath'] = work_path(sensor_destination)
    set_destination = f'Phive/ControllerSetParam/{ACTOR}.phive__ControllerSetParam.bgyml'
    fork(files, changed, set_source, controllers, set_destination)
    physics['ControllerSetPath'] = work_path(set_destination)
    physics_destination = f'Component/Physics/{ACTOR}.engine__component__PhysicsParam.bgyml'
    fork(files, changed, physics_source, physics, physics_destination)
    actor['Components']['PhysicsRef'] = '?' + physics_destination


def bite_power(files, changed, actor):
    """The bite's native hit carries a token power and no animal identifier; the companion code adds the rest."""
    table_source = actor['Components']['GameParameterTableRef'].removeprefix('?')
    table = byml.Byml(files[table_source]).parse()
    attack_source = table['Components']['gamebalance_AttackParam'].removeprefix('?')
    attack = byml.Byml(files[attack_source]).parse()
    rows = attack['AttackParams']
    if attack['BaseAttackPower'] != 10 or len(rows) != 3 or any(row['AttackIdentifier'] != ANIMAL_ATTACK for row in rows):
        raise ValueError('The stock wolf attack table changed')
    attack['BaseAttackPower'] = type(attack['BaseAttackPower'])(NATIVE_BITE_POWER)
    for row in rows:
        row['AttackIdentifier'] = ''
        row['AttackIdentifierHash'] = type(row['AttackIdentifierHash'])(0)
    attack_destination = f'GameBalance/AttackParam/{ACTOR}.game__gamebalance__AttackParam.bgyml'
    fork(files, changed, attack_source, attack, attack_destination)
    table['Components']['gamebalance_AttackParam'] = '?' + attack_destination
    table_destination = f'GameParameter/GameParameterTable/{ACTOR}.engine__actor__GameParameterTable.bgyml'
    fork(files, changed, table_source, table, table_destination)
    actor['Components']['GameParameterTableRef'] = '?' + table_destination


def wait_scheduler(files, changed, bsa_path):
    """Point every brain verb at the wait sequence: the companion code drives this wolf, so hunting never runs.

    The private brain never selects another verb. Every verb stays listed, so a lookup by name still finds one.
    """
    bsa = byml.Byml(files[bsa_path]).parse()
    source = bsa['SchedulerParamPath'].removeprefix('Work/').removesuffix('.gyml') + '.bgyml'
    scheduler = byml.Byml(files[source]).parse()
    if scheduler['UseCustomScheduler'] or WAIT_CONTAINER not in {v['ActionSeqContainer'] for v in scheduler['BrainVerbs']}:
        raise ValueError('The donor scheduler no longer matches the inspected native layout')
    for verb in scheduler['BrainVerbs']:
        if verb['EnterProcs'] or verb['LeaveProcs'] or verb['FormChangeActionSeqContainers']:
            raise ValueError('A native verb gained behaviour outside its sequence: ' + verb['BrainVerb'])
        verb['ActionSeqContainer'] = WAIT_CONTAINER
    destination = f'BSA/SchedulerParam/{ACTOR}.game__bsa__SchedulerParam.bgyml'
    files[destination] = encode(scheduler)
    changed[destination] = source
    bsa['SchedulerParamPath'] = f'Work/BSA/SchedulerParam/{ACTOR}.game__bsa__SchedulerParam.gyml'
    files[bsa_path] = encode(bsa)


def strings_in(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for value in node.values():
            yield from strings_in(value)
    elif isinstance(node, list):
        for value in node:
            yield from strings_in(value)


def member_path(reference):
    path = reference.removeprefix('?').removeprefix('Work/')
    if path.startswith('AI/Root/'):
        path = 'AI/' + path.removeprefix('AI/Root/')
    return re.sub(r'\.ain$', '.ainb', re.sub(r'\.gyml$', '.bgyml', path))


def referenced(path, raw, graphs):
    if path.endswith('.bgyml'):
        return {member_path(text) for text in strings_in(byml.Byml(raw).parse())}
    if path.endswith('.ainb'):
        words = (word.decode().removesuffix('.ainb') for word in re.findall(rb'[A-Za-z0-9_.]{6,}', raw))
        return {graphs[word] for word in words if word in graphs}
    return set()


def prune(files, changed, global_graphs):
    """Keep what the private actor reaches, following graphs through the game's shared AI pack."""
    everything = {**global_graphs, **files}
    graphs = {path[3:-5]: path for path in everything if path.startswith('AI/') and path.endswith('.ainb')}
    seen, stack = set(), [f'Actor/{ACTOR}.engine__actor__ActorParam.bgyml']
    while stack:
        path = stack.pop()
        if path not in seen and path in everything:
            seen.add(path)
            stack.extend(referenced(path, everything[path], graphs))
    removed = set(files) - seen
    unexpected = {path for path in removed if not path.startswith(PRUNABLE) and path not in changed.values()}
    if unexpected:
        raise ValueError(f'The reference walk would drop live members: {sorted(unexpected)}')
    return {path: raw for path, raw in files.items() if path in seen}, sorted(removed)


def component_references(files, actor=ACTOR):
    """Every component file the game may read for this actor: the child's references and, where the child
    is silent, the parent copy's. The caller checks each resolves in the pack or the loose romfs."""
    chain, path = [], f'Actor/{actor}.engine__actor__ActorParam.bgyml'
    while path in files and path not in chain:
        node = byml.Byml(files[path]).parse()
        chain.append(path)
        path = member_path(node.get('$parent', ''))
    effective = {}
    for path in reversed(chain):
        effective.update(byml.Byml(files[path]).parse().get('Components', {}))
    return {key: member_path(value) for key, value in effective.items() if value.startswith('?')}


def unresolved_components(files, romfs, actor=ACTOR):
    return sorted(f'{key}: {path}' for key, path in component_references(files, actor).items()
                  if path not in files and not (romfs / path).exists() and not (romfs / (path + '.zs')).exists())


def original_metadata(path):
    raw = path.read_bytes()
    tree = oead.byml.from_binary(oead.yaz0.decompress(raw))
    row = next(row for row in tree['Actors'] if row['name'] == 'WolfLink')
    scale = float(row['actorScale'])
    if row['bfres'] != 'Animal_WLink' or row['mainModel'] != 'Animal_WLink' or not math.isfinite(scale) or scale <= 0:
        raise ValueError('Original Wolf Link model identity or scale is invalid')
    bounds = {target: {axis: byml.Float(float(row[source][axis])) for axis in ('X', 'Y', 'Z')}
              for source, target in (('aabbMin', 'ModelAabbMin'), ('aabbMax', 'ModelAabbMax'))}
    return bounds, scale


def build(args):
    z = TotkZstd(args.romfs)
    sys.path.insert(0, str(args.ainb_tool))
    from ainb import AINB
    model = read_json(args.assets / 'model-receipt.json')
    textures = read_json(args.assets / 'texture-receipt.json')
    plan = read_json(args.animation_plan)
    conversion = read_json(args.graph / 'conversion.json')
    if model['project'] != ACTOR or plan['resources'] != ['Animal_Wolf_Animation']:
        raise ValueError('Expected the verified Wolf Link model and Wolf-only animation plan')
    if not conversion['roundtrip_equal'] or conversion['coverage']['commands'] != 50:
        raise ValueError('Animation draft did not pass its complete command round-trip')
    sys.path.insert(0, str(args.asb_tool))
    from asb import ASB
    previous_directory = Path.cwd()
    try:
        os.chdir(args.asb_tool)
        graph = ASB.from_binary((args.graph / f'{ACTOR}.root.asb').read_bytes())
        graph.import_baev((args.graph / f'{ACTOR}.root.baev').read_bytes())
        if graph.asdict() != read_json(args.graph / 'graph.json'):
            raise ValueError('Animation draft bytes differ from the checked graph')
        from wolf_native_commands import validate_native_movement
        validate_native_movement(graph.asdict())
    finally:
        os.chdir(previous_directory)

    outputs, sources = {}, {}

    def native(path):
        raw = (args.romfs / path).read_bytes()
        sources[path] = sha(raw)
        return z.decompress(args.romfs / path) if path.endswith('.zs') else raw

    projects = [{'ModelProjectName': item['project']} for item in plan['selected'].values()]
    donor_path = f'Pack/Actor/{DONOR}.pack.zs'
    original = members(native(donor_path))
    movement, movement_receipt = read_movement(args.botw_romfs)
    template = members(native(LOCK_TEMPLATE))[LOCK_CLIENT]
    sage = byml.Byml(members(native(SAGE_TEMPLATE))[SAGE_AFFILIATION]).parse()
    no_drop_pack = members(native(NO_DROP_TEMPLATE))
    no_drop = {path: no_drop_pack[path] for path in (NO_DROP_TABLE, NO_DROP_ELEMENTS)}
    files, changed = fork_actor(original, projects, AINB, movement, byml.Byml(template).parse(), sage, no_drop)
    root_motion = root_motion_contract(files, graph.asdict())
    if any(files[path] != raw for path, raw in original.items()):
        raise ValueError('An existing shared pack member was modified')
    files, pruned = prune(files, changed, members(native(GLOBAL_GRAPHS)))
    # Stock references the game finds elsewhere (shared packs, bootup tables) stay as the donor had them; the
    # private graph is a draft output. Anything else is a reference the game cannot resolve.
    dangling = (set(unresolved_components(files, args.romfs)) - set(unresolved_components(original, args.romfs, DONOR))
                - {f'ASRef: AS/{ACTOR}.root.asb'})
    if dangling:
        raise ValueError(f'Component references the donor never needed and the pack cannot resolve: {sorted(dangling)}')
    writer = oead.SarcWriter(oead.Endianness.Little)
    for path, raw in files.items():
        writer.files[path] = raw
    archive = bytes(writer.write()[1])
    if members(archive) != files:
        raise ValueError('Actor pack round-trip changed its members')
    outputs[f'Pack/Actor/{ACTOR}.pack.zs'] = z.compress(archive, donor_path)

    model_name = f'{ACTOR}.{ACTOR}.bfres.mc'
    outputs['Model/' + model_name] = checked(args.assets / model_name, model['model_sha256'])
    for texture in textures:
        name = texture['file']
        if Path(name).name != name or not name.startswith(ACTOR + '_') or not name.endswith('.txtg'):
            raise ValueError('Texture names must remain private file stems')
        outputs['TexToGo/' + name] = checked(args.assets / 'TexToGo' / name, texture['sha256'])
    requested = {name for material in model['materials'] for name in material['textures']}
    if requested != {Path(texture['file']).stem for texture in textures}:
        raise ValueError('Model texture bindings and converted texture inventory differ')
    for resource in plan['selected'].values():
        for name, receipt in resource['files'].items():
            if Path(name).name != name or not name.startswith(ACTOR + '_'):
                raise ValueError('Animation resources must remain private file stems')
            raw = checked(args.animations / name, receipt['sha256'])
            path = 'Model/' + name
            if name.endswith('.anim.bfres'):
                path += '.zs'
                raw = z.compress(raw, path)
            outputs[path] = raw
    for folder, extension in (('AS', 'asb'), ('AnimationEvent/AsNode', 'baev')):
        name = f'{ACTOR}.root.{extension}'
        path = f'{folder}/{name}.zs'
        outputs[path] = z.compress((args.graph / name).read_bytes(), path)

    original_info = args.botw_romfs / 'Actor/ActorInfo.product.sbyml'
    bounds, scale = original_metadata(original_info)
    sources['BotW/Actor/ActorInfo.product.sbyml'] = sha(original_info.read_bytes())
    added_rows = {}
    for table in ('ActorInfo', 'GameActorInfo'):
        path = f'RSDB/{table}.Product.121.rstbl.byml.zs'
        rows = byml.Byml(native(path)).parse()
        if any(row['__RowId'] == ACTOR for row in rows):
            raise ValueError('Input is not a clean registry: ' + path)
        row = deepcopy(next(row for row in rows if row['__RowId'] == DONOR))
        row['__RowId'] = ACTOR
        if table == 'ActorInfo':
            if row.get('SLinkUserName') != DONOR:
                raise ValueError('The donor registry row no longer names the stock wolf sound user')
            row.update(ActorName=ACTOR, ModelProjectName=ACTOR, FmdbName=ACTOR, SLinkUserName=ACTOR,
                       AnimationResources=projects, **bounds)
            row.pop('ModelVariationFmabName')
        added_rows[table] = row
        outputs[path] = z.compress(encode(sorted(rows + [row], key=lambda row: row['__RowId'])), path)

    tree = args.output / 'romfs'
    stale = {path.relative_to(tree).as_posix() for path in tree.rglob('*') if path.is_file()} - outputs.keys()
    if stale:
        raise ValueError(f'Unexpected existing output files: {sorted(stale)}')
    for path, raw in outputs.items():
        target = tree / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        if target.read_bytes() != raw:
            raise ValueError('Written resource differs: ' + path)
    receipt = dict(diagnostic_only=True, actor=ACTOR, donor=DONOR, spawn_scale=scale, native_sources=sources,
                   movement=movement_receipt, root_motion=root_motion,
                   modified_members=changed, pruned_members=pruned, added_rows=added_rows,
                   input_receipts={name: sha(path.read_bytes()) for name, path in (
                       ('model', args.assets / 'model-receipt.json'),
                       ('textures', args.assets / 'texture-receipt.json'),
                       ('animations', args.animation_plan), ('graph', args.graph / 'conversion.json'))},
                   files={path: {'sha256': sha(raw), 'bytes': len(raw)} for path, raw in outputs.items()},
                   unresolved=['Companion behavior, affiliation, life and scene lifecycle',
                               'Original steering, avoidance and animation-driven speed scaling',
                               'Native parse budgets and instance allocation measurement',
                               'Rendered model/retargeting parity and wheel integration'],
                   graph_requirements=conversion['remaining_requirements'])
    (args.output / 'actor-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    (args.output / 'generated-chunks.txt').write_text(
        'Resource draft only: do not install as a standalone mod. No runtime spawn source or RESTBL is included.\n'
        'The private brain is inert without the companion adapter; native life remains untuned.\n'
        + '\n'.join(sorted(outputs)) + '\n\n' + json.dumps(added_rows, indent=2) + '\n', encoding='utf-8')
    print(f'Private actor draft: {len(outputs)} files, {len(changed)} private members; gameplay/allocation checks pending')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('romfs', 'botw-romfs', 'assets', 'animations', 'animation-plan', 'graph', 'asb-tool', 'ainb-tool', 'output'):
        parser.add_argument('--' + name, type=lambda path: Path(path).resolve(), required=True)
    build(parser.parse_args())
