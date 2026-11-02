"""Native instruction witnesses for version-dependent member layouts.

Entry-word guards alone cannot detect a moved member. Derive each witnessed
field from the native operation that uses it, then compare with the profile.
"""
from __future__ import annotations


def cancel_target(image, bindings: dict) -> dict:
    command = bindings["kCancelCommand"]
    candidates = []
    for pc in range(command - 0x250, command, 4):
        word = image.word(pc)
        # EnterCancel: read return X from Player, read its event flags, then set the collision-off flag. The following FCMP tests return X for NaN.
        if word & 0xffc003ff != 0xbd4002a0:  # LDR S0,[X21,#member]
            continue
        if image.word(pc + 4) & 0xffc003ff != 0xb94002a8:  # LDR W8,[X21,#flags]
            continue
        if image.word(pc + 8) != 0x32140108 or image.word(pc + 12) != 0x1e202000:
            continue
        candidates.append((pc, ((word >> 10) & 0xfff) * 4,
                           ((image.word(pc + 4) >> 10) & 0xfff) * 4))
    if len(candidates) != 1:
        raise ValueError(f"Native return-target witness is not unique: {candidates}")
    pc, target, flags = candidates[0]
    if target != bindings["player::kReturnTarget"] or flags != bindings["player::kEventFlags"]:
        raise ValueError(f"Native cancel layout differs: return={target:#x}, flags={flags:#x}")
    return {"role": "cancel return target and collision flag", "address": pc,
            "return_target": target, "event_flags": flags,
            "words": [image.word(pc + n) for n in range(0, 16, 4)]}


def verify(image, bindings: dict) -> list[dict]:
    from totk_version_port.images import BUILD_IDS
    from native_core_contracts import verify as verify_core
    version = next(v for v,bid in BUILD_IDS.items() if bid == image.build_id)
    return [cancel_target(image, bindings), *prompt_frames(image, bindings),
            *effect_choices(image, bindings), *physics_result(image, bindings),
            *effect_cover(image, bindings), *camera_layout(image, bindings),
            *node_player(image, bindings, version), *world_access(image, bindings, version),
            *verify_core(image, bindings, version)]


