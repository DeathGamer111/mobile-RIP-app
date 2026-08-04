#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mode=""
pass_args=()
theme_from_cli=""
theme_file_from_cli=""

usage() {
    cat <<EOF
Usage: ./Dev_Build_App.sh [mode]

Modes:
  --linux                 Build/run the Linux desktop app
  --test                  Configure, build, and run the local CTest suite
  --android-setup         Install or repair Android SDK, NDK, emulator, and Qt Android kits
  --android               Build, install, and launch the Android APK on the emulator
  --android-device        Build, install, and launch the Android APK on a USB device

Theme options:
  --theme default|nocai|xante
  --theme-file /path/to/custom-theme.json

Environment:
  RIP_THEME=default|nocai|xante
  RIP_THEME_FILE=/path/to/custom-theme.json

Legacy aliases:
  --android-build         Build the Android APK only
  --android-run           Same as --android
  --android-device-run    Same as --android-device
  --android-setup-build   Install Android dependencies, then build the APK
  --setup-android-deps    Same as --android-setup-build
  --skip-android-deps     Same as --linux
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --theme=*)
            theme_from_cli="${arg#*=}"
            ;;
        --theme-file=*)
            theme_file_from_cli="${arg#*=}"
            ;;
        --linux)
            mode="linux"
            ;;
        --test)
            mode="test"
            ;;
        --android-setup)
            mode="android-setup"
            ;;
        --android)
            mode="android"
            ;;
        --android-device|--android-device-run)
            mode="android-device"
            ;;
        --android-build)
            mode="android-build"
            ;;
        --android-run)
            mode="android"
            ;;
        --android-setup-build|--setup-android-deps)
            mode="android-setup-build"
            ;;
        --skip-android-deps)
            mode="linux"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --theme|--theme-file)
            printf '%s requires a value. Use --theme=xante or --theme-file=/path/to/theme.json.\n' "${arg}" >&2
            exit 1
            ;;
        *)
            pass_args+=("${arg}")
            ;;
    esac
done

choose_mode() {
    if [[ ! -t 0 ]]; then
        printf 'No build mode selected in noninteractive mode; defaulting to Linux. Use --android for Android.\n'
        mode="linux"
        return
    fi

    cat <<EOF
Select PrintFlow build target:
  1) Linux desktop build
  2) Local test suite
  3) Android setup / repair environment
  4) Android build + install + run on emulator
  5) Android build + install + run on USB device
  q) Quit
EOF
    printf 'Choice [1]: '
    read -r reply

    case "${reply:-1}" in
        1) mode="linux" ;;
        2) mode="test" ;;
        3) mode="android-setup" ;;
        4) mode="android" ;;
        5) mode="android-device" ;;
        q|Q) exit 0 ;;
        *) printf 'Unknown choice: %s\n' "${reply}" >&2; exit 1 ;;
    esac
}

[[ -n "${mode}" ]] || choose_mode

needs_theme_selection() {
    case "$1" in
        linux|android|android-device|android-build|android-setup-build) return 0 ;;
        *) return 1 ;;
    esac
}

validate_builtin_theme() {
    case "$1" in
        default|nocai|xante) return 0 ;;
        *) return 1 ;;
    esac
}

choose_theme() {
    if [[ -n "${theme_from_cli}" && -n "${theme_file_from_cli}" ]]; then
        printf 'Use either --theme or --theme-file, not both.\n' >&2
        exit 1
    fi

    if [[ -n "${theme_from_cli}" ]]; then
        validate_builtin_theme "${theme_from_cli}" || {
            printf 'Unknown theme: %s. Expected default, nocai, or xante.\n' "${theme_from_cli}" >&2
            exit 1
        }
        export RIP_THEME="${theme_from_cli}"
        unset RIP_THEME_FILE
        return
    fi

    if [[ -n "${theme_file_from_cli}" ]]; then
        [[ -f "${theme_file_from_cli}" ]] || {
            printf 'Theme file does not exist: %s\n' "${theme_file_from_cli}" >&2
            exit 1
        }
        export RIP_THEME="default"
        export RIP_THEME_FILE="${theme_file_from_cli}"
        return
    fi

    if [[ -n "${RIP_THEME_FILE:-}" ]]; then
        [[ -f "${RIP_THEME_FILE}" ]] || {
            printf 'RIP_THEME_FILE does not exist: %s\n' "${RIP_THEME_FILE}" >&2
            exit 1
        }
        export RIP_THEME="${RIP_THEME:-default}"
        export RIP_THEME_FILE
        return
    fi

    if [[ -n "${RIP_THEME:-}" ]]; then
        validate_builtin_theme "${RIP_THEME}" || {
            printf 'Unknown RIP_THEME: %s. Expected default, nocai, or xante.\n' "${RIP_THEME}" >&2
            exit 1
        }
        export RIP_THEME
        return
    fi

    if [[ ! -t 0 ]]; then
        export RIP_THEME="default"
        return
    fi

    cat <<EOF
Select PrintFlow theme:
  1) Default / basic
  2) Nocai
  3) Xante / iQueue
  4) Custom theme JSON file
  q) Quit
EOF
    printf 'Choice [1]: '
    read -r reply

    case "${reply:-1}" in
        1) export RIP_THEME="default" ;;
        2) export RIP_THEME="nocai" ;;
        3) export RIP_THEME="xante" ;;
        4)
            printf 'Custom theme JSON path: '
            read -r custom_theme_file
            [[ -f "${custom_theme_file}" ]] || {
                printf 'Theme file does not exist: %s\n' "${custom_theme_file}" >&2
                exit 1
            }
            export RIP_THEME="default"
            export RIP_THEME_FILE="${custom_theme_file}"
            ;;
        q|Q) exit 0 ;;
        *) printf 'Unknown choice: %s\n' "${reply}" >&2; exit 1 ;;
    esac
}

if needs_theme_selection "${mode}"; then
    choose_theme
fi

case "${mode}" in
    linux)
        exec "${SCRIPT_DIR}/scripts/dev_build_linux.sh" "${pass_args[@]}"
        ;;
    test)
        exec "${SCRIPT_DIR}/scripts/run_tests.sh" "${pass_args[@]}"
        ;;
    android-setup)
        exec "${SCRIPT_DIR}/scripts/install_android_dependencies.sh"
        ;;
    android-build)
        exec "${SCRIPT_DIR}/scripts/dev_build_android.sh" "${pass_args[@]}"
        ;;
    android)
        exec "${SCRIPT_DIR}/scripts/android_build_install_run.sh" "${pass_args[@]}"
        ;;
    android-device)
        ANDROID_TARGET=device exec "${SCRIPT_DIR}/scripts/android_build_install_run.sh" "${pass_args[@]}"
        ;;
    android-setup-build)
        PRINTFLOW_ANDROID_SETUP_ASSUME_YES=1 "${SCRIPT_DIR}/scripts/install_android_dependencies.sh"
        exec "${SCRIPT_DIR}/scripts/dev_build_android.sh" "${pass_args[@]}"
        ;;
    *)
        printf 'Unknown mode: %s\n' "${mode}" >&2
        usage >&2
        exit 1
        ;;
esac
