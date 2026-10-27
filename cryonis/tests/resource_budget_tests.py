# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Regressions for fixed native allocations hidden behind tiny BGYML files."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "build_pillar", Path(__file__).resolve().parents[1] / "tools/build_pillar.py")
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


class ResourceBudgets(unittest.TestCase):
    def test_physical_recipe_uses_shared_percentage_baseline(self):
        settings=recipe.tuning()
        self.assertEqual(settings['original_dimensions'],[3,4,3])
        self.assertEqual(settings['size_percent'],90)
        self.assertEqual(settings['dimensions'],[2.7,3.6,2.7])
        self.assertAlmostEqual(settings['settled_submerge'],.18)

    def test_whitespace_does_not_change_tuning(self):
        text = (Path(__file__).resolve().parents[1] / 'src/pure/CryonisTuning.hpp').read_text()
        expected = recipe.parse_tuning(text)
        for variant in (text.replace(' = ', '='), text.replace(' = ', '\n = \t')):
            self.assertEqual(recipe.parse_tuning(variant), expected)

    def test_position_settings_keep_original_native_allocation(self):
        # The original 72-byte parameter needs budget5304; the failed4816 recipe allocated null.
        self.assertLess(72 * 10 + 4096, 5304)
        self.assertGreaterEqual(recipe.cloned_resource_floor(5304, 72, 72), 5304)

    def test_shrinking_serialized_data_does_not_shrink_native_budget(self):
        self.assertGreaterEqual(recipe.cloned_resource_floor(20000, 1000, 20), 20000)

    def test_growth_adds_room_above_existing_native_budget(self):
        self.assertGreater(recipe.cloned_resource_floor(20000, 100, 200),
                           recipe.cloned_resource_floor(20000, 100, 100))

    def test_unknown_original_budget_is_refused(self):
        with self.assertRaises(ValueError):
            recipe.cloned_resource_floor(0, 72, 72)


if __name__ == "__main__":
    unittest.main()
