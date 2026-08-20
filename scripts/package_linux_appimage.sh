#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

APP_NAME="PrintFlow"
BUILD_DIR="${BUILD_DIR:-build-package}"
APPDIR="${APPDIR:-${APP_NAME}.AppDir}"
OUTPUT_DIR="${OUTPUT_DIR:-output}"
TOOLS_ROOT="${TOOLS_ROOT:-${REPO_ROOT}/.tools/appimage}"
RIP_THEME="${RIP_THEME:-default}"
RIP_THEME_FILE="${RIP_THEME_FILE:-}"
DIRECT_PRINT_SDK_ROOT="${DIRECT_PRINT_SDK_ROOT:-${REPO_ROOT}/third_party/nocai/direct-print}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "Missing command: $1"
}

apt_package_available() {
    local candidate
    candidate="$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ { print $2; exit }')"
    [[ -n "${candidate}" && "${candidate}" != "(none)" ]]
}

theme_cmake_args() {
    printf -- '-DRIP_THEME=%s\n' "${RIP_THEME}"
    if [[ -n "${RIP_THEME_FILE}" ]]; then
        [[ -f "${RIP_THEME_FILE}" ]] || fail "RIP_THEME_FILE does not exist: ${RIP_THEME_FILE}"
        printf -- '-DRIP_THEME_FILE=%s\n' "${RIP_THEME_FILE}"
    fi
}

download_tool() {
    local destination="$1"
    local url="$2"
    if [[ ! -x "${destination}" ]]; then
        wget -q --show-progress -O "${destination}" "${url}"
        chmod +x "${destination}"
    fi
}

elf_machine() {
    readelf -h "$1" | awk -F: '/Machine:/ { value=$2; sub(/^[[:space:]]+/, "", value); print value; exit }'
}

verify_elf_arch() {
    local file_path="$1"
    local description="$2"
    local actual_machine

    [[ -f "${file_path}" ]] || fail "${description} is missing: ${file_path}"
    actual_machine="$(elf_machine "${file_path}")"
    [[ "${actual_machine}" == "${EXPECTED_ELF_MACHINE}" ]] ||
        fail "${description} has ELF machine '${actual_machine}', expected '${EXPECTED_ELF_MACHINE}'."
    printf 'Verified %-30s %s\n' "${description}:" "${actual_machine}"
}

stage_application_icon() {
    local source_icon="$1"
    local output_icon="$2"
    local image_tool=()

    if command -v magick >/dev/null 2>&1; then
        image_tool=(magick)
    elif command -v convert >/dev/null 2>&1; then
        image_tool=(convert)
    else
        fail "ImageMagick is required to create the 256x256 application icon."
    fi

    "${image_tool[@]}" "${source_icon}" \
        -resize 256x256 \
        -background none \
        -gravity center \
        -extent 256x256 \
        "${output_icon}"
}

application_icon_source() {
    if [[ -n "${RIP_THEME_FILE}" ]]; then
        printf '%s\n' "resources/assets/logo.png"
        return
    fi
    case "${RIP_THEME}" in
        nocai|xante)
            printf '%s\n' "resources/themes/${RIP_THEME}/assets/logo.png"
            ;;
        *)
            printf '%s\n' "resources/assets/logo.png"
            ;;
    esac
}

case "$(uname -m)" in
    x86_64|amd64)
        DEPLOY_ARCH="x86_64"
        SDK_ARCH="x86_64"
        SOCKET_ARCH="x64"
        EXPECTED_ELF_MACHINE="Advanced Micro Devices X86-64"
        OPPOSITE_SOCKET_ARCH="arm64"
        ;;
    aarch64|arm64)
        DEPLOY_ARCH="aarch64"
        SDK_ARCH="arm64"
        SOCKET_ARCH="arm64"
        EXPECTED_ELF_MACHINE="AArch64"
        OPPOSITE_SOCKET_ARCH="x64"
        ;;
    *)
        fail "AppImage packaging supports only native x86_64 and aarch64 hosts; found $(uname -m)."
        ;;
