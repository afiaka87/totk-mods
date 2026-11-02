"""Inventory cross-version candidates; never treat fragment votes as a shipping profile.

The anchor input is a compact IDA function-boundary export for the 1.2.1 native
sites. Results retain the actual target words and competing candidates so each
ABI, member layout and caller can be checked before a target is enabled.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def candidates(args):
    sys.path.insert(0, str(args.root / 'scripts'))
    from totk_version_port.images import MainImage
    from totk_version_port.mapping import map_region, map_site, map_callee
    anchors = json.loads(args.anchors.read_text())
    source = MainImage('1.2.1')
    args.out.mkdir(parents=True, exist_ok=True)
    for version in args.versions:
        target = MainImage(version)
        rows = []
        functions = {}
        for anchor in anchors:
            pc = int(anchor['query'], 16)
            function = anchor['fn']
            start, size = int(function['addr'], 16), int(function['size'], 16)
            if start not in functions:
                ranks = map_region(source, start, min(size, 0x200), target)
                functions[start] = ranks
            ranks = functions[start] if pc == start else map_site(source, pc, target, 0x50)
            own = [{'offset': r.offset, 'votes': r.votes, 'word': target.word(r.offset)}
                   for r in ranks[:3] if 0 <= r.offset < target.text_end]
            callers = []
            if pc == start and (not ranks or ranks[0].votes < 8 or
                                (len(ranks) > 1 and ranks[0].votes < ranks[1].votes * 2)):
                _, found = map_callee(source, pc, target, max_callers=40)
                callers = [vars(r) for r in found[:3]]
            rows.append({'source': pc, 'function': function, 'source_word': source.word(pc),
                         'candidates': own, 'caller_candidates': callers, 'verified': False})
        report = {'version': version, 'build_id': target.build_id,
                  'source_digest': source.data_digest, 'target_digest': target.data_digest,
                  'verdict': 'Unverified candidates; native role, ABI and layout review required', 'sites': rows}
        (args.out / f'{version}.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'version': version, 'sites': len(rows),
                          'no_fragments': sum(not r['candidates'] for r in rows),
                          'caller_reviews': sum(bool(r['caller_candidates']) for r in rows)}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--anchors', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--versions', nargs='+', default=['1.0.0', '1.1.0', '1.1.2', '1.2.0',
                                                       '1.4.0', '1.4.1', '1.4.2', '1.4.3'])
    candidates(parser.parse_args())


if __name__ == '__main__':
    main()
