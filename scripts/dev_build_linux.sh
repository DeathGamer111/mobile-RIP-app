#!/bin/bash

set -euo pipefail

TARGET_NAME="PrintFlow"
BUILD_DIR="${BUILD_DIR:-build}"
MASK_SOURCE_DIR="resources/assets/blue_noise_mask_512_12000"
RUNTIME_DIR="${HOME}/.local/share/PrintFlow/runtime_assets"
RIP_THEME="${RIP_THEME:-default}"
RIP_THEME_FILE="${RIP_THEME_FILE:-}"

STEP=0
TOTAL_STEPS=8

step() {
    STEP=$((STEP + 1))
    printf '\n[%2d/%2d] %s\n' "${STEP}" "${TOTAL_STEPS}" "$1"
}

info() {
    printf '       %s\n' "$1"
}

fail() {
    printf '\nerror: %s\n' "$1" >&2
    exit 1
}

find_direct_printer_interface() {
    if [[ -n "${PRINTFLOW_PRINTER_INTERFACE:-}" ]]; then
        [[ -d "/sys/class/net/${PRINTFLOW_PRINTER_INTERFACE}" ]] ||
            fail "PRINTFLOW_PRINTER_INTERFACE does not exist: ${PRINTFLOW_PRINTER_INTERFACE}"
        printf '%s\n' "${PRINTFLOW_PRINTER_INTERFACE}"
        return
    fi

    local interface
    local -a candidates=()
    for interface_path in /sys/class/net/*; do
        interface="${interface_path##*/}"
        [[ "${interface}" != "lo" ]] || continue
        [[ ! -d "${interface_path}/wireless" ]] || continue
        [[ "$(cat "${interface_path}/type" 2>/dev/null || true)" == "1" ]] || continue
        [[ "$(cat "${interface_path}/carrier" 2>/dev/null || true)" == "1" ]] || continue
        ip -4 -o address show dev "${interface}" | grep -q . && continue
        candidates+=("${interface}")
    done

    if (( ${#candidates[@]} == 1 )); then
        printf '%s\n' "${candidates[0]}"
    elif (( ${#candidates[@]} > 1 )); then
        printf '       Multiple unconfigured wired interfaces have carrier: %s\n' "${candidates[*]}" >&2
        printf '       Set PRINTFLOW_PRINTER_INTERFACE to select the printer connection.\n' >&2
    fi
}

configure_direct_printer_network() {
    if [[ "${PRINTFLOW_CONFIGURE_PRINTER_NETWORK:-1}" == "0" ]]; then
        info "Direct-printer network setup disabled by PRINTFLOW_CONFIGURE_PRINTER_NETWORK=0."
        return
    fi

    local interface
    interface="$(find_direct_printer_interface)"
    if [[ -z "${interface}" ]]; then
        info "No single unconfigured wired printer interface was detected; network setup skipped."
        return
    fi

    if ip -4 -o address show dev "${interface}" | grep -q .; then
        info "Printer interface ${interface} already has IPv4 configured."
        return
    fi

    local safe_interface="${interface//[^a-zA-Z0-9_.-]/-}"
    local profile="printflow-direct-printer-${safe_interface}"
    if sudo nmcli --terse --fields NAME connection show | grep -Fxq "${profile}"; then
        sudo nmcli connection modify "${profile}" \
            connection.interface-name "${interface}" \
            connection.autoconnect yes \
            ipv4.method link-local \
            ipv4.never-default yes \
            ipv4.ignore-auto-dns yes \
            ipv6.never-default yes \
            ipv6.method disabled
    else
        sudo nmcli connection add \
            type ethernet \
            ifname "${interface}" \
            con-name "${profile}" \
            connection.autoconnect yes \
            ipv4.method link-local \
            ipv4.never-default yes \
            ipv4.ignore-auto-dns yes \
            ipv6.never-default yes \
            ipv6.method disabled
    fi

    sudo nmcli connection up "${profile}" ifname "${interface}"
    info "Direct-printer interface ${interface} configured for IPv4 link-local networking."
}

theme_cmake_args() {
    printf -- '-DRIP_THEME=%s\n' "${RIP_THEME}"
    if [[ -n "${RIP_THEME_FILE}" ]]; then
        [[ -f "${RIP_THEME_FILE}" ]] || fail "RIP_THEME_FILE does not exist: ${RIP_THEME_FILE}"
        printf -- '-DRIP_THEME_FILE=%s\n' "${RIP_THEME_FILE}"
    fi
}

step "Checking Linux build dependencies"
info "sudo may ask for your password."
sudo apt-get update -qq

# Ubuntu 24.04 renamed the FUSE 2 runtime package to libfuse2t64. Installing
# the legacy `fuse` package removes fuse3, which in turn removes Ubuntu's
# desktop/session packages and prevents GDM from starting.
FUSE2_PACKAGE="libfuse2"
if apt-cache show libfuse2t64 2>/dev/null | grep -q '^Package: libfuse2t64$'; then
    FUSE2_PACKAGE="libfuse2t64"
fi

sudo apt-get install -y --no-remove \
    cmake g++ qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
    qt6-declarative-dev-tools qt6-tools-dev qt6-tools-dev-tools \
    qt6-l10n-tools qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qt-labs-platform \
    qml6-module-qtquick-dialogs qt6-wayland pkg-config \
    liblcms2-dev libcups2-dev libmagick++-6.q16-dev libqt6quick6 \
    wget git patchelf desktop-file-utils libglib2.0-bin \
    "${FUSE2_PACKAGE}" zsync xz-utils libgl1-mesa-dev libopengl-dev libvulkan-dev \
    qt6-declarative-dev-tools qt6-qmltooling-plugins \
    qml6-module-qtquick-dialogs libqt6widgets6 qml6-module-qtpositioning \
    qml6-module-qtcore qml6-module-qtquick-window qml-module-qtquick-shapes \
    qt5-qmltooling-plugins qt6-image-formats-plugins libqt6widgets6 \
    libqt6svg6 libqt6svgwidgets6 qml6-module-qtqml-workerscript \
    qml6-module-qtquick-templates libqt6test6 \
    libcap2-bin network-manager iproute2

step "Applying ImageMagick policy"
sudo bash ./scripts/Relax_ImageMagick_Limits.sh

step "Preparing build directories"
sudo rm -rf "${HOME}/.local/share/PrintFlow/"
sudo rm -rf "${HOME}/.cache/PrintFlow/"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
info "Build directory: ${BUILD_DIR}"

step "Configuring CMake"
mapfile -t THEME_CMAKE_ARGS < <(theme_cmake_args)
info "Theme: ${RIP_THEME}${RIP_THEME_FILE:+ from ${RIP_THEME_FILE}}"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug "${THEME_CMAKE_ARGS[@]}"

step "Building ${TARGET_NAME}"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

step "Installing runtime assets"
mkdir -p "${RUNTIME_DIR}"

if [[ -d "${MASK_SOURCE_DIR}" ]]; then
    cp -f "${MASK_SOURCE_DIR}/mask_c.tiff"   "${RUNTIME_DIR}/mask_512_c.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_m.tiff"   "${RUNTIME_DIR}/mask_512_m.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_y.tiff"   "${RUNTIME_DIR}/mask_512_y.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_k.tiff"   "${RUNTIME_DIR}/mask_512_k.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_lc.tiff"  "${RUNTIME_DIR}/mask_512_lc.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_lm.tiff"  "${RUNTIME_DIR}/mask_512_lm.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_lk.tiff"  "${RUNTIME_DIR}/mask_512_lk.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_llk.tiff" "${RUNTIME_DIR}/mask_512_llk.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_w.tiff"   "${RUNTIME_DIR}/mask_512_w.tiff"
    cp -f "${MASK_SOURCE_DIR}/mask_v.tiff"   "${RUNTIME_DIR}/mask_512_v.tiff"
    info "Runtime masks: ${RUNTIME_DIR}"
else
    info "Mask directory not found: ${MASK_SOURCE_DIR}"
fi

step "Configuring direct-printer access"
configure_direct_printer_network

EXECUTABLE_PATH="${BUILD_DIR}/${TARGET_NAME}"
[[ -x "${EXECUTABLE_PATH}" ]] || fail "Built executable was not found: ${EXECUTABLE_PATH}"
sudo setcap cap_net_raw+ep "${EXECUTABLE_PATH}"
info "Granted CAP_NET_RAW to ${EXECUTABLE_PATH} for SDK printer discovery."

step "Build complete"
info "Run: ./${BUILD_DIR}/${TARGET_NAME}"
info "A matching direct-print SDK, when configured, is staged beside the executable by CMake."
