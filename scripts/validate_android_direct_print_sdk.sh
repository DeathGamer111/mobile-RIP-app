#!/usr/bin/env bash

set -euo pipefail

SDK_ROOT="${1:-${DIRECT_PRINT_SDK_ROOT:-}}"
REQUESTED_ABI="${2:-${ANDROID_ABI:-arm64-v8a}}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

for command_name in file readelf nm; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        fail "Missing command: ${command_name}"
done

[[ -n "${SDK_ROOT}" ]] || fail "Pass the SDK root or set DIRECT_PRINT_SDK_ROOT."

case "${REQUESTED_ABI}" in
    arm64-v8a|arm64|aarch64)
        SDK_ARCH="arm64"
        EXPECTED_MACHINE="AArch64"
        ;;
    x86_64|amd64)
        SDK_ARCH="x86_64"
        EXPECTED_MACHINE="Advanced Micro Devices X86-64"
        ;;
    *)
        fail "Unsupported Android ABI: ${REQUESTED_ABI}"
        ;;
esac

SDK_DIR="${SDK_ROOT}/android/${SDK_ARCH}"
SDK_API="${SDK_DIR}/libSYPrintAPIforPROII.so"
SDK_SOCKET="${SDK_DIR}/libPrinterSocket.so"

[[ -f "${SDK_API}" ]] || fail "Missing Android print API: ${SDK_API}"
[[ -f "${SDK_SOCKET}" ]] || fail "Missing Android printer socket: ${SDK_SOCKET}"

verify_android_elf() {
    local library_path="$1"
    local description="$2"
    local machine linux_dependencies

    machine="$(readelf -h "${library_path}" | awk -F: '/Machine:/ { value=$2; sub(/^[[:space:]]+/, "", value); print value; exit }')"
    [[ "${machine}" == "${EXPECTED_MACHINE}" ]] ||
        fail "${description} has ELF machine '${machine}', expected '${EXPECTED_MACHINE}'."

    linux_dependencies="$(
        readelf -d "${library_path}" |
            sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' |
            grep -E '^(ld-linux.*\.so(\.[0-9]+)*|libc\.so\.6|libstdc\+\+\.so\.6|libgcc_s\.so\.1)$' || true
    )"
    [[ -z "${linux_dependencies}" ]] ||
        fail "${description} is Linux/glibc, not Android/Bionic; dependencies: ${linux_dependencies//$'\n'/, }."

    printf 'Verified %-24s %s\n' "${description}:" "$(file -b "${library_path}")"
}

verify_android_elf "${SDK_API}" "Android print API"
verify_android_elf "${SDK_SOCKET}" "Android printer socket"

mapfile -t EXPORTED_SYMBOLS < <(nm -D --defined-only "${SDK_API}" | awk '{print $NF}')

has_symbol() {
    local candidate exported
    for candidate in "$@"; do
        for exported in "${EXPORTED_SYMBOLS[@]}"; do
            [[ "${exported}" == "${candidate}" ]] && return 0
        done
    done
    return 1
}

require_symbol() {
    local display_name="$1"
    shift
    has_symbol "$@" || fail "Android print API is missing required function: ${display_name}"
}

require_symbol SearchPrinter SearchPrinter _Z17API_SearchPrinterP15PrinterInfoListi
require_symbol ChoosePrinter ChoosePrinter _Z17API_SelectPrinteri
require_symbol ContinuePrint ContinuePrint _Z17API_ContinuePrintv
require_symbol InitPrinter InitPrinter _Z15API_InitPrinterv
require_symbol StartPrint StartPrint _Z14API_StartPrintP19tagPrintJobProperty
require_symbol PrintALine PrintALine _Z14API_PrintALinePcj
require_symbol AbortPrint AbortPrint _Z14API_AbortPrintv
require_symbol PausePrint PausePrint _Z14API_PausePrintv
require_symbol EndPrint EndPrint _Z12API_EndPrintv
require_symbol ClosePrint ClosePrint _Z14API_ClosePrintv
require_symbol SetJobSettings SetJobSettings _Z18API_SetJobSettingsP13stJobSettingsi

if has_symbol PrintAlignmentPattern _Z25API_PrintAlignmentPattern22eAlignmentPatternTypes; then
    printf 'Verified nozzle/alignment pattern entry point.\n'
else
    printf 'warning: SDK has no PrintAlignmentPattern entry point; nozzle-test probing will be unavailable.\n' >&2
fi

printf 'Android direct-print SDK is ready for %s packaging: %s\n' \
    "${SDK_ARCH}" "${SDK_DIR}"
