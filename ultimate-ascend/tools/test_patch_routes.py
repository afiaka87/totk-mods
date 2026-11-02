"""Run new route adapters/state gates as placed ARM; mock only their native services."""
import argparse
import json
import math
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import arm64_const as arm
from test_patch_runtime import Machine, BASE, HEAP, GPRS


class RouteMachine(Machine):
    handlers = None

    def instruction(self, cpu, address, size, context):
        if self.handlers and address in self.handlers:
            result = self.handlers[address]()
            if result is not None:
                cpu.reg_write(GPRS[0], result)
            cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
            return
        for name, operation, count in (('sqrtf', math.sqrt, 1), ('sinf', math.sin, 1),
                                       ('cosf', math.cos, 1), ('atan2f', math.atan2, 2)):
            if address == BASE + self.report['native_services'].get(name, -1):
                values = [struct.unpack('<f', struct.pack('<I', cpu.reg_read(getattr(arm, f'UC_ARM64_REG_S{i}'))))[0]
                          for i in range(count)]
                cpu.reg_write(arm.UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', operation(*values)))[0])
                cpu.reg_write(arm.UC_ARM64_REG_PC, cpu.reg_read(GPRS[30]))
                return
        super().instruction(cpu, address, size, context)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'tool-root', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    m = RouteMachine(a.layout, a.tool_root)
    types, globals = {}, {}
    with (a.layout / 'native/descend-patch.o').open('rb') as stream:
        elf = ELFFile(stream)
        bases, cursor = {}, 0
        for i, section in enumerate(elf.iter_sections()):
            if not section['sh_size'] or not section['sh_flags'] & 2 or section['sh_flags'] & 4 or section.name in (
                    '.data.patch_callback_roots', '.data.patch_guard_root'):
                continue
            alignment = max(1, section['sh_addralign'])
            cursor = (cursor + alignment - 1) & -alignment
            bases[i] = cursor
            cursor += section['sh_size']
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            if symbol['st_shndx'] in bases:
                globals[symbol.name] = HEAP + bases[symbol['st_shndx']] + symbol['st_value']
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag == 'DW_TAG_structure_type' and name and 'DW_AT_byte_size' in die.attributes:
                    types[name.value.decode()] = {
                        child.attributes['DW_AT_name'].value.decode(): child.attributes['DW_AT_data_member_location'].value
                        for child in die.iter_children() if child.tag == 'DW_TAG_member' and
                        'DW_AT_data_member_location' in child.attributes}

    def fn(name):
        found = [address for symbol, address in m.report['runtime_symbols'].items() if name in symbol]
        assert len(found) == 1, (name, found)
        return found[0]

    def put(address, fmt, *values):
        m.cpu.mem_write(address, struct.pack('<' + fmt, *values))

    def get(address, fmt):
        return struct.unpack('<' + fmt, m.cpu.mem_read(address, struct.calcsize('<' + fmt)))

    def x(i):
        return m.cpu.reg_read(GPRS[i])

    def invoke(address, *args):
        for i, value in enumerate(args):
            m.cpu.reg_write(GPRS[i], value)
        m.run(address)
        return x(0)

    def floats(values):
        for i, value in enumerate(values):
            m.cpu.reg_write(getattr(arm, f'UC_ARM64_REG_S{i}'), struct.unpack('<I', struct.pack('<f', value))[0])

    def reset(name):
        m.reset()
        m.set_ready()
        m.case = name
        m.handlers = {}
        m.fixture_writes = [(HEAP + 0x2000, HEAP + 0x10000)]
        m.cpu.mem_write(HEAP + 0x2000, bytes(0xe000))

    results = []
    if m.report.get('shrine_flag_offset') is not None:
        restricted = bool(m.report.get('shrine_gates'))
        clipper = HEAP + 0x3000
        mode = next(v for k, v in globals.items() if k.endswith('g_descendModeE'))
        prompt = next(v for k, v in globals.items() if k.endswith('g_promptDescendE'))
        # A live Descend selection must be retired before native shrine targeting.
        for shrine in (False, True):
            reset(f'shrine-native-targeting-{shrine}')
            m.fake_calls[BASE + m.bindings["kIsShrine"]] = int(shrine)
            put(mode, 'B', 1)
            put(prompt, 'I', 2)
            put(clipper + 24, 'I', 0x12345678)
            value = invoke(fn('beforeClipperUpdate'), clipper)
            armed, saved = struct.unpack('<B3xI', struct.pack('<Q', value))
            blocked = shrine and restricted
            assert bool(armed) == (not blocked), m.case
            assert get(HEAP + m.report['shrine_flag_offset'], 'B') == (int(blocked),)
            if blocked:
                assert get(clipper + 24, 'I') == (0x12345678,)
                assert get(mode, 'B') == (0,) and get(prompt, 'I') == (0,)
            else:
                assert saved == 0x12345678 and get(clipper + 24, 'I') != (0x12345678,)
            results.append(m.case)
    slot, module = HEAP + 0x2000, HEAP + 0x2100
    for ceiling in (False, True):
        for found in (False, True):
            reset(f'voxel-down-{ceiling}-{found}')
            put(BASE + m.bindings["kVolumeStatsSlot"], 'Q', slot)
            put(slot, 'Q', module)

            def query():
                assert [x(i) for i in (0, 3, 4)] == [module, 0, int(ceiling)]
                assert get(x(2), '3f') == (10, 120, -15)
                if found:
                    put(x(1) + 8, 'f', -400.5)
                return int(found)

            m.handlers[BASE + m.bindings["kVoxelVertical"]] = query
            floats((10, 120, -15))
            value = invoke(fn('voxelDown'), int(ceiling))
            status, height = struct.unpack('<B3xf', struct.pack('<Q', value))
            assert status == found and (height == -400.5 if found else math.isnan(height))
            results.append(m.case)

    world, body, sdk, native = [HEAP + v for v in (0x2600, 0x2800, 0x2b00, 0x2d00)]
    key, output = HEAP + 0x3000, HEAP + 0x3100
    matrix = (1, 0, 0, 4, 0, 1, 0, 10, 0, 0, 1, -3)
    identity = 0x123400000042

    def physics():
        put(BASE + m.bindings["kPhysicsSlot"], 'Q', slot)
        put(slot, 'Q', HEAP + 0x2200)
        put(HEAP + 0x2200 + 0xc8, 'Q', HEAP + 0x2300)
        put(HEAP + 0x2300 + 0xc0, 'Q', HEAP + 0x2400)
        put(HEAP + 0x2400 + m.bindings['kPhysicsWorldFromWrapper'], 'Q', world)
        put(key, '3Q', world, body, identity)
        put(native + 0xb8, 'Q', body)
        put(body + 0x70, 'Q', sdk)
        put(sdk + 8, 'Q', identity)
        put(body + 0xf8, '3f', 4, 5, 6)

    for case in ('live', 'world-changed', 'generation-expired', 'body-recycled', 'sdk-recycled', 'static', 'bad-matrix'):
        reset('body-' + case)
        physics()
        calls = []

        def valid():
            calls.append('valid')
            assert (x(0), x(1)) == (world, identity)
            return int(case != 'generation-expired')

        def get_body():
            calls.append('get')
            assert (x(0), x(1)) == (world, identity)
            return native

        def get_matrix():
            calls.append('matrix')
            assert x(0) == body
            values = list(matrix)
            if case == 'bad-matrix': values[0] = float('nan')
            put(x(1), '12f', *values)

        m.handlers = {BASE + m.bindings["kBodyValid"]: valid, BASE + m.bindings["kGetBody"]: get_body, BASE + m.bindings["kBodyMatrix"]: get_matrix}
        if m.bindings['kBodyMotion']:
            m.fake_calls[BASE + m.bindings["kBodyMotion"]] = int(case != 'static') * 2
        else:
            put(body + 0x60, 'Q', 0)
            put(body + 0x68, 'I', 0x40 if case == 'static' else 0)
            put(body + 0x80, 'I', 2)
        if case == 'world-changed': put(key, 'Q', world + 8)
        if case == 'body-recycled': put(native + 0xb8, 'Q', body + 8)
        if case == 'sdk-recycled': put(sdk + 8, 'Q', identity + 1)
        assert bool(invoke(fn('snapshotBody'), key, output)) == (case == 'live'), m.case
        if case == 'live': assert get(output, '15f') == matrix + (4, 5, 6)
        if case == 'world-changed': assert not calls
        if case in ('generation-expired', 'body-recycled', 'sdk-recycled', 'static'): assert 'matrix' not in calls
        results.append(m.case)

    # Run the real ray adapter, including aggregate output and native x8 group return.
    reset('body-ray-abi')
    physics()
    actor = HEAP + 0x4000

    def group():
        assert x(0) == actor
        put(x(8), 'Q', 0x1234)

    def ray():
        assert get(x(0), '3f') == (1, 2, 3) and get(x(1), '3f') == (4, 5, 6)
        assert get(x(3), 'Q') == (0x1234,)
        assert (x(4), x(5)) == (0x20, 0)
        query = x(2)
        assert get(query + 0x84, 'I') == (0,)
        put(query + 0x24, '3f', 4, 5, 6)
        put(query + 0x30, '3f', 0, -1, 0)
        put(query + 0x120, 'Q', identity)
        put(query + 0x170, 'Q', body)
        return 1

    m.handlers = {BASE + m.bindings["kSystemGroupId"]: group, BASE + m.bindings["kRaycast"]: ray}
    m.cpu.reg_write(GPRS[8], output)
    floats((1, 2, 3, 4, 5, 6))
    invoke(fn('6engine7raycast'), actor, 0, 1, key)
    assert get(output, 'B3x6f') == (1, 4, 5, 6, 0, -1, 0)
    assert get(key, '3Q') == (world, body, identity)
    results.append(m.case)

    node, context, owner, holder, player = [HEAP + v for v in (0x5000, 0x5200, 0x5400, 0x6000, 0x7000)]
    event = next(v for k, v in globals.items() if k.endswith('g_eventE'))

    def ev(name, fmt, *values):
        put(event + types['Event'][name], fmt, *values)

    def setup_event(route, state):
        for address, value in ((node + 8, context), (context + 8, owner), (owner + 504, actor),
                               (actor + m.bindings["actor::kComponents"], holder), (holder + 936, player)):
            put(address, 'Q', value)
        put(node + 24, 'B', 2)
        put(node + 32, 'I', state)
        put(node + 60, '3f', 0, -100, 0)
        put(player + 2788, 'I', 0x1000)
        for name, fmt, values in (('active', 'B', (1,)), ('serial', 'I', (7,)), ('actor', 'Q', (actor,)),
                                  ('player', 'Q', (player,)), ('route', 'B', (route,)),
                                  ('target', '3f', (0, -100, 0)), ('committed', '3f', (0, -100, 0)),
                                  ('start', '3f', (0, 10, 0)), ('touched', 'Q', (19200000,)),
                                  ('body', '3Q', (world, body, identity)), ('localOrigin', '3f', (0, 0, 0))):
            ev(name, fmt, *values)

    for case in ('waiting', 'deep-valid', 'deep-rejected', 'collision-pending', 'collision-timeout',
                 'object-moved', 'object-missing', 'object-preview-rejected'):
        reset('travel-' + case)
        object_route = case.startswith('object')
        setup_event(2 if object_route else 1, 3 if object_route else 1)
        if case == 'object-preview-rejected':
            put(node + 32, 'I', 5)
            ev('previewReady', 'B', 1)
        order = []
        if case == 'collision-timeout':
            ev('exitWaitTick', 'Q', 1)
            m.fake_calls[BASE + m.report['native_services']['svcGetSystemTick']] = 3 * 19200000

        def ready():
            order.append('ready')
            assert x(0) == 0 and get(x(1), '3f') == (0, -100, 0)
            return int(case != 'waiting')

        def plan():
            order.append('validate')
            out = x(8)
            put(out + types['Plan']['route'], 'B', 2 if object_route else 1)
            verdict = 10 if case.startswith('collision') else int(case in ('deep-rejected', 'object-missing', 'object-preview-rejected'))
            put(out + types['Plan']['verdict'], 'B', verdict)
            put(out + types['Plan']['target'], '3f', 2, -99, 3)
            put(out + types['Plan']['underside'], 'f', -99.5)
            put(out + types['Plan']['floor'], 'f', 10)
            put(out + types['Plan']['drop'], '3f', 2, -102.3, 3)
            put(out + types['Plan']['landing'], 'f', -200)

        def snapshot():
            put(x(1), '12f', *matrix)
            return int(case != 'object-missing')

        def support():
            order.append('support')
            return 1

        def stop():
            order.append('stop')
            assert x(0) == player and get(x(1), '3f') == (0, 0, 0)
            assert m.cpu.reg_read(arm.UC_ARM64_REG_S0) == 0

        def original():
            order.append('motion')
            assert x(0) == node and x(1) == context
            return 123

        m.handlers = {BASE + m.bindings["kPreloadReady"]: ready, BASE + fn('validateDeepExit'): plan,
                      BASE + fn('movingPlan'): plan, BASE + fn('snapshotBody'): snapshot,
                      BASE + fn('moveEventSupport'): support, BASE + m.bindings["kSetForwardSpeed"]: stop,
                      BASE + m.report['adapters']['original_TravelStateHook']: original}
        result = invoke(m.report['adapters']['entry_TravelStateHook'], node, context)
        if case == 'waiting': assert order == ['ready', 'motion'] and result == 123
        elif case == 'collision-pending':
            assert order[-1] == 'stop' and 'motion' not in order and result == 0
            assert not get(player + 2788, 'I')[0] & 0x80000
            assert get(event + types['Event']['exitWaitTick'], 'Q')[0] != 0
            assert get(event + types['Event']['touched'], 'Q')[0] == 19200000
        elif case in ('deep-rejected', 'collision-timeout', 'object-missing', 'object-preview-rejected'):
            assert order[-1] == 'stop' and 'motion' not in order and result == 0
            assert get(player + 2788, 'I')[0] & 0x80000
            assert not get(event + types['Event']['previewReady'], 'B')[0]
            m.fake_calls[BASE + m.native['result_getter']] = player
            assert invoke(m.native['result'],node) == 2, 'Native result must enter cancellation even from preview'
        else:
            assert order[-3:] == ['validate', 'support', 'motion'] and result == 123
            assert get(node + 60, '3f') == (2, -99, 3)
            assert get(event + types['Event']['committed'], '3f') == (0, -100, 0)
        results.append(m.case)

    # A static exit is checked after loading and once near arrival, never once per preview frame.
    for failure in (False, True):
        reset('deep-final-check-' + ('reject' if failure else 'bounded'))
        setup_event(1, 3)
        ev('floor', 'f', 10)
        ev('underside', 'f', -100.5)
        ev('drop', '3f', 0, -103.3, 0)
        calls = []

        def validate_final():
            calls.append('validate')
            out = x(8)
            put(out + types['Plan']['route'], 'B', 1)
            put(out + types['Plan']['verdict'], 'B', int(failure and len(calls) == 2))
            for field, values in (('target',(0,-100,0)), ('drop',(0,-103.3,0))):
                put(out + types['Plan'][field], '3f', *values)
            for field,value in (('floor',10),('underside',-100.5),('landing',-200)):
                put(out + types['Plan'][field], 'f', value)

        m.handlers = {BASE + fn('validateDeepExit'): validate_final,
                      BASE + fn('moveEventSupport'): lambda: 1, BASE + m.bindings["kSetForwardSpeed"]: lambda: None,
                      BASE + m.bindings["kSystemGroupId"]: lambda: put(x(8), 'Q', 0x1234), BASE + m.bindings["kRaycast"]: lambda: 0,
                      BASE + m.report['adapters']['original_TravelStateHook']: lambda: 123}
        # Start far away; many travel frames cannot trigger a second survey.
        put(actor + m.bindings["actor::kPosition"], '3f', 0, 10, 0)
        for _ in range(20): invoke(m.report['adapters']['entry_TravelStateHook'], node, context)
        assert len(calls) == 1
        put(actor + m.bindings["actor::kPosition"], '3f', 0, -98, 0)
        for _ in range(20): invoke(m.report['adapters']['entry_TravelStateHook'], node, context)
        assert len(calls) == 2
        if failure:
            assert get(player + 2788, 'I')[0] & 0x80000
            assert get(event + types['Event']['routeFailed'], 'B')[0]
        else:
            assert get(event + types['Event']['exitRechecked'], 'B')[0]
            put(node + 32, 'I', 5)
            for _ in range(20): invoke(m.report['adapters']['entry_TravelStateHook'], node, context)
            assert len(calls) == 2
        results.append(m.case)

    for near in (False, True):
        reset('pending-final-survey-' + ('hold' if near else 'moving'))
        setup_event(1,4)
        ev('exitValidated','B',1)
        ev('underside','f',-100.5)
        put(node+88,'f',20)
        put(actor + m.bindings["actor::kPosition"],'3f',0,-99.5 if near else -90,0)
        calls=[]
        def pending_plan():
            assert x(2) == 7, 'Final surveys must carry the event identity'
            put(x(8)+types['Plan']['verdict'],'B',11)
            calls.append('pending')
        m.handlers={BASE+fn('validateDeepExit'):pending_plan,
                    BASE+m.bindings["kSetForwardSpeed"]:lambda:calls.append('stop'),
                    BASE+m.report['adapters']['original_TravelStateHook']:lambda:calls.append('motion')}
        invoke(m.report['adapters']['entry_TravelStateHook'],node,context)
        assert calls == ['pending','stop' if near else 'motion']
        assert not get(player+2788,'I')[0] & 0x80000, 'Pending work is not a refusal'
        assert not get(event+types['Event']['exitRechecked'],'B')[0]
        put(player+2788,'I',0x81000)
        m.fake_calls[BASE + m.native['result_getter']] = player
        assert invoke(m.native['result'],node) == 2, 'B remains available while waiting for the final survey'
        results.append(m.case)

    reset('compiled-final-survey-budget-and-reset')
    candidate, output = HEAP+0xa000, HEAP+0xb000
    put(candidate+types['Plan']['route'],'B',1)
    put(candidate+types['Plan']['floor'],'f',100)
    put(candidate+types['Plan']['underside'],'f',-400)
    put(candidate+types['Plan']['drop'],'3f',0,-402.8,0)
    cast_count, up_count = 0, 0
    queries=[]
    def survey_ray():
        nonlocal cast_count
        cast_count += 1
        coords=[struct.unpack('<f',struct.pack('<I',m.cpu.reg_read(getattr(arm,f'UC_ARM64_REG_S{i}'))))[0]
                for i in range(6)]
        a,b=coords[:3],coords[3:]
        queries.append((a,b,x(1),x(2)))
        hits=[]
        if b[1] != a[1]:
            for height,upward in ((100,True),(-400,False),(-450,True)):
                t=(height-a[1])/(b[1]-a[1])
                front=upward == (b[1]<a[1])
                if 0<=t<=1 and (x(1)==0 or front==(x(1)==1)): hits.append(t)
        if hits:
            t=min(hits); position=[u+(v-u)*t for u,v in zip(a,b)]
            put(x(8),'B3x6f',1,*position,0,1,0)
        else: put(x(8),'B3x6f',0,0,0,0,0,0,0)
    def survey_up():
        nonlocal up_count
        up_count += 1
        packed=struct.pack('<BB2x3f',3,1,-400,100,float('nan'))
        for register,value in zip(GPRS[:2],struct.unpack('<2Q',packed)):
            m.cpu.reg_write(register,value)
    m.handlers={BASE+fn('6engine7raycast'):survey_ray, BASE+fn('6engine7voxelUp'):survey_up}
    verdict=11; frames=0
    while verdict == 11:
        before=cast_count
        m.cpu.reg_write(GPRS[8],output)
        invoke(fn('validateDeepExit'),actor,candidate,7)
        verdict=get(output+types['Plan']['verdict'],'B')[0]
        assert cast_count-before <= 16
        frames+=1
        assert frames < 12
    assert verdict == 0 and frames > 1 and cast_count > 32 and up_count == 2, (verdict,frames,cast_count,up_count,queries[:5])
    before=cast_count
    m.cpu.reg_write(GPRS[8],output)
    invoke(fn('validateDeepExit'),actor,candidate,8)
    assert cast_count>before and get(output+types['Plan']['verdict'],'B')[0] == 11
    first_query=queries[0]
    queries.clear()
    m.fake_calls[BASE+m.report['native_services']['svcGetSystemTick']] = 3*19200000
    m.cpu.reg_write(GPRS[8],output)
    invoke(fn('validateDeepExit'),actor,candidate,8)
    assert queries[0] == first_query, 'A loading pause must discard older partial collision samples'
    results.append(m.case)

    for case in ('moving', 'blocked', 'body-gone', 'arrived', 'collision-restored', 'support-missing'):
        reset('return-' + case)
        setup_event(2, 3)
        put(node + 60, '3f', 0, 11.05, 0)
        if case == 'arrived': put(node + 32, 'I', 6)
        if case == 'collision-restored': put(player + 2788, 'I', 0)
        checked, order = [], []

        def return_body():
            order.append('body')
            put(x(1), '12f', *matrix)
            return int(case != 'body-gone')

        def clearance():
            values = tuple(struct.unpack('<f', struct.pack('<I', m.cpu.reg_read(getattr(arm, f'UC_ARM64_REG_S{i}'))))[0]
                           for i in range(3))
            checked.append(values)
            return int(case != 'blocked')

        def support_return():
            order.append('support')
            return int(case != 'support-missing')

        def stop_return():
            order.append('stop')
            assert x(0) == player

        m.handlers = {BASE + fn('snapshotBody'): return_body, BASE + fn('clearReturn'): clearance,
                      BASE + fn('moveEventSupport'): support_return, BASE + m.bindings["kSetForwardSpeed"]: stop_return}
        # Existing cover state is reused; avoid testing the unrelated render singleton here.
        ev('cancelTick', 'Q', 19200000)
        result = invoke(fn('beforeCancelStage'), node)
        assert bool(result) == (case in ('blocked', 'support-missing')), m.case
        if case in ('arrived', 'collision-restored'):
            assert not checked and not order
            assert abs(get(node + 64, 'f')[0] - 11.05) < 0.001
        elif case == 'blocked':
            assert len(checked) == 10 and order[-1] == 'stop'
        elif case == 'support-missing': assert order[-1] == 'stop'
        else:
            expected = (0, 11.05, 0) if case == 'body-gone' else (4, 11.05, -3)
            assert all(abs(v-w)<0.001 for v,w in zip(get(node+60,'3f'),expected))
            assert order[-1] == 'support'
        results.append(m.case)

    # Both preload entry and readiness must use the same outside point. Upward/foreign events pass through.
    for hook in ('StartPreloadHook', 'PreloadReadyHook'):
        for case in ('deep', 'ordinary', 'object', 'released', 'different-target'):
            reset(hook + '-' + case)
            setup_event(1 if case != 'object' else 2, 1)
            ev('drop', '3f', 0, -103.3, 0)
            if case == 'ordinary': ev('active', 'B', 0)
            if case == 'released': ev('released', 'B', 1)
            position = HEAP + 0xf000
            point = (0, 50, 0) if case == 'different-target' else (0, -100, 0)
            put(position, '3f', *point)
            expected = (0, -103.3, 0) if case == 'deep' else point

            def preload_original():
                assert all(abs(v-w) < 0.001 for v,w in zip(get(x(1), '3f'), expected))
                if hook == 'StartPreloadHook': assert x(0) == actor and x(2) == 0
                else: assert x(0) == 0
                return 1

            m.handlers = {BASE + m.report['adapters']['original_' + hook]: preload_original}
            assert invoke(m.report['adapters']['entry_' + hook], actor if hook == 'StartPreloadHook' else 0, position, 0) == 1
            results.append(m.case)

    # The live component publishes its post-update position without following a retained actor in rendering.
    for owned in (False, True):
        reset('camera-target-' + str(owned))
        setup_event(1, 3)
        camera = HEAP + 0xf000
        put(camera + 24, 'Q', actor)
        ev('active', 'B', int(owned))
        ev('link', '3f', 0, 100, 0)

        def target_original():
            assert x(0) == camera and x(1) == context
            put(actor + m.bindings["actor::kPosition"], '3f', 0, 97.6, 0)

        m.handlers = {BASE + m.report['adapters']['original_CameraTargetHook']: target_original}
        invoke(m.report['adapters']['entry_CameraTargetHook'], camera, context)
        assert abs(get(event + types['Event']['link'], '3f')[1] - (97.6 if owned else 100)) < 0.001
        results.append(m.case)

    reset('camera-target-replacement-retires-owned-event')
    setup_event(1,3)
    camera = HEAP+0xf000
    owned_address=next(v for k,v in globals.items() if k.endswith('g_ownedSerialE'))
    wanted_address=next(v for k,v in globals.items() if k.endswith('g_swirlWantedE'))
    put(owned_address,'I',7)
    put(wanted_address,'I',1)
    put(camera+24,'Q',actor+0x100)
    invoke(fn('afterCameraTarget'),camera)
    assert not get(event+types['Event']['active'],'B')[0]
    assert get(owned_address,'I')[0] == get(wanted_address,'I')[0] == 0
    results.append(m.case)

    # Native world classification supplies the depth cue; no hand-written height threshold.
    for rate in (0.0, 0.5, 1.0):
        reset('depths-rate-' + str(rate))
        manager, vtable, service = HEAP + 0x3000, HEAP + 0x4000, BASE + 0x74befc
        put(BASE + m.bindings["kWorldSlot"], 'Q', module if m.bindings['kWorldDirect'] else slot)
        if not m.bindings['kWorldDirect']:
            put(slot, 'Q', module)
        put(module + 0xd0, 'Q', manager)
        put(manager, 'Q', vtable)
        put(vtable + m.bindings['kWorldPositionMethod'], 'Q', service)

        def classify():
            assert x(0) == manager and get(x(1), '3f') == (10, -500, 15)
            floats([rate])

        m.handlers = {service: classify}
        floats([10, -500, 15])
        assert bool(invoke(fn('depthsAt'))) == (rate >= 1)
        results.append(m.case)

    # Raw text and its cache identity change together, including the refresh-only path.
    prompt = next(address for symbol,address in globals.items() if symbol.endswith('g_promptDescendE'))
    for mode, word in ((0, None), (1, 'Descend'), (2, 'Depths')):
        for set_text in (False, True):
            reset(f'prompt-{mode}-{set_text}')
            message, label = HEAP + 0xe000, HEAP + 0xe100
            put(prompt, 'I', mode)
            m.cpu.mem_write(label, b'0028\0')
            invoke(fn('onPromptMessage'), message, label, int(set_text))
            assert get(message+16, 'Q')[0] == ((1 << 64) - mode if mode else 0)
            if set_text and mode:
                pointer, length = get(message, 'Q')[0], get(message+8, 'i')[0]
                assert bytes(m.cpu.mem_read(pointer, length*2)).decode('utf-16le') == word
            else: assert get(message, 'Q')[0] == 0
            results.append(m.case)

    a.out.write_text(json.dumps({'passed': len(results), 'cases': results,
                                'scope': 'Placed ARM adapters and travel gate; native services mocked'}, indent=2) + '\n')
    print(json.dumps({'passed': len(results)}))


if __name__ == '__main__':
    main()