esac

printf 'Packaging %s for %s (SDK architecture %s).\n' "${APP_NAME}" "${DEPLOY_ARCH}" "${SDK_ARCH}"
printf 'Checking Linux build dependencies. sudo may ask for your password.\n'
sudo apt-get update -qq

FUSE2_PACKAGE="libfuse2"
if apt_package_available libfuse2t64; then
    FUSE2_PACKAGE="libfuse2t64"
fi

IMAGEMAGICK_DEV_PACKAGE=""
for candidate in libmagick++-6.q16-dev libmagick++-7.q16-dev libmagick++-dev; do
    if apt_package_available "${candidate}"; then
        IMAGEMAGICK_DEV_PACKAGE="${candidate}"
        break
    fi
done
[[ -n "${IMAGEMAGICK_DEV_PACKAGE}" ]] || fail "No supported ImageMagick Magick++ development package is available."

QT6_SVG_PLUGIN_PACKAGES=()
if apt_package_available qt6-svg-plugins; then
    QT6_SVG_PLUGIN_PACKAGES+=(qt6-svg-plugins)
fi

sudo apt-get install -y --no-remove \
    cmake g++ qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
    qt6-declarative-dev-tools qt6-tools-dev qt6-tools-dev-tools \
    qt6-l10n-tools qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qt-labs-platform \
    qml6-module-qtquick-dialogs qt6-wayland pkg-config \
    liblcms2-dev libcups2-dev "${IMAGEMAGICK_DEV_PACKAGE}" libqt6quick6 \
    wget git patchelf desktop-file-utils libglib2.0-bin imagemagick \
    "${FUSE2_PACKAGE}" zsync xz-utils libgl1-mesa-dev libopengl-dev libvulkan-dev \
    qt6-qmltooling-plugins libqt6widgets6 qml6-module-qtpositioning \
    qml6-module-qtcore qml6-module-qtquick-window qml-module-qtquick-shapes \
    qml6-module-qtquick-shapes qt5-qmltooling-plugins qt6-image-formats-plugins \
    "${QT6_SVG_PLUGIN_PACKAGES[@]}" libqt6svg6 libqt6svgwidgets6 qml6-module-qtqml-workerscript \
    qml6-module-qtquick-templates libqt6test6 binutils

require_command cmake
require_command readelf
require_command qmake6

TOOL_DIR="${TOOLS_ROOT}/${DEPLOY_ARCH}"
mkdir -p "${TOOL_DIR}"
LINUXDEPLOY="${TOOL_DIR}/linuxdeploy-${DEPLOY_ARCH}.AppImage"
QT_PLUGIN="${TOOL_DIR}/linuxdeploy-plugin-qt-${DEPLOY_ARCH}.AppImage"
APPIMAGE_PLUGIN="${TOOL_DIR}/linuxdeploy-plugin-appimage-${DEPLOY_ARCH}.AppImage"

download_tool "${LINUXDEPLOY}" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${DEPLOY_ARCH}.AppImage"
download_tool "${QT_PLUGIN}" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${DEPLOY_ARCH}.AppImage"
download_tool "${APPIMAGE_PLUGIN}" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-${DEPLOY_ARCH}.AppImage"

[[ -n "${BUILD_DIR}" && "${BUILD_DIR}" != "/" ]] || fail "Unsafe BUILD_DIR: ${BUILD_DIR}"
[[ -n "${APPDIR}" && "${APPDIR}" != "/" ]] || fail "Unsafe APPDIR: ${APPDIR}"
[[ -n "${OUTPUT_DIR}" && "${OUTPUT_DIR}" != "/" ]] || fail "Unsafe OUTPUT_DIR: ${OUTPUT_DIR}"
rm -rf -- "${BUILD_DIR}" "${APPDIR}"
mkdir -p "${BUILD_DIR}" "${OUTPUT_DIR}"

