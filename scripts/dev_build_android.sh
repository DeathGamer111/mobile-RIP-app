#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build-android}"
ANDROID_ABI="${ANDROID_ABI:-x86_64}"
ANDROID_ENV_FILE="${ANDROID_ENV_FILE:-.android-env}"
GRADLE_USER_HOME="${GRADLE_USER_HOME:-$(pwd)/.gradle}"
RIP_THEME="${RIP_THEME:-default}"
RIP_THEME_FILE="${RIP_THEME_FILE:-}"
RIP_ANDROID_ENABLE_RIP_PROCESSING="${RIP_ANDROID_ENABLE_RIP_PROCESSING:-ON}"
RIP_EMBED_BLUE_NOISE_MASKS="${RIP_EMBED_BLUE_NOISE_MASKS:-OFF}"
GRADLE_OPTS="${GRADLE_OPTS:-} -Djava.net.preferIPv4Stack=true -Dorg.gradle.daemon=false -Dorg.gradle.vfs.watch=false"
export GRADLE_USER_HOME
export GRADLE_OPTS

CALLER_ANDROID_ABI="${ANDROID_ABI}"
CALLER_BUILD_DIR="${BUILD_DIR}"
CALLER_QT_ANDROID_CMAKE="${QT_ANDROID_CMAKE:-}"
CALLER_QT_HOST_PATH="${QT_HOST_PATH:-}"
CALLER_ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-}"
CALLER_ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-}"
CALLER_DIRECT_PRINT_SDK_ROOT="${DIRECT_PRINT_SDK_ROOT:-}"

if [[ -f "${ANDROID_ENV_FILE}" ]]; then
    # shellcheck disable=SC1090
    source "${ANDROID_ENV_FILE}"
fi

ANDROID_ABI="${CALLER_ANDROID_ABI}"
BUILD_DIR="${CALLER_BUILD_DIR}"
[[ -n "${CALLER_QT_ANDROID_CMAKE}" ]] && QT_ANDROID_CMAKE="${CALLER_QT_ANDROID_CMAKE}"
[[ -n "${CALLER_QT_HOST_PATH}" ]] && QT_HOST_PATH="${CALLER_QT_HOST_PATH}"
[[ -n "${CALLER_ANDROID_SDK_ROOT}" ]] && ANDROID_SDK_ROOT="${CALLER_ANDROID_SDK_ROOT}"
[[ -n "${CALLER_ANDROID_NDK_ROOT}" ]] && ANDROID_NDK_ROOT="${CALLER_ANDROID_NDK_ROOT}"
[[ -n "${CALLER_DIRECT_PRINT_SDK_ROOT}" ]] && DIRECT_PRINT_SDK_ROOT="${CALLER_DIRECT_PRINT_SDK_ROOT}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "Missing command: $1"
}

require_path() {
    [[ -e "$2" ]] || fail "$1 does not exist: $2"
}

theme_cmake_args() {
    printf -- '-DRIP_THEME=%s\n' "${RIP_THEME}"
    if [[ -n "${RIP_THEME_FILE}" ]]; then
        [[ -f "${RIP_THEME_FILE}" ]] || fail "RIP_THEME_FILE does not exist: ${RIP_THEME_FILE}"
        printf -- '-DRIP_THEME_FILE=%s\n' "${RIP_THEME_FILE}"
    fi
}

prepend_if_dir() {
    if [[ -d "$1" ]]; then
        PATH="$1:${PATH}"
    fi
}

infer_qt_host_path() {
    local qt_bin qt_target_dir qt_version_dir candidate

    qt_bin="$(dirname "${QT_ANDROID_CMAKE}")"
    qt_target_dir="$(dirname "${qt_bin}")"
    qt_version_dir="$(dirname "${qt_target_dir}")"

    for candidate in \
        "${qt_version_dir}/gcc_64" \
        "${qt_version_dir}/linux_gcc_64"
    do
        if [[ -x "${candidate}/bin/qt-cmake" ]]; then
            printf '%s' "${candidate}"
            return
        fi
    done
}

qt_target_for_abi() {
    case "$1" in
        x86_64) printf 'android_x86_64' ;;
        arm64-v8a) printf 'android_arm64_v8a' ;;
        *) return 1 ;;
    esac
}

adjust_qt_android_cmake_for_abi() {
    local target qt_bin qt_target_dir qt_version_dir candidate

    target="$(qt_target_for_abi "${ANDROID_ABI}" || true)"
    [[ -n "${target}" && -n "${QT_ANDROID_CMAKE:-}" ]] || return 0
    [[ "${QT_ANDROID_CMAKE}" != *"/${target}/"* ]] || return 0

    qt_bin="$(dirname "${QT_ANDROID_CMAKE}")"
    qt_target_dir="$(dirname "${qt_bin}")"
    qt_version_dir="$(dirname "${qt_target_dir}")"
    candidate="${qt_version_dir}/${target}/bin/qt-cmake"

    if [[ -x "${candidate}" ]]; then
        printf 'Switching Qt Android kit for ABI %s: %s\n' "${ANDROID_ABI}" "${candidate}"
        QT_ANDROID_CMAKE="${candidate}"
        export QT_ANDROID_CMAKE
    fi
}

