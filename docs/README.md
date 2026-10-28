# Vokie AI Passport Firmware

English | [简体中文](README.zh_CN.md)

FoloToy AI Passport is open wearable AI hardware. This repository preserves the upstream development baseline while packaging the Vokie AI Passport voice-input firmware as its current application. It keeps the **hardware facts, stable interfaces, resource boundaries, reference implementations, and validation methods** needed to build applications in one place.

The repository is organized around the following principles:

- `main` contains the Vokie BLE voice-input application; the upstream baseline and history remain attributed to FoloToy.
- `components/bsp` isolates board-level details and exposes stable APIs to applications.
- historical `demo/*` source and upstream branches remain references rather than code compiled into this firmware.
- Development conventions for AI assistants live in [`AGENTS.md`](../AGENTS.md) and [`docs/development/ai-guide.md`](development/ai-guide.md); the complete hardware context and troubleshooting knowledge is in [`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).
- Build results and physical-device results are reported separately. A successful build must never be presented as successful hardware validation.

## Hardware capability contract

The table below describes the hardware and application capabilities used by the current firmware. It is not a list of everything that might be possible according to the chip datasheet.

| Capability | Confirmed implementation | Application interface | Boundaries that must be respected |
| --- | --- | --- | --- |
| Display | ST7789P3, 240 × 320 portrait RGB565, SPI2 at 40 MHz; status UI and LEDC backlight control | `bsp_display_*`, `bsp_lvgl_*`, `ui_status_*` | The ESP32-C3 has no PSRAM; the current design uses a small single DMA buffer; the BSP exposes no LCD MISO, touch, or TE interface |
| Input | `UP`, `DOWN`, and `OK` share an ADC resistor ladder on GPIO0; mapped to PTT, send, delete, clear, and cancel | `bsp_button_init()`, `bsp_button_read_mv()` | Callbacks run in the button component task and must not block; do not create a second ADC1 unit |
| Audio | ES8311 microphone capture at 16 kHz, 16-bit mono; independent 20 ms IMA ADPCM frames | `bsp_audio_*`, `vokie_ble_start()` | PCM reads block and run in a worker task; recording requires a subscribed compatible BLE host and ATT MTU 185 or greater |
| Bluetooth LE | Connectable NimBLE peripheral advertising as `Vokie Passport`; Control, Audio, and Device info characteristics | `vokie_ble_*` | Single connection; protocol V1 has no pairing, peer authentication, or application-layer encryption |
| Backlight | 65% active, 38% processing, 18% dim after 3 seconds, off after 20 seconds | `ui_status_touch()`, `ui_status_set_state()` | Screen-off pauses LVGL work and permits automatic light sleep when audio is idle; LCD controller power remains on and BLE stays enabled |
| Protected storage | 3 MB factory app plus fixed `cardid` and permanent Recovery regions | `partitions.csv`, bootloader hook | Do not overwrite provisioned identity or Recovery payloads; use the documented installation paths |
| Logging and flashing | Native ESP32-C3 USB Serial/JTAG | ESP-IDF console | GPIO18/19 are reserved for USB; the default UART0 TX on GPIO21 conflicts with the backlight |

All pins, addresses, panel parameters, and button voltage windows are defined only in [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h). Application code must not duplicate these constants. See the [AI Hardware Development Guide](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) for the complete pin map, panel initialization, ADC thresholds, I2C addressing rules, audio clocks, and memory details.

The application uses ESP-IDF timers, FreeRTOS tasks, NVS initialization, NimBLE, and LVGL. Wi-Fi and historical menu demos are not compiled into the current firmware. The current product and firmware baseline uses 8 MB Flash with a 3 MB factory-app partition plus fixed protected identity and permanent-Recovery regions so derivative firmware stays installable through the mini-program. See the [BLE protocol](ai-passport-ble-protocol.md) for the host contract.

### Capabilities outside the current contract

The public firmware contract is limited to the interfaces listed above. Do not infer additional board interfaces from the ESP32-C3 feature list. New hardware interfaces require an explicit BSP definition and on-device acceptance criteria.

## Extend the firmware safely

A useful request to an AI assistant should name the host behavior and the device
acceptance criteria:

```text
Add a Vokie AI Passport firmware feature without changing the BLE V1 wire format.
Preserve the protected cardid and Recovery partitions, keep hardware facts in
components/bsp, and keep product behavior in main. Start from `main`, create a
`feature/*` branch, run ./tools/validate.sh --static and --firmware, and report
build results separately from unexecuted on-device BLE, audio, and button checks.
```

Before starting, check [`reference/`](reference/README.md) for reusable experience
and archived applications. The current `main` already contains the Vokie voice
input application; do not restore historical demos or infer unsupported board
interfaces from the ESP32-C3 datasheet.

The more specific the requirement, the more likely the assistant is to implement it correctly in one pass. Useful details include:

- User flow: what each page displays and what short press, double press, and long press do for each button.
- State and data: whether the application needs timing, persistence across power loss, networking, recording, or communication with a computer.
- Experience goals: fonts, colors, animation, sound, response time, and error states.
- Constraints: whether the main menu may be replaced, dependencies added, Flash used, or default interactions changed.
- Acceptance criteria: which behaviors require automated tests and which must be observed on real hardware.

When details are omitted, the assistant may choose conservative defaults that do not change the product direction, but it must list those assumptions in the delivery. Decisions involving new wiring, electrical safety, board revisions, or irreversible data formats require confirmation first.

## Historical upstream examples

The original FoloToy repository contains historical `demo/*` branches that may
be useful as design references. They are not branches of this standalone Vokie
repository and are not part of the current firmware contract. Inspect them only
through an explicitly named upstream remote:

```bash
git remote add folotoy https://github.com/FoloToy/ai-passport.git
git fetch --no-tags folotoy 'refs/heads/demo/*:refs/remotes/folotoy/demo/*'
git branch -r --list 'folotoy/demo/*'
git diff main...folotoy/demo/tetris-game -- main components tests
git show folotoy/demo/tetris-game:main/demo_tetris.c
```

Examples can change the same menu, configuration, or driver in incompatible
ways. Treat them as historical references, not as code that is automatically
compatible with the current Vokie application or BSP contract.

For new work, create a short-lived `feature/*` or `fix/*` branch from this
repository's current public `main`; do not develop directly on `main`.

```bash
git switch main
git switch -c feature/my-passport-app
```

## Project structure

```text
components/bsp/include/  Public BSP APIs and bsp_pins.h hardware facts
components/bsp/src/      Display, button, audio, battery, and shared-I2C implementations
main/                    Vokie BLE peripheral, audio transport, and status UI
vokie-plugin/            Importable and modifiable Vokie desktop Plugin
tests/                   Lightweight logic tests that can run without hardware
tools/                   Shared local/CI validation and firmware verification scripts
docs/                    Project docs, changelog, engineering/contribution rules, and design references
.github/                 GitHub community files, PR template, issue forms, and CI workflows
sdkconfig.defaults       ESP32-C3, USB console, Flash, and LVGL defaults
partitions.csv           App plus protected identity/Recovery layout
dependencies.lock        Reproducible ESP-IDF Managed Component resolution
AGENTS.md                Mandatory AI-agent entry point (paired with AGENTS.zh_CN.md)
CLAUDE.md                Claude Code pointer to AGENTS.md (paired Chinese version)
LICENSE                  Repository license
```

## Documentation index

Repository documentation is organized by function area. `authoritative` documents define development or collaboration requirements; `reference` documents provide background or an index.

- [`docs/ai-passport-ble-protocol.md`](ai-passport-ble-protocol.md) — public BLE V1 service, messages, audio framing, lifecycle, and security boundaries.
- [`docs/device-diagnostics.md`](device-diagnostics.md) — persistent numeric device events, bounded storage, USB export and decoding.
- [`docs/THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — dependency versions, licenses, and redistribution notes.
- [`docs/development/`](development/README.md) — engineering rules and reusable workflows: the `ai-guide.md`, `engineering/`, `ci/`, and `release/` areas. Its README lists them.
- [`docs/contribution/`](contribution/README.md) — collaboration, documentation, and commit/PR conventions.
- [`docs/hardware-design/`](hardware-design/README.md) — board facts, constraints, acceptance matrix, and troubleshooting.
- [`docs/reference/`](reference/README.md) — reference material: reusable development experience and archived application playbooks, grouped by contributor (`reference/<username>/`).
- [`docs/brand/`](brand/README.md) — public brand and product language (`brand-and-product.md`) and the official product visual references.
- [`docs/`](README.md) top-level — [`CHANGELOG.md`](CHANGELOG.md), [`brand-and-product.md`](brand/brand-and-product.md), and [`fork-guide.md`](fork-guide.md).

GitHub community documents: [CONTRIBUTING.md](../.github/CONTRIBUTING.md), [CODE_OF_CONDUCT.md](../.github/CODE_OF_CONDUCT.md), [SECURITY.md](../.github/SECURITY.md), and [SUPPORT.md](../.github/SUPPORT.md).

> This README describes the product and repository. AI agents must begin with `AGENTS.md` and follow its task-specific routing.
