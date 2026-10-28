#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_battery.c -o "${test_dir}/test_ui_battery"
    "${test_dir}/test_ui_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Icomponents/bsp/include -Imain \
        tests/test_boot_sound.c main/boot_sound.c \
        -o "${test_dir}/test_boot_sound"
    "${test_dir}/test_boot_sound"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c components/bsp/src/bsp_battery.c \
        -o "${test_dir}/test_bsp_battery"
    for scenario in {0..14}; do
        "${test_dir}/test_bsp_battery" "${scenario}"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_idle.c -o "${test_dir}/test_ui_idle"
    "${test_dir}/test_ui_idle"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_usb_standby.c -o "${test_dir}/test_usb_standby"
    "${test_dir}/test_usb_standby"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_PM_ENABLE=1 \
        -Itests/power_stubs -Itests/battery_stubs -Icomponents/bsp/include \
        tests/test_bsp_audio_power.c components/bsp/src/bsp_audio.c \
        -o "${test_dir}/test_bsp_audio_power"
    "${test_dir}/test_bsp_audio_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/lvgl_power_stubs -Itests/power_stubs -Itests/battery_stubs \
        tests/test_lvgl_power.c components/bsp/src/bsp_display_lvgl.c \
        -o "${test_dir}/test_lvgl_power"
    "${test_dir}/test_lvgl_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_button_gesture.c -o "${test_dir}/test_button_gesture"
    "${test_dir}/test_button_gesture"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ble_tx_retry.c main/ble_tx_retry.c \
        -o "${test_dir}/test_ble_tx_retry"
    "${test_dir}/test_ble_tx_retry"
    python3 tests/test_verify_firmware.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    python3 - "${validation_build_dir}/sdkconfig" <<'PYCONFIG'
import pathlib
import sys
config = set(pathlib.Path(sys.argv[1]).read_text().splitlines())
required = {
    "CONFIG_PM_ENABLE=y", "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y",
    "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y",
    "CONFIG_BT_CTRL_MODEM_SLEEP=y", "CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1=y",
    "CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y",
    "CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y", "CONFIG_ESP_PHY_MAC_BB_PD=y",
    "CONFIG_BUTTON_PERIOD_TIME_MS=20", "CONFIG_BUTTON_DEBOUNCE_TICKS=1",
    "CONFIG_BUTTON_LONG_PRESS_TIME_MS=650",
}
missing = required - config
if missing:
    sys.exit("Power configuration missing: " + ", ".join(sorted(missing)))
print("Standby power configuration: PASS")
PYCONFIG
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