adjust_qt_android_cmake_for_abi

[[ -n "${QT_ANDROID_CMAKE:-}" ]] || fail "Set QT_ANDROID_CMAKE to the Qt Android qt-cmake path, for example ~/Qt/6.x.x/android_arm64_v8a/bin/qt-cmake"
require_path "QT_ANDROID_CMAKE" "${QT_ANDROID_CMAKE}"
if [[ -z "${QT_HOST_PATH:-}" ]]; then
    QT_HOST_PATH="$(infer_qt_host_path || true)"
    export QT_HOST_PATH
fi
[[ -n "${QT_HOST_PATH:-}" ]] || fail "Set QT_HOST_PATH to the matching desktop Qt host path, for example ~/Qt/6.x.x/gcc_64"
require_path "QT_HOST_PATH" "${QT_HOST_PATH}"
[[ -n "${ANDROID_SDK_ROOT:-}" ]] || fail "Set ANDROID_SDK_ROOT to your Android SDK path"
require_path "ANDROID_SDK_ROOT" "${ANDROID_SDK_ROOT}"
[[ -n "${ANDROID_NDK_ROOT:-}" ]] || fail "Set ANDROID_NDK_ROOT to your Android NDK path"
require_path "ANDROID_NDK_ROOT" "${ANDROID_NDK_ROOT}"

prepend_if_dir "${ANDROID_SDK_ROOT}/platform-tools"
prepend_if_dir "${ANDROID_SDK_ROOT}/emulator"
prepend_if_dir "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin"
prepend_if_dir "${ANDROID_NDK_ROOT}/prebuilt/linux-x86_64/bin"
if [[ -z "${JAVA_HOME:-}" && -x "${ANDROID_SDK_ROOT}/jdk/bin/java" ]]; then
    export JAVA_HOME="${ANDROID_SDK_ROOT}/jdk"
fi
prepend_if_dir "${JAVA_HOME:-}/bin"

require_command java
require_command ninja
require_command adb
require_command emulator
mkdir -p "${GRADLE_USER_HOME}"

SDK_CMAKE_ARGS=(-DDIRECT_PRINT_SDK_ENABLED=OFF)
if [[ -n "${DIRECT_PRINT_SDK_ROOT:-}" ]]; then
    require_path "DIRECT_PRINT_SDK_ROOT" "${DIRECT_PRINT_SDK_ROOT}"
    "${REPO_ROOT}/scripts/validate_android_direct_print_sdk.sh" \
        "${DIRECT_PRINT_SDK_ROOT}" "${ANDROID_ABI}"
    SDK_CMAKE_ARGS=(
        -DDIRECT_PRINT_SDK_ENABLED=ON
        -DDIRECT_PRINT_SDK_STRICT=ON
        -DDIRECT_PRINT_SDK_ROOT="${DIRECT_PRINT_SDK_ROOT}"
    )
else
    printf 'note: native Android SDK staging is disabled; the temporary ADB printer bridge remains available.\n' >&2
fi

mapfile -t THEME_CMAKE_ARGS < <(theme_cmake_args)
printf 'Theme: %s%s\n' "${RIP_THEME}" "${RIP_THEME_FILE:+ from ${RIP_THEME_FILE}}"

RIP_CMAKE_ARGS=(-DRIP_ANDROID_ENABLE_RIP_PROCESSING="${RIP_ANDROID_ENABLE_RIP_PROCESSING}")
if [[ "${RIP_ANDROID_ENABLE_RIP_PROCESSING}" == "ON" ]]; then
    "${REPO_ROOT}/scripts/setup_android_imagemagick.sh"
fi
RIP_CMAKE_ARGS+=( -DRIP_EMBED_BLUE_NOISE_MASKS="${RIP_EMBED_BLUE_NOISE_MASKS}" )

"${QT_ANDROID_CMAKE}" \
    -S . \
    -B "${BUILD_DIR}" \
    -GNinja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_SDK_ROOT="${ANDROID_SDK_ROOT}" \
    -DANDROID_NDK_ROOT="${ANDROID_NDK_ROOT}" \
    -DQT_HOST_PATH="${QT_HOST_PATH}" \
    -DQT_ANDROID_ABIS="${ANDROID_ABI}" \
    "${RIP_CMAKE_ARGS[@]}" \
    "${SDK_CMAKE_ARGS[@]}" \
    "${THEME_CMAKE_ARGS[@]}"

cmake --build "${BUILD_DIR}" --target apk --parallel

printf '\nAPK build complete. Look under %s/android-build/build/outputs/apk/.\n' "${BUILD_DIR}"
