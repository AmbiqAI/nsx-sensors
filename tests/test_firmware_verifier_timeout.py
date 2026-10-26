"""A stalled symbol inspector must fail boundedly through the public CLI."""
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import verify_firmware_link as verifier


class FirmwareVerifierTimeoutTests(unittest.TestCase):
    def test_stalled_nm_is_bounded_and_reported_without_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "firmware.elf"
            header = bytearray(20)
            header[:6] = b"\x7fELF\x01\x01"
            struct.pack_into("<HH", header, 16, 2, 40)
            elf.write_bytes(header)

            def stalled_tool(command, **options):
                # Model a tool that never returns: only a finite timeout stops it.
                timeout = options.get("timeout")
                self.assertIsInstance(timeout, (int, float))
                self.assertGreater(timeout, 0)
                self.assertLessEqual(timeout, 120)
                raise subprocess.TimeoutExpired(command, timeout)

            error = io.StringIO()
            with patch.object(verifier.sys, "argv", ["verify", str(elf), "--map",
                                                    str(elf) + ".map", "bme688"]), \
                 patch.object(verifier.shutil, "which", return_value="arm-none-eabi-nm"), \
                 patch.object(verifier.subprocess, "run", side_effect=stalled_tool), \
                 contextlib.redirect_stderr(error):
                self.assertEqual(verifier.main(), 1)
            self.assertIn("firmware link check:", error.getvalue())
            self.assertIn("timed out", error.getvalue())
            self.assertNotIn("Traceback", error.getvalue())
