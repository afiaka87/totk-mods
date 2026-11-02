"""Compare emitted community ports with the exact pinned 1.2.1 instructions."""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as arm

from community_port import port, SOURCE_START, SOURCE_END
from community_profile_data import SITES, EXTRA
from native_profile_data import BINDINGS, VERSIONS

HEAP, STACK, STOP = 0x10000000, 0x20000000, 0x30000000
GPRS = [getattr(arm, f"UC_ARM64_REG_X{i}") for i in range(29)] + [arm.UC_ARM64_REG_X29, arm.UC_ARM64_REG_X30]
FPRS = [getattr(arm, f"UC_ARM64_REG_Q{i}") for i in range(32)]


class Machine:
    def __init__(self, image, payload, *, reference=False):
        self.image, self.payload, self.reference = image, payload, reference
        self.cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.pages = set()
        self.map(HEAP, 0x20000)
        self.map(STACK - 0x4000, 0x8000)
        self.map(STOP, 0x1000)
        self.calls, self.stops, self.trace = {}, set(), []
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)
        self.cpu.reg_write(arm.UC_ARM64_REG_SP, STACK)
        self.cpu.reg_write(GPRS[30], STOP)
        for i, register in enumerate(GPRS[:30]):
            self.cpu.reg_write(register, 0x44000000 + i)
        for i, register in enumerate(FPRS):
            self.cpu.reg_write(register, i + (i << 80))
        if reference:
            for pc, word in payload.items():
                if SOURCE_START <= pc < SOURCE_END:
                    self.put(pc, "I", word)
        else:
            for pc, word, _ in payload["instructions"]:
                self.put(pc, "I", word)
            for pc, word in payload["literals"].items():
                self.put(pc, "I", word)

    def map(self, address, size):
        for page in range(address & ~0xfff, (address + size + 0xfff) & ~0xfff, 0x1000):
            if page not in self.pages:
                self.cpu.mem_map(page, 0x1000)
                self.pages.add(page)

    def put(self, address, fmt, *values):
        data = struct.pack("<" + fmt, *values)
        self.map(address, len(data))
        self.cpu.mem_write(address, data)

    def get(self, address, fmt):
        return struct.unpack("<" + fmt, self.cpu.mem_read(address, struct.calcsize(fmt)))

    def label(self, source):
        return source if self.reference else self.payload["labels"][source]

    def native(self, source):
        return source if self.reference else SITES[source][self.image.version]

    def stub(self, source, value):
        address = self.native(source)
        self.map(address, 4)
        self.calls[address] = value

    def hook(self, cpu, address, size, _):
        if address == STOP or address in self.stops:
            cpu.emu_stop()
        elif address in self.calls:
            self.trace.append((address, tuple(cpu.reg_read(r) for r in GPRS[:4])))
            result = self.calls[address]
            if callable(result):
                result = result(self)
            if result is not None:
                cpu.reg_write(GPRS[0], result)
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))

    def run(self, source, *, native_stop=None, label_stop=None):
        self.stops = {self.native(a) for a in native_stop or []}
        self.stops.update(self.label(a) for a in label_stop or [])
        for address in self.stops:
            self.map(address, 4)
        self.cpu.emu_start(self.label(source), STOP + 4, count=5000)
        assert self.cpu.reg_read(arm.UC_ARM64_REG_PC) in self.stops | {STOP}

    def snapshot(self):
        return (bytes(self.cpu.mem_read(HEAP, 0x10000)), self.cpu.reg_read(arm.UC_ARM64_REG_SP),
                tuple(self.cpu.reg_read(r) for r in GPRS[19:30]))


