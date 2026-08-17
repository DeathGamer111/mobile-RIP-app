#!/bin/bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRIDGE_PORT="${PRINTFLOW_ANDROID_BRIDGE_PORT:-19733}"
SERVICE_PATH="${PRINTFLOW_PRINTER_SERVICE_EXECUTABLE:-${REPO_ROOT}/build/PrintFlowPrinterService}"
LOG_PATH="${PRINTFLOW_ANDROID_BRIDGE_LOG:-${REPO_ROOT}/build/android-printer-bridge.log}"

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

[[ "${BRIDGE_PORT}" =~ ^[0-9]+$ ]] || fail "Bridge port must be numeric."
(( BRIDGE_PORT > 0 && BRIDGE_PORT <= 65535 )) || fail "Bridge port must be between 1 and 65535."
[[ -x "${SERVICE_PATH}" ]] || fail "Printer service not found: ${SERVICE_PATH}. Build the Linux app first."

if [[ -f "${REPO_ROOT}/.android-env" ]]; then
    # shellcheck disable=SC1091
    source "${REPO_ROOT}/.android-env"
fi
command -v adb >/dev/null 2>&1 || fail "adb was not found. Source .android-env or install Android platform-tools."

adb_args=()
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    adb_args=(-s "${ANDROID_SERIAL}")
else
    mapfile -t connected_devices < <(adb devices | awk 'NR > 1 && $2 == "device" {print $1}')
    (( ${#connected_devices[@]} == 1 )) || fail "Attach exactly one Android target or set ANDROID_SERIAL."
    adb_args=(-s "${connected_devices[0]}")
fi

adb "${adb_args[@]}" wait-for-device
adb "${adb_args[@]}" reverse "tcp:${BRIDGE_PORT}" "tcp:${BRIDGE_PORT}"

# Stop only the PrintFlow service reachable through its protected local socket.
"${SERVICE_PATH}" --shutdown >/dev/null 2>&1 || true
for _ in {1..30}; do
    "${SERVICE_PATH}" --ping >/dev/null 2>&1 || break
    sleep 0.1
done

mkdir -p "$(dirname "${LOG_PATH}")"
nohup "${SERVICE_PATH}" --android-bridge-port "${BRIDGE_PORT}" \
    >>"${LOG_PATH}" 2>&1 &
bridge_pid=$!

for _ in {1..50}; do
    if "${SERVICE_PATH}" --ping >/dev/null 2>&1; then
        printf 'Android printer bridge ready.\n'
        printf '  Device:  %s\n' "${adb_args[1]}"
        printf '  Tunnel:  Android 127.0.0.1:%s -> Linux 127.0.0.1:%s\n' "${BRIDGE_PORT}" "${BRIDGE_PORT}"
        printf '  Service: %s (PID %s)\n' "${SERVICE_PATH}" "${bridge_pid}"
        printf '  Log:     %s\n' "${LOG_PATH}"
        if command -v getcap >/dev/null 2>&1 &&
           ! getcap "${SERVICE_PATH}" | grep -q 'cap_net_raw=ep'; then
            printf 'warning: CAP_NET_RAW is not set; ping works, but vendor discovery may fail.\n' >&2
            printf '         Re-run the Linux development build to apply it.\n' >&2
        fi
        exit 0
    fi
    kill -0 "${bridge_pid}" 2>/dev/null || fail "Printer bridge exited; inspect ${LOG_PATH}."
    sleep 0.1
done

fail "Printer bridge did not become ready; inspect ${LOG_PATH}."
