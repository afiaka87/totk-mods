# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Compare pressure response with isolated owned BotW 1.5.0 ARM instructions."""
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
    UC_ARM64_REG_X19, UC_ARM64_REG_X22, UC_ARM64_REG_X23,
)
from check_original_pressure import bits

ENTRY, END = 0xD965CC, 0xD966FC
TARGET, MATERIAL, SHAPE, VTABLE, ZERO = 0x400000, 0x400300, 0x400600, 0x400700, 0x400900
OCCLUSION_GETTER, STACK = 0x500000, 0x60F000


def cases() -> list[tuple[int, ...]]:
    samples = []
    for speed in (0., 1., 340., 599., 600., 601., 900.):
        for area in (.01, 1., 12.):
            for occlusion in (0., .5, 1.):
                for limited in (0, 1):
                    samples.append((bits(speed), 0, 0, bits(area), bits(occlusion), bits(1.), limited))
    for speed in (bits(600.)-1, bits(600.), bits(600.)+1):
        samples.append((speed, 0, 0, bits(1.), bits(1.), bits(1.), 1))
    randomizer = random.Random(0xD96614)
    for _ in range(600):
        velocity = [bits(randomizer.uniform(-1500, 1500)) for _ in range(3)]
        samples.append((*velocity, bits(randomizer.choice((.001, .1, .25, 1., 12., 64.))),
                        bits(randomizer.choice((0., .25, .5, .7, 1.))),
                        bits(randomizer.choice((0., .1, .5, 1., 2.))), randomizer.randrange(2)))
    return samples


class OriginalImpulse:
    def __init__(self, image: bytes):
        self.machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.occlusion = 0
        self.occlusion_calls = 0
        for address in (0xD96000, 0x1DA2000):
            self.machine.mem_map(address, 0x1000)
            self.machine.mem_write(address, image[address:address+0x1000])
        for address, size in ((0x400000, 0x1000), (0x500000, 0x1000), (0x600000, 0x10000)):
            self.machine.mem_map(address, size)
        self.machine.mem_write(TARGET+32, struct.pack('<Q', MATERIAL))
        self.machine.mem_write(TARGET+40, struct.pack('<Q', SHAPE))
        self.machine.mem_write(SHAPE, struct.pack('<Q', VTABLE))
        self.machine.mem_write(VTABLE+24, struct.pack('<Q', OCCLUSION_GETTER))
        self.machine.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
        self.machine.reg_write(UC_ARM64_REG_FPCR, 0)
        self.machine.hook_add(UC_HOOK_CODE, self.intercept)

    def intercept(self, machine: Uc, address: int, _size: int, _data: object) -> None:
        if address != OCCLUSION_GETTER:
            return
        self.occlusion_calls += 1
        machine.reg_write(UC_ARM64_REG_S0, self.occlusion)
        machine.reg_write(UC_ARM64_REG_PC, machine.reg_read(UC_ARM64_REG_LR))

    def run(self, sample: tuple[int, ...]) -> tuple[int, ...]:
        x, y, z, area, self.occlusion, scale, limited = sample
        self.machine.mem_write(STACK+64, struct.pack('<IIII', x, 0, z, y))
        self.machine.mem_write(TARGET+12, struct.pack('<I', 0 if limited else 0x800000))
        self.machine.mem_write(TARGET+56, struct.pack('<I', area))
        self.machine.mem_write(TARGET+332, struct.pack('<I', scale))
        self.machine.mem_write(TARGET+320, bytes(12))
        self.machine.reg_write(UC_ARM64_REG_X19, TARGET)
        self.machine.reg_write(UC_ARM64_REG_X22, 0)
        self.machine.reg_write(UC_ARM64_REG_X23, ZERO)
        self.machine.reg_write(UC_ARM64_REG_SP, STACK)
        self.machine.emu_start(ENTRY, END, count=2000)
        if self.machine.reg_read(UC_ARM64_REG_PC) != END:
            raise RuntimeError('Original response did not reach its verified end within the instruction budget')
        return struct.unpack('<III', self.machine.mem_read(TARGET+320, 12))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--botw-image', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    image = args.botw_image.read_bytes()
    if struct.unpack_from('<I', image, ENTRY)[0] != 0xBD4002E0:
        raise RuntimeError('Image does not match the verified original response entry')
    constants = [struct.unpack_from('<I', image, address)[0] for address in (0x1DA23E0, 0x1DA24D4)]
    if constants != [0x3D4CCCCD, 0x44160000]:
        raise RuntimeError('Original pressure response constants differ from the verified image')
    samples = cases()
    original = OriginalImpulse(image)
    expected = [original.run(sample) for sample in samples]
    result = subprocess.run([str(args.executable.resolve()), '--impulse-samples'],
                            input=''.join(' '.join(map(str, sample))+'\n' for sample in samples),
                            text=True, capture_output=True, check=True)
    observed = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    if len(observed) != len(samples):
        raise RuntimeError(f'Portable response returned {len(observed)} results for {len(samples)} cases')
    mismatches = [dict(index=index, input=sample, original=reference, portable=actual)
                  for index, (sample, reference, actual) in enumerate(zip(samples, expected, observed))
                  if reference != actual]
    receipt = dict(image_sha256=hashlib.sha256(image).hexdigest(),
                   slice_start=hex(ENTRY), slice_end=hex(END),
                   slice_sha256=hashlib.sha256(image[ENTRY:END]).hexdigest(),
                   executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                   cases=len(samples), bit_exact=len(samples)-len(mismatches),
                   occlusion_calls=original.occlusion_calls, mismatches=mismatches[:10],
                   conditions='Finite inputs; response allowed; zero background force; synthetic occlusion getter; no live engine')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt))
    if mismatches:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
