# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Execute the built Cryonis adapter against the owned TotK 1.2.1 group getter."""
import argparse
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0
from unicorn.arm64_const import UC_ARM64_REG_X8, UC_ARM64_REG_X30

MOD, GAME, DATA, STACK, STOP = (0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000)
GETTER, GETTER_END = 0x84E674, 0x84E6B4


def getter_bytes(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            start = segment['p_vaddr']
            if start <= GETTER and GETTER_END <= start + segment['p_filesz']:
                code = segment.data()[GETTER - start:GETTER_END - start]
                assert code[:8] == bytes.fromhex('090040f9890100b4')
                assert code[0x24:0x28] == bytes.fromhex('0a0100b9')
                return code
    raise AssertionError('TotK 1.2.1 getter not found')


def run_case(path, code, broken_link, initial_x8, group_id, enabled):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        end = max(s['p_vaddr'] + s['p_memsz'] for s in segments)
        uc.mem_map(MOD, (end + 4095) & ~4095)
        for segment in segments:
            uc.mem_write(MOD + segment['p_vaddr'], segment.data())
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        entry = next(s['st_value'] for s in symbols if s.name.startswith(
            '_ZN7cryonis6engine14collisionGroupE'))
        base = next(s['st_value'] for s in symbols if s.name == '_ZN7cryonis6engine10moduleBaseE')
    uc.mem_write(MOD + base, struct.pack('<Q', GAME))
    uc.mem_map((GAME + GETTER) & ~4095, 4096)
    uc.mem_write(GAME + GETTER, code)
    uc.mem_map(DATA, 0x10000)
    uc.mem_map(STACK, 0x10000)
    uc.mem_map(STOP, 4096)
    reference, actor, registry, physics, controller, canary = (
        DATA + offset for offset in (0, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000))
    links = ((reference, actor), (actor + 0x228, registry),
             (registry + 0x50, physics), (physics + 0x20, controller))
    for index, (address, pointer) in enumerate(links):
        uc.mem_write(address, struct.pack('<Q', 0 if index == broken_link else pointer))
    uc.mem_write(controller + 0xC8, struct.pack('<IB', group_id, enabled))
    uc.mem_write(canary, b'\xa5' * 16)
    uc.reg_write(UC_ARM64_REG_SP, STACK + 0xF000)
    uc.reg_write(UC_ARM64_REG_X0, reference)
    uc.reg_write(UC_ARM64_REG_X8, initial_x8)
    uc.reg_write(UC_ARM64_REG_X30, STOP)
    try:
        uc.emu_start(MOD + entry, STOP, count=200)
    except UcError as error:
        return False, f'{error} at PC={uc.reg_read(UC_ARM64_REG_PC):#x}'
    expected = 0 if broken_link >= 0 else group_id | (enabled << 32)
    okay = (uc.reg_read(UC_ARM64_REG_PC) == STOP
            and uc.reg_read(UC_ARM64_REG_X0) & 0xFFFFFFFFFF == expected
            and bytes(uc.mem_read(canary, 16)) == b'\xa5' * 16)
    return okay, 'returned group or protected canary mismatch' if not okay else 'pass'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--expect-broken', action='store_true')
    args = parser.parse_args()
    code = getter_bytes(args.game)
    failures = []
    total = 0
    for broken_link in (-1, 0, 1, 2, 3):
        for initial_x8 in (0, 1, DATA + 0x5000):
            for group_id, enabled in ((0x145C, 1), (0xFEDCBA98, 0)):
                total += 1
                okay, reason = run_case(args.elf, code, broken_link, initial_x8, group_id, enabled)
                if not okay:
                    failures.append((broken_link, initial_x8, reason))
    if args.expect_broken:
        assert len(failures) == total, (len(failures), total)
        print(f'Prior adapter: {total}/{total} cases reproduce invalid write or result corruption')
        print(f'First failure: {failures[0]}')
    else:
        assert not failures, failures[:3]
        print(f'Native group ABI: {total}/{total} ARM cases pass; result and canaries intact')


if __name__ == '__main__':
    main()
