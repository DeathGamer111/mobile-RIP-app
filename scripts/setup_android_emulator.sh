#!/bin/bash

set -euo pipefail

ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$(pwd)/.android-sdk}"
ANDROID_CMDLINE_TOOLS_URL="${ANDROID_CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-linux-14742923_latest.zip}"
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-36}"
ANDROID_BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-36.0.0}"
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-29.0.13113456}"
ANDROID_SYSTEM_IMAGE="${ANDROID_SYSTEM_IMAGE:-system-images;${ANDROID_PLATFORM};google_apis;x86_64}"
ANDROID_AVD_NAME="${ANDROID_AVD_NAME:-PrintFlow_Pixel_1080p}"
ANDROID_AVD_DEVICE_PREFERENCES="${ANDROID_AVD_DEVICE_PREFERENCES:-pixel,pixel_4a,pixel_3a,Nexus 5}"
ANDROID_JDK_URL="${ANDROID_JDK_URL:-https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "Missing command: $1"
}

prepend_if_dir() {
    if [[ -d "$1" ]]; then
        PATH="$1:${PATH}"
    fi
}

install_cmdline_tools() {
    if [[ -x "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager" ]]; then
        return
    fi

    require_command curl
    require_command unzip

    local tmp_zip
    tmp_zip="$(mktemp --tmpdir android-commandlinetools-XXXXXX.zip)"

    mkdir -p "${ANDROID_SDK_ROOT}/cmdline-tools"
    printf 'Downloading Android command-line tools...\n'
    curl -L "${ANDROID_CMDLINE_TOOLS_URL}" -o "${tmp_zip}"

    rm -rf "${ANDROID_SDK_ROOT}/cmdline-tools/latest"
    unzip -q "${tmp_zip}" -d "${ANDROID_SDK_ROOT}/cmdline-tools"
    mv "${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools" "${ANDROID_SDK_ROOT}/cmdline-tools/latest"
    rm -f "${tmp_zip}"
}

ensure_java() {
    if command -v java >/dev/null 2>&1; then
        return
    fi

    require_command curl
    require_command tar

    if [[ ! -x "${ANDROID_SDK_ROOT}/jdk/bin/java" ]]; then
        local tmp_tgz tmp_dir extracted_dir
        tmp_tgz="$(mktemp --tmpdir android-jdk-XXXXXX.tar.gz)"
        tmp_dir="$(mktemp -d --tmpdir android-jdk-XXXXXX)"

        printf 'Downloading local JDK for Android SDK tools...\n'
        curl -L "${ANDROID_JDK_URL}" -o "${tmp_tgz}"
        tar -xzf "${tmp_tgz}" -C "${tmp_dir}"

        extracted_dir="$(find "${tmp_dir}" -mindepth 1 -maxdepth 1 -type d -print -quit)"
        [[ -n "${extracted_dir}" ]] || fail "Downloaded JDK archive did not contain a top-level directory"

        rm -rf "${ANDROID_SDK_ROOT}/jdk"
        mv "${extracted_dir}" "${ANDROID_SDK_ROOT}/jdk"
        rm -rf "${tmp_tgz}" "${tmp_dir}"
    fi

    export JAVA_HOME="${ANDROID_SDK_ROOT}/jdk"
    PATH="${JAVA_HOME}/bin:${PATH}"
}

sdkmanager_bin() {
    printf '%s/cmdline-tools/latest/bin/sdkmanager' "${ANDROID_SDK_ROOT}"
}

avdmanager_bin() {
    printf '%s/cmdline-tools/latest/bin/avdmanager' "${ANDROID_SDK_ROOT}"
}

choose_device_id() {
    local devices preferred
    devices="$("$(avdmanager_bin)" list device)"

    IFS=',' read -ra preferred <<< "${ANDROID_AVD_DEVICE_PREFERENCES}"
    for id in "${preferred[@]}"; do
        if grep -q "id: .* or \"${id}\"" <<< "${devices}"; then
            printf '%s' "${id}"
            return
        fi
    done

    printf 'pixel'
}

create_avd_if_missing() {
    if "$(avdmanager_bin)" list avd | grep -q "Name: ${ANDROID_AVD_NAME}$"; then
        printf 'AVD already exists: %s\n' "${ANDROID_AVD_NAME}"
        return
    fi

    local device_id
    device_id="$(choose_device_id)"
    printf 'Creating AVD %s with device %s and image %s...\n' "${ANDROID_AVD_NAME}" "${device_id}" "${ANDROID_SYSTEM_IMAGE}"
    printf 'no\n' | "$(avdmanager_bin)" create avd \
        --name "${ANDROID_AVD_NAME}" \
        --package "${ANDROID_SYSTEM_IMAGE}" \
        --device "${device_id}" \
        --force
}

mkdir -p "${ANDROID_SDK_ROOT}"
install_cmdline_tools

prepend_if_dir "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin"
prepend_if_dir "${ANDROID_SDK_ROOT}/platform-tools"
prepend_if_dir "${ANDROID_SDK_ROOT}/emulator"

ensure_java
require_command java

set +o pipefail
yes | "$(sdkmanager_bin)" --sdk_root="${ANDROID_SDK_ROOT}" --licenses >/dev/null
set -o pipefail
"$(sdkmanager_bin)" --sdk_root="${ANDROID_SDK_ROOT}" \
    "platform-tools" \
    "emulator" \
    "platforms;${ANDROID_PLATFORM}" \
    "build-tools;${ANDROID_BUILD_TOOLS}" \
    "ndk;${ANDROID_NDK_VERSION}" \
    "${ANDROID_SYSTEM_IMAGE}"

create_avd_if_missing

cat <<EOF

Android emulator setup complete.

export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT}"
export ANDROID_NDK_ROOT="${ANDROID_SDK_ROOT}/ndk/${ANDROID_NDK_VERSION}"
export PATH="${ANDROID_SDK_ROOT}/platform-tools:${ANDROID_SDK_ROOT}/emulator:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:\$PATH"

AVD name: ${ANDROID_AVD_NAME}
System image: ${ANDROID_SYSTEM_IMAGE}
EOF
