"""Confirm selected padding in a local IDA database without exporting game bytes."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    selected = set()
    layouts = {}
    for variant in ('standard', 'restricted'):
        folder = args.matrix / args.version / variant / 'base'
        raw = (folder / 'placement.words').read_bytes()
        layouts[variant] = hashlib.sha256(raw).hexdigest()
        writes = {a for a, _ in struct.iter_unpack('<II', raw)}
        for address, size in json.loads((folder / 'inputs.json').read_text())['gaps']:
            if any(address + i in writes for i in range(0, size, 4)):
                selected.add((address, size))

    # IDA's installed Python environment must import idapro before its other modules.
    import idapro
    import ida_auto
    import ida_funcs
    import ida_gdl
    import ida_xref
    import idautils

    print(f'Opening {args.version} for native analysis', flush=True)
    if idapro.open_database(str(args.image.resolve()), run_auto_analysis=True):
        raise RuntimeError('IDA could not open the local image')
    try:
        if not ida_auto.auto_wait() or ida_funcs.get_func_qty() < 10000:
            raise RuntimeError('Native analysis is incomplete')
        spans, xrefs, block_hits = [], [], []
        functions = {}
        for address, size in sorted(selected):
            for endpoint in {address, address + size - 4}:
                fn = ida_funcs.get_func(endpoint)
                if fn is None:
                    continue
                if fn.start_ea not in functions:
                    functions[fn.start_ea] = [(b.start_ea, b.end_ea) for b in ida_gdl.FlowChart(fn)]
                spans.append(dict(endpoint=endpoint, function=fn.start_ea))
                for start, end in functions[fn.start_ea]:
                    if start < address + size and address < end:
                        block_hits.append(dict(gap=address, start=start, end=end))
            for ref in idautils.XrefsTo(address, ida_xref.XREF_ALL):
                xrefs.append(dict(target=address, source=ref.frm, type=ref.type))
        result = dict(version=args.version, selected_gaps=len(selected),
                      functions=ida_funcs.get_func_qty(), placement_sha256=layouts,
                      function_range_matches=spans, executable_block_overlaps=block_hits,
                      xrefs=xrefs, passed=not block_hits and not xrefs)
        args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({k: result[k] for k in ('version', 'selected_gaps', 'functions', 'passed')}), flush=True)
        if not result['passed']:
            raise SystemExit('Inspect the retained overlaps/references before capture')
    finally:
        idapro.close_database()


if __name__ == '__main__':
    main()
