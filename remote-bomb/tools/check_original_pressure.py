# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Compare the portable pressure rule with isolated owned BotW 1.5.0 ARM code."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from unicorn import Uc, UC_ARCH_ARM64, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm64_const import (
    UC_ARM64_REG_CPACR_EL1, UC_ARM64_REG_FPCR, UC_ARM64_REG_LR,
    UC_ARM64_REG_PC, UC_ARM64_REG_S0, UC_ARM64_REG_SP,
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
)

ENTRY, END = 0x127CAB4, 0x127CD7C
ORIGIN_GETTER, OBSTRUCTION_QUERY, RETURN = 0x500000, 0xE412D0, 0x500100
WRAPPER, ELEMENT, CONFIG, VTABLE = 0x400000, 0x400100, 0x400200, 0x400700
ORIGIN, CONTACT, DIRECTION, ZERO = 0x400800, 0x400850, 0x400880, 0x400A00


def bits(value: float) -> int:
    return struct.unpack('<I', struct.pack('<f', value))[0]


def cases() -> list[tuple[int, ...]]:
    samples = []
    for radius in (0., -1., .001, .01, .25, .5, 1., 4., 20.):
        for distance in (0., .001, .5, 1., 2., 4., 4.25, 5., 6., 30.):
            for blocked in (0, 1):
                samples.append((bits(distance), 0, 0, bits(radius), bits(4.), blocked))
    for threshold in (bits(4.25)-1, bits(4.25), bits(4.25)+1):
        samples.append((threshold, 0, 0, bits(.25), bits(4.), 0))
    randomizer = random.Random(0x127CAB4)
    for _ in range(600):
        vector = [bits(randomizer.uniform(-12, 12)) for _ in range(3)]
        radius = bits(randomizer.choice((.001, .25, .5, 1., 4., 20.)))
        speed = bits(randomizer.choice((.5, 1., 4., 15.)))
        samples.append((*vector, radius, speed, 0))
    return samples


class OriginalPressure:
    def __init__(self, image: bytes):
        self.machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.blocked = 0
        self.obstruction_calls = 0
        for address in (0x127C000, 0x1DA2000, 0x1DA3000):
            self.machine.mem_map(address, 0x1000)
            self.machine.mem_write(address, image[address:address+0x1000])
        for address, size in ((0x400000, 0x1000), (0x500000, 0x1000),
                              (0x600000, 0x10000), (0xE41000, 0x1000), (0x2579000, 0x1000)):
            self.machine.mem_map(address, size)
        self.machine.mem_write(WRAPPER, struct.pack('<Q', VTABLE))
        self.machine.mem_write(WRAPPER+16, struct.pack('<Q', ELEMENT))
        self.machine.mem_write(ELEMENT+8, struct.pack('<Q', CONFIG))
        self.machine.mem_write(ELEMENT+120, struct.pack('<I', 1))
        self.machine.mem_write(CONFIG+496, struct.pack('<I', 2))
        self.machine.mem_write(VTABLE+48, struct.pack('<Q', ORIGIN_GETTER))
        self.machine.mem_write(0x2579298, struct.pack('<Q', ZERO))
        self.machine.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
        self.machine.reg_write(UC_ARM64_REG_FPCR, 0)
        self.machine.hook_add(UC_HOOK_CODE, self.intercept)

    def intercept(self, machine: Uc, address: int, _size: int, _data: object) -> None:
        if address == ORIGIN_GETTER:
            machine.reg_write(UC_ARM64_REG_X0, ORIGIN)
        elif address == OBSTRUCTION_QUERY:
            self.obstruction_calls += 1
            machine.reg_write(UC_ARM64_REG_X0, self.blocked)
        else:
            return
        machine.reg_write(UC_ARM64_REG_PC, machine.reg_read(UC_ARM64_REG_LR))

    def run(self, sample: tuple[int, ...]) -> tuple[int, ...]:
        x, y, z, radius, speed, self.blocked = sample
        self.machine.mem_write(CONTACT, struct.pack('<III', x, y, z))
        self.machine.mem_write(ELEMENT+68, struct.pack('<I', speed))
        self.machine.reg_write(UC_ARM64_REG_X0, WRAPPER)
        self.machine.reg_write(UC_ARM64_REG_X1, DIRECTION)
        self.machine.reg_write(UC_ARM64_REG_X2, CONTACT)
        self.machine.reg_write(UC_ARM64_REG_X3, 0)
        self.machine.reg_write(UC_ARM64_REG_S0, radius)
        self.machine.reg_write(UC_ARM64_REG_SP, 0x60F000)
        self.machine.reg_write(UC_ARM64_REG_LR, RETURN)
        self.machine.emu_start(ENTRY, RETURN, count=5000)
        if self.machine.reg_read(UC_ARM64_REG_PC) != RETURN:
            raise RuntimeError('Original pressure calculation did not return within its instruction budget')
        direction = struct.unpack('<III', self.machine.mem_read(DIRECTION, 12))
        return (*direction, self.machine.reg_read(UC_ARM64_REG_S0))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--botw-image', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    image = args.botw_image.read_bytes()
    if struct.unpack_from('<I', image, ENTRY)[0] != 0xFC190FEC:
        raise RuntimeError('Image does not match the verified original pressure entry')
    actual_constants = [struct.unpack_from('<I', image, address)[0]
                        for address in (0x1DA26D4, 0x1DA27BC, 0x1DA3378)]
    if actual_constants != [0x3ECCCCCC, 0x40333333, 0xBECCCCCC]:
        raise RuntimeError('Original pressure constants differ from the verified image')
    samples = cases()
    original = OriginalPressure(image)
    expected = [original.run(sample) for sample in samples]
    result = subprocess.run([str(args.executable.resolve()), '--samples'],
                            input=''.join(' '.join(map(str, sample))+'\n' for sample in samples),
                            text=True, capture_output=True, check=True)
    observed = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    mismatches = [dict(index=index, input=sample, original=reference, portable=actual)
                  for index, (sample, reference, actual) in enumerate(zip(samples, expected, observed))
                  if reference != actual]
    if len(observed) != len(samples):
        raise RuntimeError(f'Portable calculation returned {len(observed)} results for {len(samples)} cases')
    receipt = dict(image_sha256=hashlib.sha256(image).hexdigest(),
                   function_start=hex(ENTRY), function_end=hex(END),
                   function_sha256=hashlib.sha256(image[ENTRY:END]).hexdigest(),
                   executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                   cases=len(samples), bit_exact=len(samples)-len(mismatches),
                   obstruction_calls=original.obstruction_calls, mismatches=mismatches[:10],
                   conditions='Finite inputs; original effect_type2/mode1; synthetic origin and obstruction result; no live engine')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt))
    if mismatches:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
