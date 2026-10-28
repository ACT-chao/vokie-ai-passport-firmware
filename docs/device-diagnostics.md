<p align="right">
  <a href="device-diagnostics.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Persistent Device Diagnostics

The device keeps a bounded numeric event journal for intermittent BLE and PTT
failures. Capture requests, debounced button edges/actions, host states, link
changes, microphone failures, enqueue failures, and capture stops identify the
source of an unexpected early stop. Local enqueue success alone cannot prove
delivery to the host. No PCM, transcript, arbitrary host text, or device identity
is stored; host messages retain only category bits, byte length, and a hash.

## Storage and timing

`diag_log` is data subtype `0x40` at `0x310000`, size `0x40000` (256 KiB), in
the gap after the unchanged 3 MiB factory application and before protected
`cardid`. `cardid`, Recovery, NVS, pins, and the BLE V1 wire format are unchanged.
Adding the journal requires the updated partition table; application-only
flashing on an older table leaves diagnostics disabled.

The first 4096-byte sector holds an immutable 64-byte format header. The other
63 sectors hold 4032 records of 64 bytes each. At capacity, the oldest sector is
reclaimed; retention is event-count based, not a promised number of days.
The full firmware includes only the immutable header, never runtime records.
Segmented upgrades write that header sector and preserve all journal data sectors.
On first provisioning, back up the newly allocated region before initializing
it; bytes unassigned in the previous partition table may contain old firmware
remnants. Never erase the entire flash or either protected region.

Producers use a 192-record (12 KiB) RAM queue without waiting. A low-priority worker
appends records; queue overflow is counted by `queue_dropped`. Audio summaries
are emitted every ten seconds and at session end, with first-frame latency,
maximum read duration, TX duration, frame gap, heap, and stack watermark.
Retry details are rate-limited to one per five seconds; idle heartbeats are emitted
after fifteen minutes without another event. Per-second serial audio INFO
output is moved to DEBUG to reduce console work while recording.

An audio-worker gate prevents all journal reads, programs and erases during
capture or playback owned by that worker. One append already running may finish
before the stream starts. Pending records stay in bounded RAM until audio pauses;
therefore an abrupt power loss during a long recording can lose that recording's
queued diagnostics. Idle draining preserves event timestamps and sequence order.
The queue and rate limits cover ordinary five-minute captures, but unusual event
bursts can still overflow and are counted explicitly.

Payload and CRC are programmed before a separate commit word. On boot, the
writer scans committed CRC-valid records, skips torn entries, and resumes the
sequence. The header is never erased at runtime. Unknown/corrupt headers or an
I/O failure disable the writer without automatically formatting storage or
blocking normal BLE operation. The newest RAM-only events can be lost on sudden
power removal; the journal is not a panic/core-dump collector.

## Reading the journal

Wait until recording/submission is idle and connect USB. Reading through the
ROM loader briefly resets BLE and then restarts the application; it does not
erase or write flash. The `read` command requires esptool 5.6.0 in the selected
Python environment. Offline decoding uses only the Python standard library.

```text
python tools/device_diagnostics.py read --port COM17 --output device-log.bin
python tools/device_diagnostics.py decode device-log.bin --output device-log.json
```

The reader verifies the diagnostic partition bounds, refuses to overwrite an
existing capture, and exports only the journal. `boot_id` and uptime associate
events with a reset; timestamps are milliseconds since boot, not calendar time.
The decoder orders events across sequence wrap and counts corrupt/torn entries.

## V1 binary format and event interpretation

All integers are little-endian. The header is magic `VDJ1`, uint16 version and
record size, uint32 sector/partition size, eleven reserved uint32 words, then
IEEE CRC32 of its first 60 bytes. A record is magic `VDR1`, uint16 version/event,
uint32 sequence, boot ID, uptime, session, eight argument words, CRC32 of its
first 56 bytes, and the `CMIT` commit word. Layout assertions and host tests
protect both boundaries. Event IDs are declared in `main/device_diag.h` and
decoded by `tools/device_diagnostics.py`; append new IDs instead of renumbering.