mapfile -t THEME_CMAKE_ARGS < <(theme_cmake_args)
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DDIRECT_PRINT_SDK_ROOT="${DIRECT_PRINT_SDK_ROOT}" \
    -DDIRECT_PRINT_SDK_STRICT=ON \
    "${THEME_CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --target PrintFlow --parallel "$(nproc)"

mkdir -p "${APPDIR}/usr/share/applications" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
    "${APPDIR}/usr/share/${APP_NAME}/assets"
cmake --install "${BUILD_DIR}" --prefix "${APPDIR}/usr"
(
    cd "${BUILD_DIR}"
    cpack -G DEB -B "${REPO_ROOT}/${OUTPUT_DIR}"
)
cp resources/packaging/linux/printflow.desktop \
    "${APPDIR}/usr/share/applications/printflow.desktop"
stage_application_icon \
    "$(application_icon_source)" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps/printflow.png"

if [[ -d "resources/assets/blue_noise_mask_512_12000" ]]; then
    mkdir -p "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_512_12000"
    cp -r resources/assets/blue_noise_mask_512_12000/. \
        "${APPDIR}/usr/share/${APP_NAME}/assets/blue_noise_mask_512_12000/"
fi

APP_EXECUTABLE="${APPDIR}/usr/bin/${APP_NAME}"
SERVICE_EXECUTABLE="${APPDIR}/usr/bin/PrintFlowPrinterService"
SDK_API="${APPDIR}/usr/bin/libSYPrintAPIforPROII.so"
SDK_SOCKET="${APPDIR}/usr/bin/PrinterSocketDLL/linux/${SOCKET_ARCH}/PrinterSocket.so"
verify_elf_arch "${APP_EXECUTABLE}" "application executable"
verify_elf_arch "${SERVICE_EXECUTABLE}" "printer service executable"
verify_elf_arch "${SDK_API}" "direct-print API"
verify_elf_arch "${SDK_SOCKET}" "printer socket"
mapfile -t PRINTER_API_LIBRARIES < <(
    find "${APPDIR}/usr/${CMAKE_INSTALL_LIBDIR:-lib}" -maxdepth 1 -type f \
        -name 'libPrintFlowPrinterApi.so.*' -print
)
(( ${#PRINTER_API_LIBRARIES[@]} > 0 )) ||
    fail "The unified PrintFlow printer API library was not installed."
verify_elf_arch "${PRINTER_API_LIBRARIES[0]}" "unified printer API"
[[ ! -e "${APPDIR}/usr/bin/PrinterSocketDLL/linux/${OPPOSITE_SOCKET_ARCH}" ]] ||
    fail "Opposite-architecture printer socket was unexpectedly installed."

VERSION="${VERSION:-$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n 1)}"
[[ -n "${VERSION}" ]] || fail "Unable to determine the application version from CMake."
OUTPUT_PATH="${REPO_ROOT}/${OUTPUT_DIR}/${APP_NAME}-${VERSION}-${DEPLOY_ARCH}.AppImage"

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QMAKE:-$(command -v qmake6)}"
export QML_SOURCES_PATHS="${QML_SOURCES_PATHS:-${REPO_ROOT}/resources/qml}"
export EXTRA_QT_MODULES="${EXTRA_QT_MODULES:-svg}"
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so}"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export LDAI_OUTPUT="${OUTPUT_PATH}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"

"${LINUXDEPLOY}" \
    --appdir "${APPDIR}" \
    --executable "${APP_EXECUTABLE}" \
    --executable "${SERVICE_EXECUTABLE}" \
    --desktop-file "${APPDIR}/usr/share/applications/printflow.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/256x256/apps/printflow.png" \
    --plugin qt \
    --output appimage

[[ -f "${OUTPUT_PATH}" ]] || fail "AppImage output was not created: ${OUTPUT_PATH}"
chmod +x "${OUTPUT_PATH}"

printf 'AppImage complete: %s\n' "${OUTPUT_PATH}"
printf 'Native unified package (GUI, service, API, and SDK) is in: %s\n' \
    "${REPO_ROOT}/${OUTPUT_DIR}"