def verify(path: Path, out: Path):
    from totk_version_port.images import MainImage
    from patch_gaps.formats import parse_pchtxt
    _, writes = parse_pchtxt(path.read_text())
    exact = {w.off + i: struct.unpack_from("<I", w.data, i)[0]
             for w in writes if w.enabled for i in range(0, len(w.data), 4)}
    base = MainImage("1.2.1")
    result = []
    for version in VERSIONS:
        image = MainImage(version)
        emitted = port(path, version, image)
        counts = dict(acceleration=0, target_validity=0, return_target=0, guide=0, terrain_reload=0,
                      floor_fallback=0, ceiling_span=0, arrival_speed=0, speed_prefix=0)
        def pair():
            return (Machine(base, exact, reference=True), Machine(image, emitted))
        counts.update(wrapper_cases(pair, version, image, emitted))
        for state in (0, 1, 2, 3):
            for elapsed in (0, 149, 150, 180, 300, 600, 1200):
                for flags in (0, 0x80000):
                    snapshots = []
                    for m in pair():
                        node, player = HEAP, HEAP + 0x3000
                        m.cpu.reg_write(GPRS[0], node)
                        m.put(node + 0x20, "I", state)
                        m.put(node + 0x40, "f", elapsed)
                        m.put(node + 0x50, "f", 150)
                        m.put(node + 0x4c, "f", 0.6)
                        m.put(player + 0xae4, "I", flags)
                        m.stub(0x107e9a0, player)
                        m.stub(0xec98cc, 0)
                        m.run(0x2b19ca0, label_stop=[0x2b19d34])
                        snapshots.append(m.snapshot())
                    assert snapshots[0] == snapshots[1], (version, "acceleration", state, elapsed, flags)
                    counts["acceleration"] += 1
        for accepted in (0, 1):
            for flags in (0, 0x40, 0x80, 0x400, 0x4b8, 0x800, 0xffff):
                snapshots = []
                for m in pair():
                    m.cpu.reg_write(GPRS[0], HEAP)
                    m.put(HEAP + 0x7c, "H", flags)
                    m.stub(0x175c99c, accepted)
                    m.run(0x2b19a84)
                    snapshots.append((m.snapshot(), m.cpu.reg_read(GPRS[0])))
                assert snapshots[0] == snapshots[1], (version, "validity", flags, accepted)
                counts["target_validity"] += 1
        # Check the native guide register and replay independently of its number.
        for active in (False, True):
            for elapsed in (0, 89, 90, 150):
                observations = []
                for m in pair():
                    player, guide, var = HEAP, HEAP + 0x3000, HEAP + 0x6000
                    slot = m.native(0x462fec8)
                    m.put(slot, "Q", var)
                    m.put(var, "Q", HEAP + 0x7000)
                    m.put(player + 0xae4, "I", 0x1000 if active else 0)
                    m.put(guide + 0x1d8, "f", elapsed)
                    m.cpu.reg_write(GPRS[19], player)
                    m.stub(0x11eb06c, guide)
                    m.stub(0xec98cc, 0)
                    m.run(0x2b19f8c, native_stop=[0x695ab0], label_stop=[0x2b19fe4])
                    forced = m.cpu.reg_read(arm.UC_ARM64_REG_PC) == m.native(0x695ab0)
                    register = 21 if not m.reference and version.startswith("1.4.") else 22
                    observations.append((forced, m.cpu.reg_read(GPRS[register]) if forced else None,
                                         m.get(guide + 0x1d8, "Q")))
                assert observations[0] == observations[1], (version, "guide", active, elapsed)
                counts["guide"] += 1
        for initialized, button, pending, ready in ((a, b, c, d)
                for a in (0, 1) for b in (0, 2) for c in (0, 8) for d in range(8)):
            observations = []
            for m in pair():
                node, player, var, module, controller = [HEAP + a for a in (0, 0x3000, 0x6000, 0x7000, 0x8000)]
                m.cpu.reg_write(GPRS[19], node)
                m.put(node + 0x76, "BB", initialized, pending)
                m.put(m.native(0x462ff90), "Q", var)
                m.put(var, "Q", module)
                m.put(module + 0xa0, "Q", controller)
                m.put(controller + 8, "B", button)
                member = 0x1344 if m.reference else BINDINGS['player::kReturnTarget'][version]
                m.put(player + member, "3f", 12.5, -456.0, 789.25)
                m.stub(0x107e9a0, player)
                m.stub(0xbc7610, HEAP + 0x9000)
                m.stub(0x1957db4, ready & 1)
                m.stub(0x1957e50, (ready >> 1) & 1)
                m.stub(0x1c57848, 0)
                positions = []
                def position(machine):
                    positions.append(machine.get(machine.cpu.reg_read(GPRS[1]), "3f"))
                    return 0
                m.stub(0xe8daf0, position)
                if not m.reference and version.startswith('1.4.'):
                    ex = EXTRA[version]
                    m.put(ex['terrainModuleSlot'], 'Q', HEAP + 0xa000)
                    m.put(HEAP + 0xa000, 'Q', HEAP + 0xb000)
                    for address, value in ((ex['terrainPoint'], 0), (ex['terrainReady'], (ready >> 2) & 1)):
                        m.map(address, 4)
                        m.calls[address] = value
                else:
                    m.stub(0x1956e08, (ready >> 2) & 1)
                exits = (0x1c57758, 0x1c57820, 0x1c5783c)
                m.run(0x2b19e70, native_stop=exits)
                pc = m.cpu.reg_read(arm.UC_ARM64_REG_PC)
                observations.append((exits.index(next(a for a in exits if m.native(a) == pc)),
                                     m.get(node + 0x74, '4B'), positions,
                                     m.cpu.reg_read(arm.UC_ARM64_REG_SP), m.cpu.reg_read(GPRS[20])))
            assert observations[0] == observations[1], (version, 'return', initialized, button, pending, ready)
            counts['return_target'] += 1
        if version.startswith('1.4.'):
            m = Machine(image, emitted)
            ex = EXTRA[version]
            variable, first, second = HEAP + 0x100, HEAP + 0x200, HEAP + 0x300
            m.put(ex['terrainModuleSlot'], 'Q', variable)
            m.put(variable, 'Q', first)
            m.cpu.reg_write(GPRS[0], HEAP + 0x400)
            def change_module(machine):
                assert tuple(machine.cpu.reg_read(r) for r in GPRS[:4]) == (first, 0, HEAP + 0x400, 1)
                machine.put(variable, 'Q', second)
                return 0
            def check_module(machine):
                assert tuple(machine.cpu.reg_read(r) for r in GPRS[:4]) == (second, 0, 16, 16)
                return 1
            for address, callback in ((ex['terrainPoint'], change_module), (ex['terrainReady'], check_module)):
                m.map(address, 4)
                m.calls[address] = callback
            before = tuple(m.cpu.reg_read(r) for r in GPRS[19:30])
            m.run(0x8000000)
            assert m.cpu.reg_read(GPRS[0]) == 1 and m.cpu.reg_read(arm.UC_ARM64_REG_SP) == STACK
            assert tuple(m.cpu.reg_read(r) for r in GPRS[19:30]) == before
            counts['terrain_reload'] += 1
        result.append(dict(version=version, **counts))
    out.write_text(json.dumps(dict(cases=result, verdict="Offline instruction comparison only"), indent=2) + "\n")
    print(json.dumps(result))


