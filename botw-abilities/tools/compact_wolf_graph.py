# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Shrink Wolf Link's animation graph to match the compacted clips.

Commands the companion never requests route to the nearest surviving behaviour, unreachable nodes go,
and one-shot clips play as a frame range of a shared bank. Stock animals play ranges the same way; stock event frames count from the clip start,
so event frames move with the clip.
"""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import sys
from uuid import NAMESPACE_URL, uuid5

# Each command keeps its name, so a native request still resolves.
ALIASES = {
    'Hearing': 'Wait', 'Hearing_End': 'Wait', 'Notice': 'Wait', 'Search': 'Wait',
    'Hit_BackStep': 'BackStep', 'RoarLoop': 'Roar', 'Warp': 'WarpOut', 'Turn_Escape': 'Turn_Escape_NoGrowl',
    'Move_AttackEx_Gear1': 'Move_Attack_Gear3', 'Move_EscapeEx_Gear1': 'Move_Attack_Gear3',
    'Move_Escape_Gear1': 'Move_Attack_Gear3', 'Move_Escape_Gear2': 'Move_Attack_Gear3',
    'Move_Gear1': 'Move_Attack_Gear1', 'Move_Normal_Gear1': 'Move_Attack_Gear1',
    'Move_Gear2': 'Move_Attack_Gear2', 'Move_Normal_Gear2': 'Move_Attack_Gear2',
    'Move_Gear3': 'Move_Attack_Gear3', 'Move_Gear4': 'Move_Attack_Gear3',
}


def references(body):
    """Every place a node body names another node, as (container, key) pairs."""
    for key in ('Frame Controls', 'Events'):
        values = body.get(key) or []
        yield from ((values, index) for index in range(len(values)))
    children = body.get('Child Nodes') or []
    if isinstance(children, dict):
        children = [children]
    for index, child in enumerate(children):
        if isinstance(child, int):
            yield children, index
        else:
            yield from ((child, key) for key in ('Node Index', 'Condition True', 'Condition False') if key in child)


def replace_references(graph, mapping):
    for node in graph['Nodes']:
        for container, key in references(node.get('Body') or {}):
            container[key] = mapping.get(container[key], container[key])
    for command in graph['Commands']:
        command['Node Index'] = mapping.get(command['Node Index'], command['Node Index'])


def route_commands(graph):
    """Point unused commands, and the gear selectors that name them, at surviving behaviours."""
    commands = {command['Name']: command for command in graph['Commands']}
    routes = {}
    for name, target in ALIASES.items():
        old, new = commands[name]['Node Index'], commands[target]['Node Index']
        if routes.setdefault(old, new) != new:
            raise ValueError(f'Two routes for the behaviour behind {name}')
    for command in graph['Commands']:
        if command['Node Index'] in routes and command['Name'] not in ALIASES:
            raise ValueError(f'{command["Name"]} shares a behaviour marked as unused')
        command['Node Index'] = routes.get(command['Node Index'], command['Node Index'])
    for node in graph['Nodes']:
        if node['Node Type'] == 'IntSelector':
            for case in node['Body']['Child Nodes']:
                case['Node Index'] = routes.get(case['Node Index'], case['Node Index'])


def reachable(graph):
    seen, stack = set(), [command['Node Index'] for command in graph['Commands']]
    while stack:
        index = stack.pop()
        if index < 0 or index in seen:
            continue
        seen.add(index)
        stack.extend(c[k] for c, k in references(graph['Nodes'][index].get('Body') or {}))
    return seen


def prune(graph):
    keep = sorted(reachable(graph))
    mapping = {old: new for new, old in enumerate(keep)}
    nodes = graph['Nodes']
    graph['Nodes'] = [nodes[index] for index in keep]
    replace_references(graph, mapping)
    for index, node in enumerate(graph['Nodes']):
        node['Node Index'] = index


def shift_event(item, base):
    start, end = item['Start Frame'], item.get('End Frame')
    whole = start == 0 and end == -1
    if start >= 0 and not whole:
        item['Start Frame'] = start + base
    if end is not None and end >= 0:
        item['End Frame'] = end + base


def shift_events(node, base):
    event = node['Body']['Event']
    for items in event.values():
        for item in items:
            shift_event(item, base)
    for entry in node.get('BAEV Events', []):
        for lists in entry['Event'].values():
            for items in lists.values():
                for item in items:
                    shift_event(item, base)


def place_clips(graph, placements):
    """Rename one-shot clips to their bank and give each a frame range inside it."""
    nodes = graph['Nodes']
    template = next(node for node in nodes if node['Node Type'] == 'FrameController')
    owners, added = {}, 0
    for node in list(nodes):
        if node['Node Type'] != 'SkeletalAnimation' or node['Body']['Animation'] not in placements:
            continue
        body = node['Body']
        clip = body['Animation']
        place = placements[clip]
        base, length = place['base'], place['length']
        if not body.get('Frame Controls'):
            control = deepcopy(template)
            control['Node Index'] = len(nodes)
            control['GUID'] = str(uuid5(NAMESPACE_URL, f'botw-abilities/wolf-link/range/{node["GUID"]}'))
            control['Body'].update({'Animation Rate': 1.0, 'Start Frame': 0.0, 'End Frame': -1.0, 'Loop Flags': 0,
                                    'Loop Num': -1, 'Max Random Loop Num': -1})
            nodes.append(control)
            body['Frame Controls'] = [control['Node Index']]
            added += 1
        for index in body['Frame Controls']:
            control = nodes[index]['Body']
            if control['Loop Flags'] == 1:
                raise ValueError(f'A bank range must not loop: {clip}')
            start, end = control['Start Frame'], control['End Frame']
            control['Start Frame'] = float(base + start)
            control['End Frame'] = float(base + (length if end < 0 else end))
        for index in body.get('Events') or []:
            if owners.setdefault(index, node['Node Index']) != node['Node Index']:
                raise ValueError(f'An event is shared between clips: {clip}')
            shift_events(nodes[index], base)
        body['Animation'] = place['bank']
    return added


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--graph', type=Path, required=True, help='Directory of the complete graph')
    parser.add_argument('--fit', type=Path, required=True, help='fit.json from compact_wolf_animations.py')
    parser.add_argument('--asb-tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    graph = json.loads((args.graph / 'graph.json').read_text(encoding='utf-8'))
    conversion = json.loads((args.graph / 'conversion.json').read_text(encoding='utf-8'))
    fit = json.loads(args.fit.read_text(encoding='utf-8'))
    before = len(graph['Nodes'])
    route_commands(graph)
    prune(graph)
    stored = set(fit['loops']) | set(fit.get('standalone', [])) | set(fit['placements'])
    used = {node['Body']['Animation'] for node in graph['Nodes'] if node['Node Type'] == 'SkeletalAnimation'}
    if used - stored:
        raise ValueError(f'Clips the graph still needs: {sorted(used - stored)}')
    added = place_clips(graph, fit['placements'])
    output = args.output.resolve()
    tool = args.asb_tool.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    sys.path.insert(0, str(tool))
    from wolf_native_commands import validate_native_movement
    previous = Path.cwd()
    os.chdir(tool)
    try:
        from asb import ASB
        validate_native_movement(graph)
        ASB.from_dict(graph).to_binary(str(output))
        saved = ASB.from_binary((output / 'BotwWolfLink.root.asb').read_bytes())
        saved.import_baev((output / 'BotwWolfLink.root.baev').read_bytes())
        reparsed = saved.asdict()
        validate_native_movement(reparsed)
    finally:
        os.chdir(previous)
    if reparsed != json.loads(json.dumps(graph)):
        raise ValueError('The compacted graph did not survive a round-trip')
    (output / 'graph.json').write_text(json.dumps(reparsed, indent=1) + '\n', encoding='utf-8')
    conversion['compacted'] = dict(nodes_before=before, nodes=len(reparsed['Nodes']),
                                   added_ranges=added, unused_clips=sorted(stored - used), aliases=ALIASES)
    (output / 'conversion.json').write_text(json.dumps(conversion, indent=1) + '\n', encoding='utf-8')
    print(f'Graph: {before} to {len(reparsed["Nodes"])} nodes, '
          f'{added} ranges added, unused stored clips {sorted(stored - used)}; '
          f'{(output / "BotwWolfLink.root.asb").stat().st_size} + {(output / "BotwWolfLink.root.baev").stat().st_size} bytes')


if __name__ == '__main__':
    main()
