"""Fast offline checks for the target-ABI layout extraction."""
import unittest
from resource_qualification import parse_sizes


class LayoutTests(unittest.TestCase):
    def test_hexadecimal_sizes_and_ignore_other_symbols(self):
        self.assertEqual(parse_sizes('resource_size_nsx_adapter B 0 134\n'
                                     'resource_size_nsx_scratch B 134 100\n'
                                     'unrelated T 0 10\n'),
                         {'nsx_adapter': 308, 'nsx_scratch': 256})

    def test_empty_or_non_size_nm_output_is_not_evidence(self):
        self.assertEqual(parse_sizes('resource_size_adapter U\n'), {})


if __name__ == '__main__':
    unittest.main()
