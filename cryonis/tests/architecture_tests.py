# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Keep Cryonis policy and native code on their documented sides of the boundary."""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]

class Architecture(unittest.TestCase):
    def test_pure_headers_do_not_import_native_code(self):
        for path in (ROOT / 'src/pure').glob('*.hpp'):
            text = path.read_text()
            for include in re.findall(r'#include\s+[<"]([^>"\n]+)', text):
                self.assertNotIn(include, {'lib.hpp', 'CryonisRuntime.hpp', 'CryonisRuntimeState.hpp'})
                self.assertFalse(include.startswith(('totk/', 'nvn/', '../engine', '../feature')), str(path))
            self.assertIsNone(re.search(r'Logging\.|reinterpret_cast|HOOK_DEFINE|std::uintptr_t', text), str(path))

    def test_adapters_do_not_import_feature_globals(self):
        for path in (ROOT / 'src/engine').glob('*'):
            if path.suffix not in {'.hpp', '.cpp'}:
                continue
            self.assertIsNone(re.search(r'CryonisRuntime|runtimeState|../feature/', path.read_text()), str(path))

    def test_coordinator_contains_no_native_calls_or_offsets(self):
        for path in (ROOT / 'src/feature').glob('*.cpp'):
            self.assertIsNone(re.search(r'reinterpret_cast|call<|read<|0x[0-9a-fA-F]{5,}', path.read_text()), str(path))

    def test_comments_are_single_line_invariants(self):
        paths = [*(ROOT / 'src').rglob('*.cpp'), *(ROOT / 'src').rglob('*.hpp'), *(ROOT / 'shaders').glob('*')]
        for path in paths:
            if '/lib/' in path.as_posix():
                continue
            text = path.read_text()
            self.assertNotIn('/*', text, str(path))
            lines = text.splitlines()
            for first, second in zip(lines, lines[1:]):
                if first.strip().startswith('//') and second.strip().startswith('//'):
                    self.assertTrue(first.startswith('// SPDX-License-Identifier:') and second.startswith('// Copyright'), str(path))

if __name__ == '__main__':
    unittest.main()
