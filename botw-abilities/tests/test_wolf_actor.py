# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Reject private graphs that disconnect the inherited native movement update."""

from pathlib import Path
import sys
import unittest
import byml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_wolf_actor import ACTOR, DONOR_DROP, NO_DROP_ELEMENTS, NO_DROP_TABLE, component_references, encode, leave_nothing, root_motion_contract, unresolved_components


class RootMotionTests(unittest.TestCase):
    def setUp(self):
        self.actor = f'Actor/{ACTOR}.engine__actor__ActorParam.bgyml'
        self.parent = 'Actor/Parent.engine__actor__ActorParam.bgyml'
        self.parameter = 'Component/AnimeDrivenMoveParam/Test.bgyml'
        self.files = {
            self.actor: encode({'$parent': 'Work/Actor/Parent.engine__actor__ActorParam.gyml'}),
            self.parent: encode({'Components': {'AnimeDrivenMoveRef': '?' + self.parameter}}),
            self.parameter: encode({'PartialSlotName': 'Root'}),
        }
        self.graph = {'Partials': [{'Name': 'Root', 'Is Material Slot': False}]}

    def test_inherited_component_binds_skeletal_slot(self):
        result = root_motion_contract(self.files, self.graph)
        self.assertEqual(result['inherited_from'], self.parent)
        self.assertEqual(result['parameter'], self.parameter)
        self.assertEqual(result['slot'], 0)

    def test_slot_index_is_resolved_by_name(self):
        self.graph['Partials'].insert(0, {'Name': 'Eyes', 'Is Material Slot': True})
        self.assertEqual(root_motion_contract(self.files, self.graph)['slot'], 1)

    def test_missing_duplicate_and_material_slots_fail(self):
        slot = self.graph['Partials'][0]
        for slots in ([], [slot, slot], [{'Name': 'Root', 'Is Material Slot': True}]):
            with self.subTest(slots=slots), self.assertRaisesRegex(ValueError, 'skeletal Root'):
                root_motion_contract(self.files, {'Partials': slots})

    def test_disabled_component_does_not_fall_back_to_parent(self):
        self.files[self.actor] = encode({'Components': {'AnimeDrivenMoveRef': ''},
            '$parent': 'Work/Actor/Parent.engine__actor__ActorParam.gyml'})
        with self.assertRaisesRegex(ValueError, 'missing or disabled'):
            root_motion_contract(self.files, self.graph)

    def test_changed_native_settings_fail(self):
        for settings in ({'PartialSlotName': 'Eyes'}, {'PartialSlotName': 'Root', 'EnableMode': byml.Int(1)}):
            self.files[self.parameter] = encode(settings)
            with self.assertRaisesRegex(ValueError, 'native defaults'):
                root_motion_contract(self.files, self.graph)

    def test_missing_component_and_parent_cycle_fail(self):
        for parent, error in (({}, 'no inherited'),
                ({'$parent': 'Work/' + self.actor[:-6] + '.gyml'}, 'inheritance cycle')):
            self.files[self.parent] = encode(parent)
            with self.assertRaisesRegex(ValueError, error):
                root_motion_contract(self.files, self.graph)


class LeaveNothingTests(unittest.TestCase):
    def setUp(self):
        self.source = DONOR_DROP.removeprefix('?')
        self.files = {self.source: encode({'$parent': 'Work/Component/DropParam/Default.game__component__DropParam.gyml',
                                           'DropTableResourceList': ['Work/Drop/DropTable/Meat.game__drop__DropTable.gyml']})}
        self.actor = {'Components': {'DropRef': DONOR_DROP}}
        self.no_drop = {NO_DROP_TABLE: b'table', NO_DROP_ELEMENTS: b'elements'}

    def test_drop_reference_stays_and_names_the_empty_table(self):
        changed = {}
        leave_nothing(self.files, changed, self.actor, self.no_drop)
        destination = f'Component/DropParam/{ACTOR}.game__component__DropParam.bgyml'
        self.assertEqual(self.actor['Components']['DropRef'], '?' + destination)
        self.assertEqual(byml.Byml(self.files[destination]).parse()['DropTableResourceList'],
                         ['Work/Drop/DropTable/Animal_NoDrop_Normal.game__drop__DropTable.gyml'])
        self.assertEqual(changed, {destination: self.source})
        self.assertEqual(self.files[NO_DROP_TABLE], b'table')
        self.assertEqual(self.files[NO_DROP_ELEMENTS], b'elements')

    def test_changed_donor_or_conflicting_stock_copy_fail(self):
        with self.assertRaisesRegex(ValueError, 'drop component changed'):
            leave_nothing(self.files, {}, {'Components': {}}, self.no_drop)
        self.files[NO_DROP_TABLE] = b'other'
        with self.assertRaisesRegex(ValueError, 'differs from the donor copy'):
            leave_nothing(self.files, {}, self.actor, self.no_drop)


class ComponentReferenceTests(unittest.TestCase):
    def setUp(self):
        self.actor = f'Actor/{ACTOR}.engine__actor__ActorParam.bgyml'
        self.parent = 'Actor/Parent.engine__actor__ActorParam.bgyml'
        self.files = {
            self.actor: encode({'$parent': 'Work/Actor/Parent.engine__actor__ActorParam.gyml',
                                'Components': {'ASRef': '?AS/Wolf.root.asb', 'LifeRef': '?Component/Life/Child.bgyml'}}),
            self.parent: encode({'Components': {'LifeRef': '?Component/Life/Parent.bgyml',
                                                'DropRef': '?Component/Drop/Parent.bgyml'}}),
            'Component/Life/Child.bgyml': b'x',
        }

    def test_parent_reference_counts_where_the_child_is_silent(self):
        self.assertEqual(component_references(self.files), {'ASRef': 'AS/Wolf.root.asb',
                         'LifeRef': 'Component/Life/Child.bgyml', 'DropRef': 'Component/Drop/Parent.bgyml'})
        self.assertEqual(unresolved_components(self.files, Path('/nonexistent')),
                         ['ASRef: AS/Wolf.root.asb', 'DropRef: Component/Drop/Parent.bgyml'])


if __name__ == '__main__':
    unittest.main()
