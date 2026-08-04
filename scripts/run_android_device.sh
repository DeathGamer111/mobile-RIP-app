#!/bin/bash

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-android-device}"
PACKAGE_NAME="${PACKAGE_NAME:-com.ripapp.printer}"
ACTIVITY_NAME="${ACTIVITY_NAME:-org.qtproject.qt.android.bindings.QtActivity}"
STREAM_LOGCAT="${STREAM_LOGCAT:-0}"

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
export PATH

command -v adb >/dev/null 2>&1 || fail "Missing command: adb"

APK_PATH="${APK_PATH:-}"
if [[ -z "${APK_PATH}" ]]; then
    APK_PATH="$(find "${BUILD_DIR}/android-build/build/outputs/apk" -type f -name '*.apk' 2>/dev/null | sort | tail -n 1 || true)"
fi

[[ -n "${APK_PATH}" && -f "${APK_PATH}" ]] || fail "APK not found. Run scripts/dev_build_android.sh first or set APK_PATH."

adb_args=()
target_label="USB device"
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    adb_args=(-s "${ANDROID_SERIAL}")
    target_label="${ANDROID_SERIAL}"
else
    adb_args=(-d)
fi

printf 'Waiting for Android device: %s\n' "${target_label}"
adb "${adb_args[@]}" wait-for-device

boot_completed="$(adb "${adb_args[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)"
if [[ "${boot_completed}" != "1" ]]; then
    printf 'Waiting for Android framework to finish booting...\n'
    until [[ "$(adb "${adb_args[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)" == "1" ]]; do
        sleep 2
    done
fi

printf 'Installing APK on %s...\n' "${target_label}"
adb "${adb_args[@]}" install -r "${APK_PATH}"
adb "${adb_args[@]}" shell am start -n "${PACKAGE_NAME}/${ACTIVITY_NAME}"

if [[ "${STREAM_LOGCAT}" == "1" ]]; then
    printf '\nApp launched. Streaming filtered logcat; press Ctrl+C to stop.\n'
    adb "${adb_args[@]}" logcat | grep --line-buffered -E 'RIP|Qt|libSYPrintAPIforPROII|Nocai|AndroidRuntime'
else
    printf '\nApp launched on %s. Set STREAM_LOGCAT=1 to keep a filtered logcat stream open.\n' "${target_label}"
fi