def wrapper_cases(pair, version, image, emitted):
    from totk_version_port import arm64
    counts = dict(floor_fallback=0, ceiling_span=0, arrival_speed=0, speed_prefix=0)
    # Exercise native-success, allowed failures, invalid floats, nearby cells, ceiling clearance and obstruction through the original fallback code.
    variants = [(1,0,0,20,40,100,0), (0,0,0,20,40,100,0),
                (0,0x2080,0,20,40,100,0), (0,1,0,20,40,100,0),
                (0,0,9,20,40,100,0), (0,0,0,float('nan'),40,100,0),
                (0,0,0,9,40,100,0), (0,0,0,20,19,100,0),
                (0,0,0,20,10011,10020,0), (0,0,0,20,40,41,0),
                (0,0,0,20,40,float('nan'),0), (0,0,0,20,40,100,1)]
    for accepted, reasons, x, first, end, next_solid, blocked in variants:
        snapshots = []
        for m in pair():
            clipper,player,start,out,flag = [HEAP+n for n in (0,0x1000,0x5000,0x5100,0x5200)]
            for reg,value in zip(GPRS[:5],(clipper,player,start,out,flag)):m.cpu.reg_write(reg,value)
            m.put(clipper+0x7c,'H',reasons)
            m.put(start,'3f',0,10,0)
            m.put(player+0x2508,'I5f',1,x,0,first,end,next_solid)
            m.put(flag,'B',1)
            m.stub(0x175d0b4,accepted)
            m.stub(0x175e69c,blocked)
            m.run(0x2b19ae0)
            snapshots.append((m.snapshot(),m.cpu.reg_read(GPRS[0])))
        assert snapshots[0] == snapshots[1], (version,'floor fallback',variants.index(
            (accepted,reasons,x,first,end,next_solid,blocked)))
        counts['floor_fallback'] += 1
    for speed in (18.,144.):
        for frames in (1.,7.):
            for distance in (1.,20.,500.):
                spans, speeds = [], []
                for m in pair():
                    node,actor,params = HEAP,HEAP+0x2000,HEAP+0x6000
                    m.put(node+0x30,'2f',0,100+distance)
                    m.put(node+0x40,'2f',frames+10,10)
                    m.put(node+0x4c,'f',speed)
                    m.put(actor+(0x2b8 if m.reference else BINDINGS['actor::kPosition'][version]+4),'f',100)
                    m.cpu.reg_write(GPRS[19],node)
                    m.cpu.reg_write(GPRS[20],node)
                    m.cpu.reg_write(GPRS[22],actor)
                    m.cpu.reg_write(GPRS[8],params)
                    at=m.native(0x1c64628)
                    member=arm64.load_store_unsigned(m.image.word(at))[2]
                    m.put(params+member,'f',2)
                    m.run(0x2b19d60,native_stop=[0x1c6462c])
                    spans.append(m.cpu.reg_read(arm.UC_ARM64_REG_S0))
                    m.cpu.reg_write(GPRS[19],node)
                    m.run(0x2b19d90,native_stop=[0x1c64b74])
                    speeds.append(m.cpu.reg_read(GPRS[23]))
                assert spans[0]==spans[1],(version,'near-ceiling span',speed,frames,distance)
                assert speeds[0]==speeds[1],(version,'arrival speed',speed,frames,distance)
                counts['ceiling_span']+=1;counts['arrival_speed']+=1
    # Retain the native frame and all values consumed after the bypassed clamp. This is an ABI check; it does not emulate movement or native rendering.
    for speed in (0.6,4.8):
        m=Machine(image,emitted)
        player,actor,components,direction=[HEAP+n for n in (0,0x2000,0x4000,0x6000)]
        m.cpu.reg_write(GPRS[0],player);m.cpu.reg_write(GPRS[1],direction)
        m.put(player+0x18,'Q',actor)
        m.put(actor+BINDINGS['actor::kComponents'][version],'Q',components)
        m.put(direction,'3f',3,4,5)
        bits=struct.unpack('<I',struct.pack('<f',speed))[0]
        m.cpu.reg_write(arm.UC_ARM64_REG_S0,bits)
        m.run(0x2b19de0,native_stop=[0xbf8bd0])
        newer=version.startswith('1.4.')
        assert m.cpu.reg_read(arm.UC_ARM64_REG_SP)==STACK-(0x30 if newer else 0x40)
        value_register=arm.UC_ARM64_REG_S0 if newer and version!='1.4.1' else arm.UC_ARM64_REG_S8
        assert m.cpu.reg_read(value_register)==bits
        if newer:
            assert m.cpu.reg_read(GPRS[8])==components
            if version!='1.4.1':
                for register,value in ((2,4.),(3,3.),(4,25.),(5,-1.),(6,16.)):
                    assert m.cpu.reg_read(getattr(arm,f'UC_ARM64_REG_S{register}'))==struct.unpack('<I',struct.pack('<f',value))[0]
        counts['speed_prefix']+=1
    return counts


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--community", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.root / "scripts"))
    verify(args.community, args.out)
