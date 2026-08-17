#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DESTINATION="${REPO_ROOT}/third_party/imagemagick/android"
CACHE_DIR="${IMAGEMAGICK_ANDROID_CACHE_DIR:-${REPO_ROOT}/build-android-imagemagick-downloads}"
RELEASE="7.1.2-29"
BASE_URL="https://github.com/MolotovCherry/Android-ImageMagick7/releases/download/${RELEASE}"

declare -A ARCHIVE_SHA256=(
    [x86_64]="2c3a972b389b3c5995562bc9a7cefb5cb808d2d7d5d1e5b3a5a323139084fce8"
    [arm64-v8a]="d8a0e9c15edae27496e42186a9308c9db07f406c83f47823c3675e6d5dbffc44"
)

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

command -v curl >/dev/null 2>&1 || fail "Missing command: curl"
command -v sha256sum >/dev/null 2>&1 || fail "Missing command: sha256sum"
command -v unzip >/dev/null 2>&1 || fail "Missing command: unzip"

[[ -f "${DESTINATION}/include/Magick++.h" ]] || \
    fail "Curated ImageMagick headers are missing from ${DESTINATION}/include"

mkdir -p "${CACHE_DIR}"

for abi in x86_64 arm64-v8a; do
    archive="${CACHE_DIR}/imagemagick-7-android-${abi}.zip"
    archive_url="${BASE_URL}/imagemagick-7-android-${abi}.zip"
    lib_dir="${DESTINATION}/lib/${abi}"

    if [[ ! -f "${archive}" ]] || \
       ! printf '%s  %s\n' "${ARCHIVE_SHA256[${abi}]}" "${archive}" | sha256sum --check --status; then
        printf 'Downloading ImageMagick %s for %s...\n' "${RELEASE}" "${abi}"
        curl --fail --location --retry 3 --output "${archive}" "${archive_url}"
    fi

    printf '%s  %s\n' "${ARCHIVE_SHA256[${abi}]}" "${archive}" | \
        sha256sum --check --status || fail "Checksum mismatch: ${archive}"

    mkdir -p "${lib_dir}"
    for library in libmagick++-7.so libmagickwand-7.so libmagickcore-7.so libomp.so; do
        unzip -p "${archive}" "shared/${library}" > "${lib_dir}/${library}"
        [[ -s "${lib_dir}/${library}" ]] || fail "Failed to extract ${library} for ${abi}"
    done
done

printf 'Android ImageMagick %s is ready under %s\n' "${RELEASE}" "${DESTINATION}"
