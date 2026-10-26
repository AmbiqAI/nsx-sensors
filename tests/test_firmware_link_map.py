"""Map provenance contracts; real ELF/map fixtures run in the Arm CI job."""
import unittest

from firmware_link_map import parse_map

SYMBOL = "bme688_validate_config"
ORIGIN = "sensors/libnsx_sensor_bme688.a(bme688.c.o)"
GNU = f"""Discarded input sections
 .text.dead 0x0 0x40 fake.o
Linker script and memory map
.text 0x8000 0x100
 .text.bme688_validate_config
                0x8000 0x22 {ORIGIN}
                0x8000 {SYMBOL}
 .debug_info    0x0 0xffff debug.o
"""
LLD = f"""     VMA      LMA     Size Align Out     In      Symbol
    8000     8000       22     4 .text
    8000     8000       22     4         {ORIGIN}:(.text.bme688_validate_config)
    8001     8001       22     1                 {SYMBOL}
       0        0     ffff     1         debug.o:(.debug_info)
"""
ARM = f"""    Global Symbols
    {SYMBOL} 0x8001 Thumb Code 34 bme688.c.o(.text.bme688_validate_config)
Memory Map of the image
    0x8000 0x22 Code RO 12 * .text.bme688_validate_config {ORIGIN}
"""

ARM_LOAD = ARM.replace("0x8000 0x22 Code", "0x8000 0x10000 0x22 Code")


class FirmwareMapTests(unittest.TestCase):
    def verify(self, text, address=0x8000):
        parse_map(text).verify(SYMBOL, address, "libnsx_sensor_bme688.a", "bme688.c")

    def test_supported_formats_and_thumb_addresses(self):
        for text in (GNU, LLD, ARM, ARM_LOAD):
            with self.subTest(text=text):
                self.verify(text)

    def test_replacement_object_and_wrong_archive_member(self):
        for text in (GNU, LLD, ARM, ARM_LOAD):
            for origin in ("replacement.o", "other.a(bme688.c.o)",
                           "libnsx_sensor_bme688.a(replacement.c.o)"):
                with self.subTest(text=text, origin=origin):
                    with self.assertRaisesRegex(ValueError, "wrong defining object"):
                        self.verify(text.replace(ORIGIN, origin))

    def test_stale_map_address(self):
        for text in (GNU, LLD, ARM, ARM_LOAD):
            with self.assertRaisesRegex(ValueError, "does not match ELF address"):
                self.verify(text, address=0x9000)

    def test_archive_listing_or_discarded_section_is_not_provenance(self):
        text = f"""Discarded input sections
 .text.bme688_validate_config 0x8000 0x22 {ORIGIN}
Linker script and memory map
LOAD sensors/libnsx_sensor_bme688.a
 0x8000 {SYMBOL}
"""
        with self.assertRaisesRegex(ValueError, "wrong defining object"):
            self.verify(text)

    def test_ambiguous_overlapping_owners(self):
        with self.assertRaisesRegex(ValueError, "wrong defining object"):
            self.verify(GNU + " .text.other 0x8000 0x22 other.o\n")

    def test_code_at_zero_does_not_overlap_debug_provenance(self):
        self.verify(GNU.replace("0x8000", "0x0"), address=0)

    def test_unknown_format_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "unsupported linker map"):
            parse_map("unrecognized map with a plausible archive name " + ORIGIN)


if __name__ == "__main__":
    unittest.main()
