#!/usr/bin/env python3
"""Host tests for the mini-program firmware compatibility parser."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import unittest
import tempfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_firmware", ROOT / "tools" / "verify_firmware.py"
)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFY
SPEC.loader.exec_module(VERIFY)


def sample_table(include_diag: bool = False) -> bytes:
    entries = (
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 1, 0xF000, 0x1000, "phy_init"),
        (0, 0, 0x10000, 0x300000, "factory"),
        (1, 2, 0x356000, 0x4000, "cardid"),
        (0, 0x20, 0x700000, 0x100000, "recovery"),
    )
    if include_diag:
        entries = entries[:3] + ((1, 0x40, 0x310000, 0x40000, "diag_log"),) + entries[3:]
    raw = bytearray(b"\xff" * VERIFY.PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        VERIFY.ENTRY.pack_into(
            raw,
            index * VERIFY.ENTRY.size,
            0x50AA,
            kind,
            subtype,
            offset,
            size,
            label.encode().ljust(16, b"\0"),
            0,
        )
    marker = len(entries) * VERIFY.ENTRY.size
    struct.pack_into("<H", raw, marker, 0xEBEB)
    raw[marker + 16 : marker + 32] = hashlib.md5(raw[:marker]).digest()
    return bytes(raw)


class PartitionParserTest(unittest.TestCase):
    def test_diagnostic_seed_required_and_runtime_records_forbidden(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'bootloader').mkdir()
            (folder / 'bootloader/bootloader.bin').write_bytes(VERIFY.RECOVERY_BOOT_MARKER)
            (folder / 'FoloToy-AI-Passport.bin').write_bytes(b'\xe9' + b'\x00' * 31)
            header = struct.pack('<IHHII11I', 0x314A4456, 1, 64, 4096, 0x40000, *([0] * 11))
            header += struct.pack('<I', zlib.crc32(header))
            (folder / 'diag_log_seed.bin').write_bytes(header)
            merged = bytearray(b'\xff' * (0x310000 + 64))
            merged[0x8000:0x8c00] = sample_table(include_diag=True)
            merged[0x10000] = 0xe9
            merged[0x310000:0x310040] = header
            VERIFY.verify_recovery_contract(bytes(merged), folder)
            bad = bytearray(merged)
            bad[0x310000] ^= 1
            with self.assertRaisesRegex(ValueError, 'header'):
                VERIFY.verify_recovery_contract(bytes(bad), folder)
            with self.assertRaisesRegex(ValueError, 'runtime diagnostic'):
                VERIFY.verify_recovery_contract(bytes(merged) + b'\x00', folder)

    def test_parses_protected_layout_and_md5(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(sample_table())
        self.assertTrue(found_md5)
        self.assertEqual(partitions[-2].label, "cardid")
        self.assertEqual(partitions[-1].offset, VERIFY.RECOVERY_OFFSET)

    def test_rejects_bad_md5(self) -> None:
        raw = bytearray(sample_table())
        raw[28] ^= 1
        with self.assertRaisesRegex(ValueError, "MD5"):
            VERIFY.parse_partition_table(bytes(raw))


if __name__ == "__main__":
    unittest.main()