| Event | Relevant evidence |
| --- | --- |
| `button`, `start_requested`, `stop_requested` | Debounced PRESS/RELEASE/LONG, classified action, duration, state flags and connection epoch; distinguish a second UP release from a host-driven stop |
| `host_state` | Ready/recording/processing/success/error, device flags and queued frames before applying the state; optional message category/hash |
| `capture_stopped`, `capture_aborted` | Reason, queued frames, retries, connection/session context; stop also includes control enqueue result |
| `audio_stats`, `first_frame` | Maximum gaps/durations and first-frame latency; successful enqueue is not a host receipt |
| `tx_failure`, `tx_retry`, `microphone_error` | Exact numeric return code, attempts, wait/read duration and stage |
| `connect`, `disconnect`, `mtu`, `subscribe`, `link` | Connection result/reason, actual interval/MTU, subscriptions and epoch |
| `tx_resources` | Stage 1 mbuf allocation vs stage 2 notify-stack rejection; stage 0 summary. Current/minimum free MSYS blocks, cumulative allocation/stack failures, queued frames and epoch |
| `msys_pools` | Separate MSYS 1/2 current free, minimum free since boot, total blocks and block bytes; unknown pools decode as null, not zero |
| `start_blocked`, `end_delivery` | A press while waiting for the previous result; submit vs error notification, return code and same-link disconnect fallback |
| `boot`, `build`, `queue_dropped`, `storage_slow` | Reset reason, ELF hash, lost RAM events and unusually slow storage operations |

State flags are bit 0 recording, 1 stop requested, 2 host ready, 3 control
subscribed, 4 audio subscribed, 5 awaiting host result, 6 sending the terminal event. Button/edge/action enum values follow
`bsp_button.h` and `button_gesture.h`. Named stop reasons are decoded separately;
they do not redefine button semantics. A failed capture sends a session-scoped `device_error` with bounded worker retries,
never an ordinary `ptt_up`. If its terminal event cannot be queued, the worker
requests disconnect of that same connection/session; a replacement link is never
terminated. A clean release sends `ptt_up` and waits for host success/error/ready
before accepting another UP start. A blocked press displays "Wait for result"
and is not queued for later. Explicit host errors still stop capture. These
changes preserve BLE V1 packets and do not extend host watchdogs or claim delivery
from a successful local enqueue. The existing 80 ms audio retry bound is retained
so throughput changes can be evaluated without hiding congestion.

Run `./tools/validate.sh` for portable power-loss/wrap tests, decoder tests,
the firmware build, and header/protected-layout checks. Hardware acceptance must
separately check export across resets, recording while logging, long sessions,
wrap/overflow under load, abrupt power removal and battery impact.

## Audio buffer ownership

Audio payloads explicitly request the larger configured MSYS pool, with 16 bytes
reserved for protocol header room. NimBLE still allocates a separate ATT command
header from the small pool, alongside control and RX; this is not a hard control
buffer reservation. The ESP-IDF 5.5.3 `ble_hs_mbuf_from_flat()` helper requests a zero-size
packet header and chooses the smallest fitting pool without trying the next
pool on exhaustion. Reusing it for every audio packet can therefore return
ENOMEM while all 24 large blocks remain unused. The default 12 x 256-byte and
24 x 320-byte block budgets are retained and checked by firmware validation;
this change does not increase queue capacity or the audio retry deadline.
A capacity check and append-error cleanup protect mbuf ownership before NimBLE
consumes the packet. Host tests reproduce an empty small pool with 24 available
large blocks and verify that exhausted audio buffers leave control capacity.

The peripheral now requests a fixed 15 ms connection interval instead of a
15-30 ms range. A Windows host selected 30 ms within the former range and
exhausted notification resources while producing 20 ms voice frames. A controlled
test on the same firmware with a temporary Windows 15 ms preference removed the
observed failures and retries in repeated short recordings. The fixed request
avoids explicitly allowing that 30 ms choice; the central still controls the
final interval. Always inspect negotiated `link` records and repeat long voice
tests on each host. Neither the pool change nor this request guarantees throughput
on every adapter. Audio retry limits, buffer counts and BLE V1 packets are unchanged.
