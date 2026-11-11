# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Find code and data that two Fieldworks members define differently under the same name."""
from __future__ import annotations

import argparse
import collections
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REVIEWED_VARIABLES = HERE.parent / 'checks' / 'shared-variables.txt'
FUNCTION_TYPES = set('Ww')
VARIABLE_TYPES = set('VvuDB')


def tool(toolchain: pathlib.Path, name: str) -> str:
    candidates = [toolchain / f'aarch64-none-elf-{name}.exe', toolchain / f'aarch64-none-elf-{name}']
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    sys.exit(f'missing aarch64-none-elf-{name} in {toolchain}')


def member_of(build: pathlib.Path, obj: pathlib.Path) -> str:
    parts = obj.relative_to(build).parts
    return parts[1] if parts[0] == 'members' else 'fieldworks-core'


def definitions(build: pathlib.Path, nm: str):
    found = collections.defaultdict(list)
    for obj in sorted(build.rglob('*.obj')):
        member = member_of(build, obj)
        listing = subprocess.run([nm, '-S', '--defined-only', str(obj)], capture_output=True, text=True,
                                 check=True).stdout
        for line in listing.splitlines():
            fields = line.split()
            if len(fields) != 4:
                continue
            _, size, kind, symbol = fields
            if kind in FUNCTION_TYPES or kind in VARIABLE_TYPES:
                found[symbol].append((member, kind, int(size, 16), obj))
    return found


def body(objdump: str, obj: pathlib.Path, symbol: str) -> tuple[str, ...]:
    listing = subprocess.run([objdump, '-dr', '--no-show-raw-insn', f'--disassemble={symbol}', str(obj)],
                             capture_output=True, text=True, check=True).stdout
    lines, inside = [], False
    for line in listing.splitlines():
        if line.endswith(f'<{symbol}>:'):
            inside = True
            continue
        if inside and not line.strip():
            break
        if inside:
            text = re.sub(r'^\s*[0-9a-f]+:\s*', '', line).strip()
            text = re.sub(r'\b[0-9a-f]+ <', '<', text)  # branch targets: keep the symbol, drop the offset
            lines.append(text)
    return tuple(lines)


def reviewed_variables() -> set[str]:
    if not REVIEWED_VARIABLES.exists():
        return set()
    names = set()
    for line in REVIEWED_VARIABLES.read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            names.add(line.split()[0])
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--toolchain', type=pathlib.Path, required=True)
    args = parser.parse_args()
    nm, objdump, demangle = (tool(args.toolchain, n) for n in ('nm', 'objdump', 'c++filt'))
    reviewed = reviewed_variables()

    failures, shared_functions, shared_variables = [], 0, []
    for symbol, rows in sorted(definitions(args.build, nm).items()):
        members = sorted({member for member, *_ in rows})
        if len(members) < 2:
            continue
        readable = subprocess.run([demangle, symbol], capture_output=True, text=True).stdout.strip()
        if all(kind in FUNCTION_TYPES for _, kind, _, _ in rows):
            shared_functions += 1
            bodies = collections.defaultdict(set)
            for member, _, _, obj in rows:
                bodies[member].add(body(objdump, obj, symbol))
            # A member may hold several bodies (mixed flags); two members with none in common differ.
            clashes = [(a, b) for i, a in enumerate(members) for b in members[i + 1:]
                       if not bodies[a] & bodies[b]]
            if clashes:
                variants = '; '.join(f'{m}: {sorted(len(b) for b in bodies[m])} instructions' for m in members)
                failures.append(f'function {readable}: members with no body in common '
                                f'{", ".join(f"{a}/{b}" for a, b in clashes)} ({variants})')
            continue
        sizes = sorted({size for _, _, size, _ in rows})
        shared_variables.append((readable, members, sizes))
        if len(sizes) > 1:
            failures.append(f'variable {readable}: sizes {sizes} in {",".join(members)}')
        elif symbol not in reviewed:
            failures.append(f'variable {readable} ({symbol}) shared by {",".join(members)}: not in '
                            f'{REVIEWED_VARIABLES.name}; review it and list it there')

    print(f'functions defined by two or more members: {shared_functions}')
    print(f'variables shared by two or more members: {len(shared_variables)}')
    for readable, members, sizes in shared_variables:
        print(f'  {readable}  [{",".join(members)}]  size {sizes[0]}')
    for failure in failures:
        print(f'FAIL {failure}')
    print('member symbol check: ' + ('FAILED' if failures else 'OK'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
