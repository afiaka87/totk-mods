"""Execute the linked callbacks, vendored register bridge, and owned 1.2.1 setter offline.

uv run --no-project --with unicorn --with pyelftools --with capstone python
    tests/verify_tall_speed_native.py build/subsdk4.elf <private main121.relocated.bin>
No game bytes are retained by this check.
"""
import argparse
import math
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as reg


def pack(word):
    return struct.pack('<I', word)


def branch(source, target, link=False):
    delta = target-source
    assert delta % 4 == 0 and -(1 << 27) <= delta < (1 << 27)
    return pack((0x94000000 if link else 0x14000000) | ((delta//4) & 0x3ffffff))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('main', type=Path)
    args = parser.parse_args()
    native = args.main.read_bytes()
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    base, entry, stop = 0x4000000, 0x4040000, 0x4060000
    data, stack = 0x8000000, 0x9000000
    uc.mem_map(base, 0x70000)
    uc.mem_map(data, 0x10000)
    uc.mem_map(stack, 0x10000)
    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                uc.mem_write(base+segment['p_vaddr'], segment.data())
        symbols = {s.name: base+s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        for relocation in elf.get_section_by_name('.rela.dyn').iter_relocations():
            if relocation['r_info_type'] == 1027:  # R_AARCH64_RELATIVE
                uc.mem_write(base+relocation['r_offset'], struct.pack('<Q', base+relocation['r_addend']))

    def symbol(*parts):
        matches = {value for name, value in symbols.items() if all(p in name for p in parts)}
        assert len(matches) == 1, (parts, matches)
        return matches.pop()

    def write(address, fmt, *values):
        uc.mem_write(address, struct.pack('<'+fmt, *values))

    def read(address, fmt):
        return struct.unpack('<'+fmt, uc.mem_read(address, struct.calcsize('<'+fmt)))[0]

    def call(address, *values):
        uc.reg_write(reg.UC_ARM64_REG_SP, stack+0xF000)
        for i, value in enumerate(values):
            uc.reg_write(getattr(reg, f'UC_ARM64_REG_X{i}'), value)
        uc.reg_write(reg.UC_ARM64_REG_LR, stop)
        uc.emu_start(address, stop, timeout=1000000, count=10000)
        assert uc.reg_read(reg.UC_ARM64_REG_PC) == stop

    def native_page(address):
        page = address & ~0xfff
        uc.mem_map(page, 0x1000)
        uc.mem_write(page, native[page:page+0x1000])

    for address in (0xBF8BA0, 0x38117EC, 0x3812620, 0x1C647A4):
        native_page(address)

    player, node, actor, scope = data+0x1000, data+0x3000, data+0x4000, data+0x5000
    get_player, direction, set_speed = data, data+0x100, data+0x110
    uc.mem_write(get_player, pack(0xF9400000)+pack(0xD65F03C0))  # ldr x0,[x0]; ret
    uc.mem_write(direction, pack(0xD65F03C0))
    uc.mem_write(set_speed, pack(0xD65F03C0))
    write(player+0x18, 'Q', actor)
    write(player+0x1344, '3f', 0, 100, 0)
    write(player+0x132C, '3f', 0, 600, 0)
    write(player+0xAE4, 'I', 0x100)
    write(node, 'Q', player)
    write(node+0x20, 'I', 1)
    call(symbol('speed', '4bind'), get_player)
    constructor = symbol('ApproachScopeC1')
    destructor = symbol('ApproachScopeD1')

    def install_bridge(callback, continuation=stop, displaced=b''):
        impl = symbol('exl_inline_float_hook_impl')
        uc.mem_write(entry, pack(0xF81F03FE)+branch(entry+4, impl, True)+
                     pack(0xF85F03FE)+branch(entry+12, entry+32)+struct.pack('<Q', callback))
        uc.mem_write(entry+32, displaced+branch(entry+32+len(displaced), continuation))
        uc.ctl_remove_cache(entry, entry+64)

    forward = symbol('ForwardSpeedAfterCapHook', 'Callback')
    lookahead = symbol('ApproachLookaheadHook', 'Callback')
    # Exercise the actual bridge. Every general and floating register except the intended scalar must survive.
    for callback, scalar, expected in ((forward, 8, 1.2), (lookahead, 1, 1.2)):
        write(player+0xAE4, 'I', 0x100 if scalar == 8 else 0x80)
        install_bridge(callback)
        gp = [data+0x6000+i*16 for i in range(31)]
        gp[0], gp[19], gp[30] = player, node, 0x1C566E8
        fp = [(i+1) << 64 | struct.unpack('<I', struct.pack('<f', 0.6))[0] for i in range(32)]
        for i, value in enumerate(gp):
            uc.reg_write(getattr(reg, f'UC_ARM64_REG_X{i}'), value)
        for i, value in enumerate(fp):
            uc.reg_write(getattr(reg, f'UC_ARM64_REG_Q{i}'), value)
        uc.reg_write(reg.UC_ARM64_REG_SP, stack+0xF000)
        uc.emu_start(entry, stop, timeout=1000000, count=10000)
        assert uc.reg_read(reg.UC_ARM64_REG_PC) == stop
        assert uc.reg_read(reg.UC_ARM64_REG_SP) == stack+0xF000
        for i, value in enumerate(gp):
            assert uc.reg_read(getattr(reg, f'UC_ARM64_REG_X{i}')) == value, ('GP', i)
        for i, value in enumerate(fp):
            if i == scalar:
                value = (value & ~0xffffffff) | struct.unpack('<I', struct.pack('<f', expected))[0]
            assert uc.reg_read(getattr(reg, f'UC_ARM64_REG_Q{i}')) == value, ('FP', i)

    # Run the real setter, including its native cap and native final metres/second conversion.
    # Four displaced words are sufficient here; each is position independent and register-verified above.
    hook = 0xBF8BD0
    install_bridge(forward, hook+16, native[hook:hook+16])
    uc.mem_write(hook, branch(hook, entry))
    uc.ctl_remove_cache(hook, hook+16)
    chain = [data+0x7000+i*0x100 for i in range(6)]
    for pointer, offset, target in ((actor, 0x228, chain[0]), (chain[0], 0x50, chain[1]),
                                  (chain[1], 0x20, chain[2]), (chain[2], 0x10, chain[3]),
                                  (chain[3], 8, chain[4]), (chain[4], 0, chain[5])):
        write(pointer+offset, 'Q', target)
    write(chain[5]+0x48, 'Q', direction)
    write(chain[5]+0x68, 'Q', set_speed)
    vector = data+0x6000
    write(vector, '3f', 0, 1, 0)
    commands = []

    def command_hook(machine, address, size, user_data):
        commands.append(struct.unpack('<f', struct.pack('<I', machine.reg_read(reg.UC_ARM64_REG_S0)))[0])

    uc.hook_add(UC_HOOK_CODE, command_hook, begin=set_speed, end=set_speed)
    cap = struct.unpack_from('<f', native, 0x3812620)[0]
    assert 1.8 < cap < 1.9
    cases = 0
    movement_cases = [
        ('travel', 0x100, 500, 0x1C566E8, False, 0.6, 2),
        ('travel capped', 0x100, 500, 0x1C566E8, False, 10, 2),
        ('short', 0x100, 150, 0x1C566E8, False, 0.6, 1),
        ('cancel', 0x80100, 500, 0x1C566E8, False, 0.6, 1),
        ('preview', 0x500, 500, 0x1C566E8, False, 0.6, 1),
        ('unrelated caller', 0x100, 500, stop, False, 0.6, 1),
        ('approach', 0x80, 500, stop, True, 0.6, 2),
        ('approach animation', 0x80, 500, stop, False, 0.6, 1),
        ('downward', 0x100, -500, 0x1C566E8, False, 0.6, 1),
    ]
    tiers = [(0, 1), (150, 1), (151, 1.5), (300, 1.5), (301, 2), (500, 2), (501, 3),
             (1000, 3), (1001, 6), (1500, 6), (1501, 7), (2000, 7), (2001, 7.5),
             (10000, 7.5), (10001, 1)]
    for rise, factor in tiers:
        movement_cases.append((f'approach {rise}', 0x80, rise, stop, True, 0.6, factor))
        movement_cases.append((f'capped travel {rise}', 0x100, rise, 0x1C566E8, False, 10, factor))
    for phase, flags, rise, caller, scoped, native_step, factor in movement_cases:
        write(player+0xAE4, 'I', flags)
        write(player+0x132C, '3f', 0, 100+rise, 0)
        if scoped:
            call(constructor, scope, node)
        uc.reg_write(reg.UC_ARM64_REG_SP, stack+0xF000)
        uc.reg_write(reg.UC_ARM64_REG_X0, player)
        uc.reg_write(reg.UC_ARM64_REG_X1, vector)
        uc.reg_write(reg.UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', native_step))[0])
        uc.reg_write(reg.UC_ARM64_REG_LR, caller)
        uc.emu_start(0xBF8BA0, caller, timeout=1000000, count=10000)
        assert uc.reg_read(reg.UC_ARM64_REG_PC) == caller, phase
        assert len(commands) == cases+1, phase
        assert math.isclose(commands[-1], min(native_step, cap)*factor*30, rel_tol=1e-6), (phase, commands[-1])
        if scoped:
            call(destructor, scope)
        cases += 1

    # Falsify the historical load-site strategy with the actual displaced load.
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    old = next(decoder.disasm(native[0x1C64BA0:0x1C64BA4], 0x1C64BA0))
    assert old.mnemonic == 'ldr' and old.op_str.startswith('s0, [x')
    register = int(old.op_str.split('[x')[1].split(',')[0].split(']')[0])
    displacement = int(old.op_str.split('#')[1].rstrip(']'), 16)
    uc.reg_write(getattr(reg, f'UC_ARM64_REG_X{register}'), 0x3811000)
    # Read the matching native constant via the actual instruction; its page is the prior ADRP's target.
    assert math.isclose(read(0x3811000+displacement, 'f'), 0.6, rel_tol=1e-6)
    uc.reg_write(reg.UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', 1.5))[0])
    uc.emu_start(0x1C64BA0, 0x1C64BA4, count=1)
    reloaded = struct.unpack('<f', struct.pack('<I', uc.reg_read(reg.UC_ARM64_REG_S0)))[0]
    assert math.isclose(reloaded, 0.6, rel_tol=1e-6)

    camera_names = ('ApproachCamera', 'PauseCameraHook', 'LookAtCameraHook', 'ApproachLeaveHook', '6camera')
    assert not any(part in name for name in symbols for part in camera_names), 'camera intervention remains linked'
    print('PASS: fixed camera build contains no retired camera adapter or hook symbols.')
    print(f'PASS: compiled inline bridge preserves GP/SIMD; {cases} native setter cases; old load overwrites override.')
    print(f'Native cap={cap:.9f}; GSI approach command=108 m/s; maximum capped travel command={cap*7.5*30:.3f} m/s.')


if __name__ == '__main__':
    main()
