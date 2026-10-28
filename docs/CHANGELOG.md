<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Request a fixed 15 ms BLE interval for the 20 ms audio cadence instead of allowing Windows to retain 30 ms. Record separate MSYS pool snapshots so an exhausted ATT-header pool is distinguishable from audio-payload exhaustion. Keep retry deadlines, buffer counts and BLE V1 unchanged; verify the central's actual interval and repeat device voice acceptance.

- Allocate audio from the existing larger MSYS pool with explicit header room, preserving the smaller pool for control and RX. Fix allocation failures with unused large blocks caused by the zero-size best-fit helper; retain the 12/24 buffer counts and existing retry limits. Add ownership/capacity regression tests and validate the pinned pool configuration.

- Cancel failed captures with a retried session-scoped error instead of submitting truncated audio; disconnect only the affected link if terminal delivery fails. Gate a new UP start until the preceding host result completes. Defer all diagnostic Flash I/O until audio is idle, use a bounded 192-record queue, and record allocation-vs-stack failures, MSYS headroom, blocked starts and terminal delivery. Retain the 80 ms audio retry bound and BLE V1 wire format; long recordings and Windows acceptance require device checks.

- Keep a bounded 256 KiB on-device diagnostic journal with explicit PTT stop sources, button/host/link events, audio timing and enqueue errors. CRC-committed records survive resets and rotate by sector; idle-only writes/erases and numeric-only metadata bound the cost. Add USB export/decoding, journal/decoder tests and a checked header install payload; preserve the 3 MiB app, identity and Recovery contracts. Long-term timing and power impact remain hardware checks.

- Retry temporarily rejected BLE audio notifications for at most 80 ms without duplicating accepted frames, and send PTT lifecycle notifications from the audio worker with a 200 ms bound. Count audio sequences only after successful enqueue, cancel stale retries after reconnection/session changes, request 15-30 ms connection intervals and longer link-layer packets, and log negotiated link parameters and precise transport failures. An explicit host error stops abandoned microphone capture. Windows throughput and end-to-end voice acceptance remain device checks.

- Explicitly mute and power down the speaker DAC before pausing its clocks, while keeping microphone bias/filter state. A zero playback volume now disables the output; only deliberate playback can re-enable it with a running stream. Verify output registers and keep the stream awake for retry if the transition fails. This addresses an unsafe idle-output path identified while investigating intermittent post-flash noise; acoustic reproduction remains a device check.

- Execute short button actions on debounced release instead of waiting for double-click classification. Consecutive taps remain separate actions; OK keeps its 650 ms long-press clear/cancel behavior even after a quick tap, without deleting again on release.

- Protect the native USB Serial/JTAG console with a no-light-sleep lock and an 80 MHz APB-frequency floor while the bus is active. Poll every two seconds with a five-second release grace for brief SOF gaps. A long-silent or unplugged bus permits battery standby again. Host suspend/replug behavior remains a hardware acceptance item; the cause of the earlier isolated disconnect is unconfirmed.

- Avoid repeated clipping at recording start by retaining ES8311 bias/filter state during standby and pausing I2S/DMA instead of fully restarting the codec. Resume fresh DMA and prime one partial I2S slot word (62.5 us at 16 kHz) before returning the full PCM frame, preventing a startup spike from seeding ADPCM. Pause/resume failures retain necessary power locks and support retry instead of aborting the firmware; retained analog current remains unmeasured.

- Reduced screen-off standby activity: pause idle I2S streams, wait for recording events, pause LVGL ticks/refresh/animations while off, and enable dynamic frequency scaling plus automatic light sleep with BLE modem sleep. ADC keys use 20 ms scanning; duplicate host states no longer restart the display timeout. Actual current savings and wake/audio behavior still require board measurements.

- Added the two-second Vokie startup sound at moderate volume. It plays once per boot on the audio worker, yields to voice capture, and mutes and clears startup microphone samples before recording.

- Keep the status screen in OFFLINE until Vokie completes the BLE handshake; DOWN and OK no longer change an unconnected device to READY.
- Placed VOICE/SEND/UNDO close to the right edge with subtle leader lines to their keys. Each right-side key reveals only its matching label and line. Single hints share the middle SEND position, with a compact one-row background and a line from that position to the corresponding physical key; a new key replaces the previous selection. Startup retains the original three label positions and guide paths, shows all three for three seconds, then fades them out. The hardware power button has no application press event.
- Placed the voice state and supporting text above the logo, with the top of READY aligned to the upper key guide. Moved Vokie Power below the logo, with the title's bottom aligned to the lower key guide. The logo's visual center aligns with the SEND row, and all main content remains horizontally centered.
- Added a subdued gray (`#646C78`) battery icon and SOC percentage in the top-right corner, refreshed every 30 seconds and always visible while the screen is lit. Hints selected by a physical key remain visible during recording/processing and fade out after the three-second idle hint timeout. Startup guidance expires after three seconds even if the host becomes active. Status changes and battery refreshes do not reveal the hints. Battery refreshes preserve screen dimming and sleep. The title uses 22 px Barlow Condensed Bold, and the redundant status dot is removed. Readings at or below 20% appear red; invalid SOC falls back to measured voltage, and unavailable readings show `--%`.
- Initialized the CW2017 with the official stock 520 mAh profile from FoloToy PR #38 to restore valid SOC on unconfigured gauges, with readback verification, bounded readiness checks, and no redundant profile writes.

- Added the standalone AI Passport Vokie Plugin alongside the firmware for
  import into compatible Vokie builds and custom desktop-integration development;
  documented the current built-in Vokie connection as the default path.
- Replaced the hardware-demo menu with the Vokie AI Passport voice firmware: a documented BLE V1 peripheral, 16 kHz mono IMA ADPCM transport, physical PTT/edit controls, host-driven status UI, and three-stage backlight power saving.
- Added a fork-specific project README, BLE protocol reference, third-party notices, and separate Vokie brand-asset terms for public distribution.
- Removed the inherited upstream-sync workflow from this standalone derivative; downstream forks update from the public repository through an explicit reviewed merge.

- Reorganized the documentation by function area with a dual entry point: the root `AGENTS.md` is now a thin router (hard constraints + task routing only) and the detailed AI workflow lives in `docs/development/ai-guide.md`; `agent-guide.md` was folded in. `docs/development/` gained a second level (`engineering/`, `ci/`, `release/`), and the `plays/` application archive and `experiences/` moved into a `docs/reference/` area with a dedicated README. Removed `docs/software-design/` (empty scaffold); folded the three `assets/{fonts,images,music}/README` leaves into the `assets/` README; flattened the six `project-completion` sub-documents into a single file; and unified each directory to a single README, eliminating every `INDEX` file and a duplicated experience index. All cross-references and bibliographic links were updated; no content was dropped.

- Made mini-program BLE install compatibility a template-level invariant: fixed
  protected `cardid`/Recovery partitions, retained the five-second UP-key
  Recovery boot hook, and added CI validation for merged-image structure,
  partition MD5/ranges, the 3 MB app limit, and protected payload exclusion.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
