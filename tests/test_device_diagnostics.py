#!/usr/bin/env python3
"""Offline diagnostic decoder: committed records, truncation and wrap."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

SPEC = importlib.util.spec_from_file_location('device_diagnostics', Path(__file__).parents[1] / 'tools/device_diagnostics.py')
D = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(D)


def record(sequence, kind=45, args=(1, 178, 0, 0, 0, 2, 2, 3560)):
    data = D.EVENT.pack(D.RECORD_MAGIC, 1, kind, sequence, 123, 6000, 99, *args, 0, D.COMMIT)
    return data[:56] + struct.pack('<II', zlib.crc32(data[:56]), D.COMMIT)


class DecoderTest(unittest.TestCase):
    def image(self):
        return bytearray(D.seed_header() + b'\xff' * (D.SIZE - D.RECORD))

    def test_numeric_stop_reason_and_crc_rejection(self):
        raw = self.image()
        raw[D.SECTOR:D.SECTOR + 64] = record(1)
        raw[D.SECTOR + 64:D.SECTOR + 128] = record(2)
        raw[D.SECTOR + 64 + 24] ^= 1
        records, metadata = D.decode(bytes(raw))
        self.assertEqual(metadata['valid_records'], 1)
        self.assertEqual(metadata['rejected_torn_or_corrupt_records'], 1)
        self.assertEqual(records[0]['details']['reason'], 'up_button')
        self.assertEqual(records[0]['details']['queued_frames'], 178)

    def test_resource_stage_and_terminal_delivery(self):
        resources = D.details(48, [1, 19, 6, (2 << 16) | 4, 17, 0, 105, 2])
        self.assertEqual(resources['stage'], 'mbuf_allocation')
        self.assertEqual((resources['msys_free'], resources['msys_minimum_free']), (4, 2))
        self.assertEqual(resources['allocation_failures'], 17)
        ending = D.details(50, [1, 6, 0, 1, 0, 105, 2, 60])
        self.assertEqual(ending['mode'], 'error')
        self.assertTrue(ending['disconnect_requested'])
        self.assertEqual(ending['notify_rc'], 6)
        self.assertEqual(D.details(50, [0] * 8)['mode'], 'submit')

    def test_per_pool_exhaustion_and_unknown_snapshot(self):
        raw = self.image()
        raw[D.SECTOR:D.SECTOR + 64] = record(1, 51, (0, 0, 12, 256, 12, 10, 24, 320))
        records, metadata = D.decode(bytes(raw))
        self.assertEqual(metadata['valid_records'], 1)
        self.assertEqual(records[0]['event'], 'msys_pools')
        pools = records[0]['details']
        self.assertEqual(pools['msys_1']['free'], 0)
        self.assertEqual(pools['msys_2']['free'], 12)
        unknown = D.details(51, [0xffffffff, 0xffffffff, 0, 0] * 2)
        self.assertIsNone(unknown['msys_1']['free'])
        self.assertIsNone(unknown['msys_2']['minimum_free_since_boot'])

    def test_sequence_wrap_is_chronological(self):
        raw = self.image()
        for index, seq in enumerate((0, 0xfffffffe, 1, 0xffffffff)):
            raw[D.SECTOR + 64 * index:D.SECTOR + 64 * (index + 1)] = record(seq)
        records, _ = D.decode(bytes(raw))
        self.assertEqual([r['sequence'] for r in records], [0xfffffffe, 0xffffffff, 0, 1])

    def test_bad_header_truncation_and_uncommitted_record(self):
        with self.assertRaises(ValueError):
            D.decode(b'\xff' * D.SIZE)
        with self.assertRaises(ValueError):
            D.decode(bytes(self.image()[:-1]))
        raw = self.image()
        raw[D.SECTOR:D.SECTOR + 64] = record(1)
        raw[D.SECTOR + 60:D.SECTOR + 64] = b'\xff' * 4
        records, metadata = D.decode(bytes(raw))
        self.assertEqual(records, [])
        self.assertEqual(metadata['rejected_torn_or_corrupt_records'], 1)


if __name__ == '__main__':
    unittest.main()