def node_player(image, bindings, version):
    from community_profile_data import SITES
    at = SITES[0x107e9a0][version]
    def load(target, base, member):
        return 0xf9400000 | (member // 8 << 10) | (base << 5) | target
    expected = [load(8,0,bindings['node::kContext']),load(8,8,bindings['node::kContextOwner']),
                load(8,8,bindings['node::kOwnerActor']),load(8,8,bindings['actor::kComponents']),
                0xb4000068,load(0,8,bindings['components::kPlayer']),0xd65f03c0,0xaa1f03e0,0xd65f03c0]
    if [image.word(at+i*4) for i in range(9)] != expected:
        raise ValueError('Native node/actor/player chain differs')
    return [dict(role='node actor and player component chain',address=at,words=expected)]


def world_access(image, bindings, version):
    from native_profile_data import row
    from totk_version_port import arm64
    at = row(0x136a620,0x13a845c,0x139738c,0x1389354,0x13912dc,
             0x8e4ec8,0x930c28,0x922de4,0x98152c)[version]
    page = arm64.adrp_page(image.word(at),at)
    direct = bindings['kWorldDirect']
    access = arm64.add_immediate(image.word(at+4)) if direct else arm64.load_store_unsigned(image.word(at+8))
    if page is None or access is None or access[:2] != (8,8) or page+access[2] != bindings['kWorldSlot']:
        raise ValueError('Native world module access differs')
    if direct:
        expected = {8:0xf9400108,12:0xf9406900,20:0xf9400008,
                    28:0xf9400108 | (bindings['kWorldPositionMethod']//8 << 10),32:0xd63f0100}
        length = 36
    else:
        expected = {4:0x9101f001,12:0xf9400108,16:0xf9406908,20:0xf9400109,24:0xaa0803e0,
                    28:0xf9400122 | (bindings['kWorldPositionMethod']//8 << 10),32:0xd61f0040}
        length = 36
    if any(image.word(at+n) != word for n,word in expected.items()):
        raise ValueError('Native world position method/manager chain differs')
    return [dict(role='native position-dependent Depths rate',address=at,
                 words=[image.word(at+n) for n in range(0,length,4)])]


def camera_layout(image, bindings):
    """Pin the scalar types as well as offsets used by camera overrides/fading."""
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    from capstone.arm64 import ARM64_OP_MEM
    from totk_version_port import arm64
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    decoder.detail = True
    start = bindings['kCameraUpdate']
    body = list(decoder.disasm(image.data[start:start+0x500],start))
    result = []
    for key, kind in (('kPosition','d'),('kLookAt','d'),('kPositionOverride','d'),
                      ('kLookAtOverride','d'),('kUp','s')):
        member = bindings['game_camera::'+key]
        matches = [i for i in body if i.mnemonic.startswith('ld') and i.op_str.startswith(kind)
                   and any(o.type == ARM64_OP_MEM and i.reg_name(o.mem.base) == 'x19'
                           and o.mem.disp == member for o in i.operands)]
        if len(matches) != 1:
            raise ValueError(f'Native camera member differs: {key} at {member:x}')
        pc = matches[0].address
        result.append(dict(role='camera '+key,address=pc,words=[image.word(pc)]))
    for function, key, base, destination in (
            ('kCameraAlpha','game_camera::kFadePosition',0,1),
            ('kCameraAlpha','game_camera::kFadeLookAt',0,2),
            ('kPlayerFade','player_camera::kFadePosition',19,None)):
        start = bindings[function]
        matches = [pc for pc in range(start,start+0x100,4)
                   if (add := arm64.add_immediate(image.word(pc))) is not None
                   and add[0] == base and add[2] == bindings[key]
                   and (destination is None or add[1] == destination)]
        if len(matches) != 1:
            raise ValueError(f'Native fade camera member differs: {key}')
        pc = matches[0]
        result.append(dict(role=key,address=pc,words=[image.word(pc)]))
    # The three adjacent camera vectors are a native float-camera block.
    if bindings['player_camera::kFadeLookAt'] != bindings['player_camera::kFadePosition'] + 12:
        raise ValueError('Player fade camera vectors are not adjacent')
    return result


def physics_result(image, bindings):
    """Follow the query's real post-cast vtable method to its SDK backpointer."""
    import struct
    pointer = lambda pc: struct.unpack_from('<Q', image.data, pc)[0]
    table = bindings['kRayQueryVtableSlot']
    if not bindings['kDirectRayVtables']:
        table = pointer(table)
    post = pointer(table + 0x80)
    reads = [pc for pc in range(post, post + 0x120, 4)
             if image.word(pc) & 0xffffffe0 == 0xf9405c00]
    if len(reads) != 1:
        raise ValueError('Native query SDK backpointer is not unique')
    pc = reads[0]
    if [image.word(pc + n) for n in (-12,-8,-4)] != [0xf9400008,0xf9408d08,0xd63f0100]:
        raise ValueError('Native query no longer obtains the record through world virtual118')
    register = image.word(pc) & 31
    stores = [at for at in range(pc + 4, pc + 24, 4)
              if image.word(at) == 0xf900ba60 | register]
    if len(stores) != 1:
        raise ValueError('Native query no longer publishes the body at170')
    expected_world = 0xf9400000 | ((bindings['kPhysicsWorldFromWrapper'] // 8) << 10)
    world_reads = [at for at in range(post, pc, 4) if image.word(at) == expected_world]
    if len(world_reads) != 1:
        raise ValueError('Native physics world wrapper differs')
    return [dict(role='query SDK record and body pointer',address=pc-12,
                 words=[image.word(at) for at in range(pc-12,stores[0]+4,4)]),
            dict(role='physics world wrapper',address=world_reads[0],words=[expected_world])]


def effect_cover(image, bindings):
    """Check the exact pointer chain and flag the native effect update consumes."""
    from totk_version_port import arm64
    start = bindings['kEffectCalc']
    expected = 0x39400129 | ((bindings['ceiling_effect::kFlags'] + 1) << 10)
    reads = [pc for pc in range(start,start+0x200,4) if image.word(pc) == expected]
    if len(reads) != 1:
        raise ValueError('Native in-ceiling flag read differs')
    pc = reads[0]
    if image.word(pc+4) & 0xfff8001f != 0x37380009:
        raise ValueError('Native in-ceiling bit differs')
    if image.word(pc-8) != 0xf9400129 | ((bindings['ceiling_effect::kOwner'] // 8) << 10):
        raise ValueError('Native in-ceiling owner differs')
    dereference = arm64.load_store_unsigned(image.word(pc-16))
    if dereference is None or dereference[1:] != (9,0):
        raise ValueError('Native in-ceiling variable dereference differs')
    load = arm64.load_store_unsigned(image.word(pc-20))
    if load is None or load[:2] != (dereference[0],dereference[0]):
        raise ValueError('Native in-ceiling GOT register differs')
    page_sites = [at for at in range(start,pc-20,4)
                  if image.word(at)&31 == load[0] and arm64.adrp_page(image.word(at),at) is not None]
    page_pc = page_sites[-1]
    page = arm64.adrp_page(image.word(page_pc),page_pc)
    if page + load[2] != bindings['ceiling_effect::kGlobalSlot']:
        raise ValueError('Native in-ceiling global differs')
    return [dict(role='native in-ceiling global page',address=page_pc,words=[image.word(page_pc)]),
            dict(role='native in-ceiling owner and cover bit',address=pc-20,
                 words=[image.word(at) for at in range(pc-20,pc+8,4)])]


def effect_choices(image, bindings: dict) -> list[dict]:
    """Verify the live direction register against the native screen-effect branch."""
    from totk_version_port.arm64 import branch_target, sign_extend
    result = []
    for role in ("Wipe", "Loop"):
        site = bindings[f"kEffect{role}Choice"]
        register = bindings[f"kEffect{role}Register"]
        branches = [pc for pc in range(site, site + 24, 4)
                    if image.word(pc) & 0xff00001f == 0x34000000 | register]
        if len(branches) != 1:
            raise ValueError(f"Native {role} direction branch does not match register {register}")
        pc = branches[0]
        words = [image.word(at) for at in range(site, pc + 4, 4)]
        result.append({"role": f"{role} effect direction", "address": site, "words": words,
                       "register": register, "branch": pc, "nonzero": pc + 4,
                       "zero": pc + 4 * sign_extend((image.word(pc) >> 5) & 0x7ffff, 19),
                       "calls": [branch_target(image.word(at), at) for at in range(site, pc, 4)
                                 if image.word(at) & 0xfc000000 == 0x94000000]})
    return result


def prompt_frames(image, bindings: dict) -> list[dict]:
    """Derive both message pointers from native SP/FP setup and ID consumers."""
    from totk_version_port import arm64
    witnesses = []
    for role, key in (("Setup", "kPromptSetup"), ("Refresh", "kPromptRefresh")):
        site = bindings[key]
        frame_sites = [pc for pc in range(site - 0x800, site, 4)
                       if image.word(pc) & 0xffc003ff == 0x910003fd]
        if not frame_sites:
            raise ValueError(f"No native {role} frame setup")
        frame_pc = frame_sites[-1]
        _, _, frame = arm64.add_immediate(image.word(frame_pc))
        if not any(image.word(pc) & 0xffc07fff == 0xa9007bfd
                   and ((image.word(pc) >> 15) & 0x7f) * 8 == frame
                   for pc in range(frame_pc - 24, frame_pc, 4)):
            raise ValueError(f"Native {role} frame does not match its saved FP/LR")
        message = arm64.add_immediate(image.word(site - 20))
        label_word = image.word(site - 16)
        if message is None or message[:2] != (31, 0):
            raise ValueError(f"Native {role} message is not formed from SP")
        if label_word & 0xbfc003ff != 0x910003a2:
            raise ValueError(f"Native {role} label is not formed from FP")
        label = ((label_word >> 10) & 0xfff) * (-1 if label_word & 0x40000000 else 1)
        if (message[2] - frame != bindings[f"prompt::k{role}Message"]
                or label != bindings[f"prompt::k{role}Label"]):
            raise ValueError(f"Native {role} prompt frame differs from profile")
        id_pc = site + 4 if role == "Setup" else site
        if arm64.load_store_unsigned(image.word(id_pc)) != (31, 8, message[2] + bindings["prompt::kId"]):
            raise ValueError(f"Native {role} message ID member differs")
        witnesses.extend([
            {"role": f"{role} prompt frame", "address": frame_pc, "words": [image.word(frame_pc)]},
            {"role": f"{role} prompt message and label", "address": site - 20,
             "words": [image.word(site - 20), image.word(site - 16)]},
            {"role": f"{role} prompt ID", "address": id_pc, "words": [image.word(id_pc)]},
        ])
    return witnesses
