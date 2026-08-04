#!/bin/bash

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-android}"
AVD_NAME="${AVD_NAME:-PrintFlow_Pixel_1080p}"
PACKAGE_NAME="${PACKAGE_NAME:-com.ripapp.printer}"
ACTIVITY_NAME="${ACTIVITY_NAME:-org.qtproject.qt.android.bindings.QtActivity}"
STREAM_LOGCAT="${STREAM_LOGCAT:-0}"
EMULATOR_HEADLESS="${EMULATOR_HEADLESS:-0}"
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

ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$(pwd)/.android-sdk}"
prepend_if_dir "${ANDROID_SDK_ROOT}/platform-tools"
prepend_if_dir "${ANDROID_SDK_ROOT}/emulator"

command -v adb >/dev/null 2>&1 || fail "Missing command: adb"
command -v emulator >/dev/null 2>&1 || fail "Missing command: emulator"

if [[ "${EMULATOR_ALLOW_NO_KVM}" != "1" && ! -w /dev/kvm ]]; then
    fail "The x86_64 Android emulator requires writable /dev/kvm on this host. Enable KVM/VM acceleration or set EMULATOR_ALLOW_NO_KVM=1 to try anyway."
fi

APK_PATH="${APK_PATH:-}"
if [[ -z "${APK_PATH}" ]]; then
    APK_PATH="$(find "${BUILD_DIR}/android-build/build/outputs/apk" -type f -name '*.apk' 2>/dev/null | sort | tail -n 1 || true)"
fi

[[ -n "${APK_PATH}" && -f "${APK_PATH}" ]] || fail "APK not found. Run scripts/dev_build_android.sh first or set APK_PATH."

if ! adb get-state >/dev/null 2>&1; then
    printf 'Starting emulator: %s\n' "${AVD_NAME}"
    emulator_args=(-avd "${AVD_NAME}" -netdelay none -netspeed full)
    if [[ "${EMULATOR_HEADLESS}" == "1" ]]; then
        emulator_args+=(-no-window -no-audio)
    fi
    if [[ -n "${EMULATOR_EXTRA_ARGS}" ]]; then
        # shellcheck disable=SC2206
        extra_args=(${EMULATOR_EXTRA_ARGS})
        emulator_args+=("${extra_args[@]}")
    fi
    emulator "${emulator_args[@]}" >/tmp/printflow-emulator.log 2>&1 &
    emulator_pid=$!
    sleep 4
    if ! kill -0 "${emulator_pid}" >/dev/null 2>&1; then
        printf 'Emulator exited early. Last log lines:\n' >&2
        tail -n 80 /tmp/printflow-emulator.log >&2 || true
        exit 1
    fi
fi

adb wait-for-device
adb shell getprop sys.boot_completed | grep -q 1 || {
    printf 'Waiting for Android boot to complete...\n'
    until adb shell getprop sys.boot_completed | grep -q 1; do
        sleep 2
    done
}

adb install -r "${APK_PATH}"
adb shell am start -n "${PACKAGE_NAME}/${ACTIVITY_NAME}"

if [[ "${STREAM_LOGCAT}" == "1" ]]; then
    printf '\nApp launched. Streaming filtered logcat; press Ctrl+C to stop.\n'
    adb logcat | grep --line-buffered -E 'RIP|Qt|libSYPrintAPIforPROII|Nocai|AndroidRuntime'
else
    printf '\nApp launched on %s. Set STREAM_LOGCAT=1 to keep a filtered logcat stream open.\n' "${AVD_NAME}"
fi
