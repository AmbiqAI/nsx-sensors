"""Fast offline checks for the target-ABI layout extraction."""
import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch
import resource_qualification as qualification
from resource_qualification import parse_sizes


class LayoutTests(unittest.TestCase):
    def test_hexadecimal_sizes_and_ignore_other_symbols(self):
        self.assertEqual(parse_sizes('resource_size_nsx_adapter B 0 134\n'
                                     'resource_size_nsx_scratch B 134 100\n'
                                     'unrelated T 0 10\n'),
                         {'nsx_adapter': 308, 'nsx_scratch': 256})

    def test_empty_or_non_size_nm_output_is_not_evidence(self):
        self.assertEqual(parse_sizes('resource_size_adapter U\n'), {})


class ExternalInputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        self.workspace = self.root / 'workspace'
        self.paths = [self.sdk, self.workspace / 'zephyr',
                      self.workspace / 'modules/hal/cmsis',
                      self.workspace / 'modules/hal/cmsis_6']
        self.sdk.mkdir()
        self.git(['git', 'init', '-q', self.sdk])
        (self.sdk / 'input.h').write_text('original')
        self.git(['git', '-C', self.sdk, 'add', 'input.h'])
        self.git(['git', '-C', self.sdk, '-c', 'user.name=Test', '-c',
                  'user.email=test@example.invalid', 'commit', '-qm', 'fixture'])
        self.revision = self.git(['git', '-C', self.sdk, 'rev-parse', 'HEAD']).strip()
        for path in self.paths[1:]:
            path.parent.mkdir(parents=True, exist_ok=True)
            self.git(['git', 'clone', '-q', '--no-hardlinks', self.sdk, path])
        for name, value in (
            ('SDK_REVISION', self.revision), ('ZEPHYR_SHA', self.revision),
            ('PROJECTS', {'cmsis': ('modules/hal/cmsis', self.revision),
                          'cmsis_6': ('modules/hal/cmsis_6', self.revision)})):
            context = patch.object(qualification, name, value)
            context.start()
            self.addCleanup(context.stop)

    @staticmethod
    def git(command):
        return subprocess.check_output([str(x) for x in command], text=True,
                                       stderr=subprocess.STDOUT, timeout=10)

    def check(self):
        return qualification.external_inputs(self.sdk, self.workspace, self.git)

    def test_records_all_four_clean_pins(self):
        evidence = self.check()
        self.assertEqual(set(evidence), {'sdk', 'zephyr', 'cmsis', 'cmsis_6'})
        for item in evidence.values():
            self.assertEqual(item['revision'], self.revision)
            self.assertEqual(item['status'], '')

    def test_each_dependency_rejects_tracked_and_untracked_changes(self):
        for path in self.paths:
            with self.subTest(path=path):
                (path / 'input.h').write_text('changed')
                with self.assertRaisesRegex(ValueError, 'clean'):
                    self.check()
                self.git(['git', '-C', path, 'add', 'input.h'])
                with self.assertRaisesRegex(ValueError, 'clean'):
                    self.check()
                self.git(['git', '-C', path, 'reset', '--hard', 'HEAD'])
                (path / 'extra.h').write_text('untracked')
                with self.assertRaisesRegex(ValueError, 'clean'):
                    self.check()
                (path / 'extra.h').unlink()

    def test_wrong_revision_rejected(self):
        with patch.object(qualification, 'ZEPHYR_SHA', '0' * 40):
            with self.assertRaisesRegex(ValueError, 'expected revision'):
                self.check()

    def test_missing_cmsis_rejected(self):
        path = self.paths[-1]
        path.rename(path.with_name('removed'))
        with self.assertRaisesRegex(ValueError, 'cmsis_6: missing'):
            self.check()

    def test_nested_directory_is_not_a_checkout(self):
        child = self.sdk / 'child'
        child.mkdir()
        with self.assertRaisesRegex(ValueError, 'repository root'):
            qualification.external_inputs(child, self.workspace, self.git)


if __name__ == '__main__':
    unittest.main()
