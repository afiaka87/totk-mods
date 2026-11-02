"""Check the native travel fields and voxel call ABI used by each port."""
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_MEM


def verify(image, bindings, version):
    from native_fixtures import derive
    from native_profile_data import BINDINGS
    from totk_version_port.images import MainImage
    from totk_version_port import arm64
    native = derive(version, bindings)
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    decoder.detail = True
    witnesses = []

    def field(role, start, length, key, register, operation, kind, delta=0):
        member = bindings[key] + delta
        matches = [i for i in decoder.disasm(image.data[start:start+length], start)
                   if i.mnemonic.startswith(operation) and i.op_str.startswith(kind)
                   and any(o.type == ARM64_OP_MEM and i.reg_name(o.mem.base) == register
                           and o.mem.disp == member for o in i.operands)]
        if not matches:
            raise ValueError(f'Native {role} no longer uses {key}={member:#x}')
        pc = matches[0].address
        witnesses.append(dict(role=role, address=pc, words=[image.word(pc)]))

    clipper = bindings['kClipperUpdate']
    field('clipper flags', clipper, 0x300, 'clipper::kFlags', 'x19', 'strb', 'w')
    field('clipper rejection reasons', clipper, 0x300, 'clipper::kReasons', 'x19', 'ldrh', 'w')
    field('clipper pending vector', clipper, 0x200, 'clipper::kPending', 'x20', 'str', 'x')
    field('clipper pending Z', clipper, 0x200, 'clipper::kPending', 'x19', 'str', 'w', 8)
    state = native['state']
    field('main state', state, 0x40, 'main_stage::kState', 'x19', 'ldr', 'w')
    field('main preview timer', state, 0x80, 'main_stage::kPreviewTimer', 'x21', 'ldr', 's')
    field('main goal', state, 0x200, 'main_stage::kTarget', 'x19', 'ldp', 's')
    field('main slowdown', state, 0x300, 'main_stage::kSlowdown', 'x19', 'ldr', 's')
    field('live actor position', state, 0x200, 'actor::kPosition', 'x20', 'ldr', 's')
    cancel = bindings['kCancelUpdate']
    field('return state', cancel, 0x40, 'cancel_stage::kState', 'x19', 'ldr', 'w')
    field('return goal', cancel, 0x150, 'cancel_stage::kGoal', 'x19', 'ldp', 's')
    field('return speed', cancel, 0x500, 'cancel_stage::kSpeed', 'x19', 'ldr', 's')
    field('return slowdown', cancel, 0x500, 'cancel_stage::kSlowdown', 'x19', 'ldr', 's')
    field('return cloth velocity', cancel, 0x800, 'actor::kClothWind', 'x20', 'ldr', '')
    field('saved cloth velocity', cancel, 0x800, 'cancel_stage::kClothWind', 'x19', 'st', '')
    selector = bindings['kSelectorUpdatePosition']
    field('selector ceiling', selector, 0x40, 'selector::kCeilingHeight', 'x21', 'ldr', 's')
    field('selector cancel latch', selector, 0x700, 'selector::kCancelLatch', 'x19', 'strb', 'w')

    # After their independently scheduled prologues, the actual wrappers retain the same lock, argument, backend-slot and boolean-return instructions.
    reference = MainImage('1.2.1')
    def normalized(word, pc, start):
        if arm64.is_bl(word):
            return 'call'
        target = arm64.branch_target(word, pc)
        if target is not None:
            return (word & 0xfc000000, target-start)
        return word
    for key, length in (('kVoxelUp', 0x88), ('kVoxelVertical', 0xa0)):
        start, baseline = bindings[key], BINDINGS[key]['1.2.1']
        for offset in range(24, length, 4):
            if normalized(image.word(start+offset),start+offset,start) != normalized(
                    reference.word(baseline+offset),baseline+offset,baseline):
                raise ValueError(f'Native voxel wrapper ABI differs: {version} {key}+{offset:x}')
        # Runtime guards cover backend lookup and arguments, without storing the entire wrapper as redundant guard data.
        offset = 0x3c if key == 'kVoxelUp' else 0x48
        witnesses.append(dict(role=key+' backend arguments', address=start+offset,
                              words=[image.word(start+n) for n in range(offset,offset+24,4)]))
    return witnesses + voxel_outputs(image, bindings, version, decoder)


def voxel_outputs(image, bindings, version, decoder):
    import struct
    from native_profile_data import row
    table = row(0x431ec28,0x43ee7e8,0x43e8910,0x43dcd00,0x43ebd00,
                0x37258f0,0x37208f0,0x37228f0,0x37348f0)[version]
    up = row(0x1ae0bd4,0x1b3eda4,0x1b33274,0x1b240e0,0x1b2f5a0,
             0x21a5278,0x2195428,0x2195668,0x219ba78)[version]
    vertical = row(0x76900c,0xf1b424,0x7ad57c,0xf11b54,0xf0bb6c,
                   0x5c55fc,0x4c4250,0x1b50e4,0x3b3d90)[version]
    if struct.unpack_from('<QQ',image.data,table+0x80) != (vertical,up):
        raise ValueError('Native voxel backend vtable differs')
    result = []
    for role, start, size in (('up',up,0x2fc),('vertical',vertical,0x2c4)):
        body = list(decoder.disasm(image.data[start:start+size],start))
        if not any(i.mnemonic == 'mov' and i.op_str == 'x19, x1' for i in body):
            raise ValueError('Voxel output is no longer argument X1')
        for member in ([bindings['voxel_up::'+key] for key in
                        ('kUnit','kCellX','kFirstSolid','kSolidEnd','kNextSolid')]
                       if role == 'up' else [4,8,12]):
            stores = [i for i in body if i.mnemonic.startswith('st') and
                      any(o.type == ARM64_OP_MEM and i.reg_name(o.mem.base) == 'x19'
                          and (o.mem.disp == member or
                               i.mnemonic == 'stp' and i.op_str.startswith('s') and o.mem.disp+4 == member)
                          for o in i.operands)]
            if len(stores) != 1:
                raise ValueError(f'Native voxel {role} output {member:x} differs')
            pc = stores[0].address
            result.append(dict(role=f'voxel {role} output {member:x}',address=pc,words=[image.word(pc)]))
        if not any(i.mnemonic == 'fmov' and '#0.50000000' in i.op_str for i in body):
            raise ValueError('Native voxel half-cell center differs')
    return result
