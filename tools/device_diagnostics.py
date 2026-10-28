#!/usr/bin/env python3
"""Create, read and decode the bounded numeric on-device diagnostic journal."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import zlib

OFFSET, SIZE, SECTOR, RECORD = 0x310000, 0x40000, 4096, 64
HEADER_MAGIC, RECORD_MAGIC, COMMIT = 0x314A4456, 0x31524456, 0x54494D43
HEADER = struct.Struct('<IHHII11II')
EVENT = struct.Struct('<IHH12I2I')
EVENT_NAMES = {
    1: 'boot', 2: 'build', 3: 'heartbeat', 4: 'queue_dropped', 5: 'storage_slow',
    10: 'connect', 11: 'disconnect', 12: 'mtu', 13: 'subscribe', 14: 'link',
    15: 'link_request', 16: 'data_length', 17: 'nimble_reset',
    20: 'host_ready', 21: 'host_state', 22: 'host_rejected',
    30: 'button', 31: 'start_requested', 32: 'stop_requested', 33: 'button_send',
    40: 'capture_started', 41: 'first_frame', 42: 'audio_stats', 43: 'tx_retry',
    44: 'tx_failure', 45: 'capture_stopped', 46: 'capture_aborted', 47: 'microphone_error',
    48: 'tx_resources', 49: 'start_blocked', 50: 'end_delivery', 51: 'msys_pools',
}
REASONS = ['none', 'up_button', 'ok_click', 'ok_long', 'host_error', 'disconnect',
           'transport_error', 'audio_pause', 'audio_open', 'audio_read', 'mtu',
           'start_tx', 'session_changed']


def seed_header(size: int = SIZE) -> bytes:
    header = HEADER.pack(HEADER_MAGIC, 1, RECORD, SECTOR, size, *([0] * 11), 0)
    return header[:60] + struct.pack('<I', zlib.crc32(header[:60]))


def validate_header(raw: bytes) -> int:
    if len(raw) < RECORD:
        raise ValueError('Diagnostic header is truncated')
    magic, version, record, sector, size, *_ = HEADER.unpack_from(raw)
    if (magic, version, record, sector) != (HEADER_MAGIC, 1, RECORD, SECTOR):
        raise ValueError('Unsupported diagnostic format; no automatic initialization')
    if size < 3 * SECTOR or size % SECTOR:
        raise ValueError('Invalid diagnostic capacity')
    if zlib.crc32(raw[:60]) != struct.unpack_from('<I', raw, 60)[0]:
        raise ValueError('Diagnostic header CRC mismatch')
    return size


def details(kind: int, args: list[int]) -> dict:
    a = args
    if kind == 51:
        return {name: {'free': None if a[i] == 0xffffffff else a[i],
                       'minimum_free_since_boot': None if a[i + 1] == 0xffffffff else a[i + 1],
                       'blocks': a[i + 2], 'block_bytes': a[i + 3]}
                for name, i in (('msys_1', 0), ('msys_2', 4))}
    if kind == 1:
        names = ['unknown', 'poweron', 'external', 'software', 'panic', 'interrupt_watchdog',
                 'task_watchdog', 'watchdog', 'deep_sleep', 'brownout', 'sdio', 'usb',
                 'jtag', 'efuse', 'power_glitch', 'cpu_lockup']
        return {'reset_reason': names[a[0]] if a[0] < len(names) else f'unknown_{a[0]}',
                'free_heap': a[1], 'minimum_free_heap': a[2], 'capacity_records': a[4]}
    if kind == 2:
        return {'elf_sha256': struct.pack('<8I', *a).hex()}
    if kind in (32, 45, 46, 47):
        reason = REASONS[a[0]] if a[0] < len(REASONS) else f'unknown_{a[0]}'
        result = {'reason': reason}
        if kind in (45, 46):
            result.update(queued_frames=a[1], retries=a[2],
                          **({'control_rc': a[3]} if kind == 45 else {}))
        elif kind == 47:
            result.update(rc=a[1], duration_us=a[2])
        return result
    if kind == 21:
        states = ['unknown', 'ready', 'recording', 'processing', 'success', 'error']
        return {'state': states[a[0]] if a[0] < len(states) else 'unknown',
                'device_flags': a[1], 'queued_frames': a[2],
                'message_categories': [name for bit, name in ((1, 'stall'), (2, 'timeout'),
                    (4, 'cancel'), (8, 'websocket'), (16, 'audio')) if a[4] & bit],
                'message_hash': f'{a[5]:08x}', 'message_bytes': a[6]}
    if kind == 42:
        return dict(zip(('queued_frames', 'retries', 'max_read_us', 'max_tx_ms',
                         'max_frame_gap_ms', 'free_heap', 'minimum_free_heap', 'stack_watermark'), a))
    if kind == 30:
        result = dict(zip(('button', 'edge', 'action', 'duration_ms', 'device_flags',
                           'ready_epoch', 'press_epoch', 'stop_reason'), a))
        for key, names in (('button', ['up', 'down', 'ok']),
                           ('edge', ['press', 'release', 'long', 'other']),
                           ('action', ['none', 'click', 'long'])):
            result[key + '_name'] = names[result[key]] if result[key] < len(names) else 'unknown'
        return result
    if kind == 14:
        return dict(zip(('origin', 'connection', 'mtu', 'interval_us', 'latency',
                         'supervision_ms', 'epoch', 'device_flags'), a))
    if kind in (43, 44):
        return {'attribute': a[0], 'rc': a[1], 'attempts': a[2], 'elapsed_ms': a[3]}
    if kind == 48:
        return {'stage': {0: 'summary', 1: 'mbuf_allocation', 2: 'notify_stack'}.get(a[0], 'unknown'),
                'attribute': a[1], 'rc': a[2], 'msys_free': a[3] & 0xffff,
                'msys_minimum_free': a[3] >> 16, 'allocation_failures': a[4],
                'stack_failures': a[5], 'queued_frames': a[6], 'epoch': a[7]}
    if kind == 49:
        return {'device_flags': a[0], 'epoch': a[1], 'queued_frames': a[2],
                'button_duration_ms': a[3]}
    if kind == 50:
        return {'mode': 'error' if a[0] else 'submit', 'notify_rc': a[1],
                'cancelled': bool(a[2]), 'disconnect_requested': bool(a[3]),
                'disconnect_rc': a[4], 'queued_frames': a[5], 'epoch': a[6], 'device_flags': a[7]}
    return {}


def decode(raw: bytes) -> tuple[list[dict], dict]:
    size = validate_header(raw)
    if len(raw) != size:
        raise ValueError(f'Expected {size} diagnostic bytes, got {len(raw)}')
    records = []
    rejected = 0
    for offset in range(SECTOR, size, RECORD):
        chunk = raw[offset:offset + RECORD]
        if chunk == b'\xff' * RECORD:
            continue
        values = EVENT.unpack(chunk)
        magic, version, kind, seq, boot, uptime, session, *tail = values
        args, crc, commit = tail[:8], tail[8], tail[9]
        if magic != RECORD_MAGIC or version != 1 or commit != COMMIT or zlib.crc32(chunk[:56]) != crc:
            rejected += 1
            continue
        records.append({'sequence': seq, 'boot_id': f'{boot:08x}', 'uptime_ms': uptime,
                        'event': EVENT_NAMES.get(kind, f'event_{kind}'), 'session': session,
                        'args': args, 'details': details(kind, args)})
    newest = None
    for record in records:
        if newest is None or 0 < (record['sequence'] - newest) % 2**32 < 2**31:
            newest = record['sequence']
    if newest is not None:
        records.sort(key=lambda r: -((newest - r['sequence']) % 2**32))
    return records, {'valid_records': len(records), 'rejected_torn_or_corrupt_records': rejected,
                     'capacity_records': (size - SECTOR) // RECORD}


def read_device(port: str, output: Path) -> None:
    # Lazy import: seed generation and offline decoding need only Python stdlib.
    from esptool import cmds
    from esptool.targets.esp32c3 import ESP32C3ROM
    esp = None
    try:
        esp = ESP32C3ROM(port, 115200)
        esp.connect(mode='usb-reset', attempts=3)
        esp = esp.run_stub()
        esp.change_baud(921600)
        cmds.attach_flash(esp)
        table = esp.read_flash(0x8000, 0xc00)
        entries = [struct.unpack_from('<HBBII16sI', table, i) for i in range(0, 0xc00, 32)]
        match = [e for e in entries if e[0] == 0x50aa and e[5].split(b'\0')[0] == b'diag_log']
        if len(match) != 1 or match[0][1:5] != (1, 0x40, OFFSET, SIZE):
            raise ValueError('Device does not have the expected diagnostic partition')
        raw = esp.read_flash(OFFSET, SIZE)
        validate_header(raw)
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            raise FileExistsError('Refusing to overwrite a previous diagnostic capture')
        output.write_bytes(raw)
    finally:
        if esp is not None:
            try:
                esp.watchdog_reset()
            finally:
                esp._port.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    seed = sub.add_parser('seed', help='Create the immutable header only; preserve data sectors on upgrade')
    seed.add_argument('output', type=Path)
    read = sub.add_parser('read', help='Read logs via USB (resets BLE; use while idle); requires esptool 5.6.0')
    read.add_argument('--port', required=True)
    read.add_argument('--output', type=Path, required=True)
    dec = sub.add_parser('decode', help='Decode raw capture to standard JSON')
    dec.add_argument('input', type=Path)
    dec.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.command == 'seed':
        args.output.write_bytes(seed_header())
    elif args.command == 'read':
        read_device(args.port, args.output)
    else:
        records, metadata = decode(args.input.read_bytes())
        data = json.dumps({'metadata': metadata, 'records': records}, indent=2)
        if args.output:
            args.output.write_text(data + '\n', encoding='utf-8')
        else:
            print(data)


if __name__ == '__main__':
    main()
