#!/usr/bin/env bash
set -euo pipefail

TOOL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRINTFLOW_DIR="$(cd "${TOOL_DIR}/../.." && pwd)"

case "$(uname -m)" in
    x86_64|amd64) SDK_ARCH="x86_64" ;;
    aarch64|arm64) SDK_ARCH="arm64" ;;
    *)
        echo "Unsupported architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

BUILD_DIR="${PRINTFLOW_DIR}/build/nocai_x33_debug-${SDK_ARCH}"

cmake -S "${TOOL_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DDIRECT_PRINT_SDK_STRICT=ON
cmake --build "${BUILD_DIR}" --parallel

BINARY="${BUILD_DIR}/bin/nocai_x33_debug"
if [[ "${1:-}" == "--setcap" ]]; then
    sudo setcap cap_net_raw+ep "${BINARY}"
fi

file "${BINARY}"
file "${BUILD_DIR}/bin/libSYPrintAPIforPROII.so"
file "${BUILD_DIR}/bin/PrinterSocketDLL/linux/${SDK_ARCH/x86_64/x64}/PrinterSocket.so"
getcap "${BINARY}" || true
echo "Built ${BINARY}"
