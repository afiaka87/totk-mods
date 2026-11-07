# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Resource-selection regressions use deliberately different overlapping clips."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wolf_animation_resources import graph_assets, resource_selection


class ResourcePrecedence(unittest.TestCase):
    def test_later_resource_wins_without_losing_earlier_unique_clips(self):
        resources = [{'name': 'first', 'skeletal': {'idle', 'fallback'}},
                     {'name': 'last', 'skeletal': {'idle'}}]
        before = deepcopy(resources)
        result = resource_selection(resources, {'skeletal': {'idle', 'fallback'}})
        self.assertEqual(result['clips']['skeletal'], {'fallback': 'first', 'idle': 'last'})
        self.assertEqual(result['resources'], ['first', 'last'])
        self.assertEqual(resources, before)

    def test_shadowed_resource_is_not_required(self):
        result = resource_selection([{'name': 'first', 'skeletal': {'idle'}},
                                     {'name': 'last', 'skeletal': {'idle'}}], {'skeletal': {'idle'}})
        self.assertEqual(result['resources'], ['last'])

    def test_material_namespace_is_independent(self):
        result = resource_selection([{'name': 'first', 'material': {'appear'}},
                                     {'name': 'last', 'skeletal': {'appear'}}], {'material': {'appear'}})
        self.assertEqual(result['resources'], ['first'])

    def test_missing_clip_and_duplicate_resources_fail(self):
        for resources in ([{'name': 'one'}], [{'name': 'one'}, {'name': 'one'}]):
            with self.subTest(resources=resources), self.assertRaises(ValueError):
                resource_selection(resources, {'skeletal': {'idle'}})

    def test_material_references_keep_their_original_namespace(self):
        reference = {'Actor/ASList/WolfLink.baslist': {'lists': {'ASDefines': {'objects': {
            'command': {'Filename': 'test'},
        }}}}, 'Actor/AS/test.bas': {'lists': {'Elements': {'lists': {
            str(kind): {'objects': {'Parameters': {'TypeIndex': kind, 'FileName': 'same'}}}
            for kind in (39, 62, 67)
        }}}}}
        self.assertEqual(graph_assets(reference), {'skeletal': {'same'},
                                                   'material': {'same_fvm', 'same_fsp'}})


if __name__ == '__main__':
    unittest.main()
