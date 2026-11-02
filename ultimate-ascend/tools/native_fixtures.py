"""Native services used by the ARM tests, derived from their actual callers.

These are test fixtures, not replacements for native behavior. Tests execute the
state and animation routines themselves and mock their external services only.
"""
from __future__ import annotations

from native_profile_data import row

MAIN_RESULT = row(0x1c015f8, 0x1c64288, 0x1c58a98, 0x1c4a708, 0x1c56058,
                  0x22e4048, 0x22d4e9c, 0x22d48c8, 0x22dda2c)
# Actual native render branches consuming the same in-ceiling bit. Compiler scheduling and the owning register differ; do not reuse 1.2.1's branch offsets.
RENDER_COVER = row(0xbe1040, 0x95d8b8, 0x92f3bc, 0xb6142c, 0x946450,
                   0x5402e8, 0x39cf30, 0x2d8aa8, 0x7a96d0)


def derive(version, bindings):
    from totk_version_port.images import MainImage
    from totk_version_port import arm64, scan
    image = MainImage(version)

    def calls(start, size):
        return [(pc, arm64.branch_target(image.word(pc), pc)) for pc in range(start, start + size, 4)
                if arm64.is_bl(image.word(pc))]

    # The state update is the call immediately before the known animation call.
    update_calls = calls(bindings['kMainUpdate'], 0x100)
    anim_index = next(i for i, (_, target) in enumerate(update_calls) if target == bindings['kMainUpdateAnim'])
    state_pc, state = update_calls[anim_index - 1]
    assert image.word(state_pc - 4) == 0xaa1303e0  # MOV X0,X19
    assert image.word(state_pc - 8) & 0xff00001f == 0x54000000  # B.EQ
    state_calls = calls(state, 0x100)
    assert len(state_calls) >= 7
    actor, player, repeated_player, event, timer, game_data, confirm = [at for _, at in state_calls[:7]]
    assert player == repeated_player
    event_pc = state_calls[3][0]
    assert image.word(event_pc + 4) == 0x39410008  # event+40: animation ready
    pause = scan.globals_in(image, event_pc + 12, 4)
    assert len(pause) == 1
    pause_pc, variable, kind, slot = pause[0]
    assert image.word(pause_pc + 4) == 0xf9400108
    pause_member = arm64.load_store_unsigned(image.word(pause_pc + 8))
    assert pause_member[:2] == (8, 8)
    assert image.word(pause_pc + 12) & 0xff00001f == 0x37000008

    animation_calls = calls(bindings['kMainUpdateAnim'], 0x100)
    assert [a for _, a in animation_calls[:3]] == [player, bindings['kCheckCommand'], actor]
    command_pc = animation_calls[1][0] - 12
    command = scan.globals_in(image, command_pc, 2)
    assert len(command) == 1 and command[0][2] == 'direct'
    return_anim_pc = bindings['kCancelUpdate'] + 0x30
    return_anim = arm64.branch_target(image.word(return_anim_pc), return_anim_pc)
    assert arm64.is_bl(image.word(return_anim_pc))
    assert image.word(return_anim_pc - 4) == 0xaa1303e0
    assert image.word(return_anim + 12) == 0x39406008  # node animation flag

    result = MAIN_RESULT[version]
    result_getter = arm64.branch_target(image.word(result + 16), result + 16)
    assert image.word(result + 20) == 0xb9402268
    assert image.word(result + 36) == 0x396b9809  # player's cancellation byte
    cover = RENDER_COVER[version]
    cover_word, branch = image.word(cover), image.word(cover + 4)
    assert cover_word & 0xfffffc00 == 0x39729400
    assert branch & 0xfff8001f == 0x37380000 | (cover_word & 31)
    return dict(state=state, result=result, result_getter=result_getter, actor=actor, player=player,
                event=event, timer=timer, game_data=game_data, confirm=confirm,
                pause_address=slot if kind == 'got' else variable, pause_indirect=kind == 'got',
                pause_member=pause_member[2], preview_command=command[0][1],
                preview_actor_check=animation_calls[3][1], return_animation=return_anim,
                return_animation_words=[image.word(return_anim+i) for i in range(0,20,4)],
                cover=cover, cover_owner=(cover_word >> 5) & 31,
                cover_on=cover+4+4*arm64.sign_extend((branch >> 5)&0x3fff,14), cover_off=cover+8)
