"""Check that rebuilding never ignores another product's reservations."""
import unittest
from patch_registry import owned_sources


class RegistryOwnership(unittest.TestCase):
    def test_only_owned_first_party_sources_are_ignored(self):
        own = 'first-party/ultimate-ascend-v0.16.0-standard'
        other = 'first-party/another-mod-v1.0.0'
        self.assertEqual(owned_sources({'sources': {
            own: {'party': 'first-party'}, other: {'party': 'first-party'}}}), {own})

    def test_third_party_name_cannot_claim_ownership(self):
        self.assertFalse(owned_sources({'sources': {
            'first-party/ultimate-ascend-spoof': {'party': 'third-party'}}}))

    def test_old_descend_reservations_are_retained(self):
        self.assertFalse(owned_sources({'sources': {
            'first-party/descend-v0.15.0': {'party': 'first-party'}}}))


if __name__ == '__main__':
    unittest.main()
