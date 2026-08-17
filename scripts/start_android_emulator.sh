#!/bin/bash

set -euo pipefail

ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$(pwd)/.android-sdk}"
AVD_NAME="${AVD_NAME:-PrintFlow_Pixel_1080p_API35}"
EMULATOR_HEADLESS="${EMULATOR_HEADLESS:-0}"
EMULATOR_GPU_MODE="${EMULATOR_GPU_MODE:-host}"
EMULATOR_EXTRA_ARGS="${EMULATOR_EXTRA_ARGS:--no-metrics}"
EMULATOR_ALLOW_NO_KVM="${EMULATOR_ALLOW_NO_KVM:-0}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

prepend_if_dir() {
    if [[ -d "$1" ]]; then
        PATH="$1:${PATH}"
    fi
}

prepend_if_dir "${ANDROID_SDK_ROOT}/platform-tools"
prepend_if_dir "${ANDROID_SDK_ROOT}/emulator"
export PATH

command -v adb >/dev/null 2>&1 || fail "Missing command: adb"
command -v emulator >/dev/null 2>&1 || fail "Missing command: emulator"

if [[ "${EMULATOR_ALLOW_NO_KVM}" != "1" && ! -w /dev/kvm ]]; then
    fail "The x86_64 Android emulator requires writable /dev/kvm on this host. Enable KVM/VM acceleration or set EMULATOR_ALLOW_NO_KVM=1 to try anyway."
fi

if ! adb get-state >/dev/null 2>&1; then
    printf 'Starting emulator: %s\n' "${AVD_NAME}"
    emulator_args=(-avd "${AVD_NAME}" -gpu "${EMULATOR_GPU_MODE}" -netdelay none -netspeed full)
    if [[ "${EMULATOR_HEADLESS}" == "1" ]]; then
        emulator_args+=(-no-window -no-audio)
    fi
    if [[ -n "${EMULATOR_EXTRA_ARGS}" ]]; then
        # shellcheck disable=SC2206
        extra_args=(${EMULATOR_EXTRA_ARGS})
        emulator_args+=("${extra_args[@]}")
    fi
    nohup emulator "${emulator_args[@]}" </dev/null >/tmp/printflow-emulator.log 2>&1 &
    emulator_pid=$!
    sleep 4
    if ! kill -0 "${emulator_pid}" >/dev/null 2>&1; then
        printf 'Emulator exited early. Last log lines:\n' >&2
        tail -n 80 /tmp/printflow-emulator.log >&2 || true
        exit 1
    fi
fi

adb wait-for-device
printf 'Waiting for Android boot to complete...\n'
until adb shell getprop sys.boot_completed | grep -q 1; do
    sleep 2
done

printf 'Emulator is booted: %s\n' "${AVD_NAME}"
adb devices
