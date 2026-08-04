#!/usr/bin/env bash
set -euo pipefail

TOOL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRINTFLOW_DIR="$(cd "${TOOL_DIR}/../.." && pwd)"
BUILD_DIR="${PRINTFLOW_DIR}/build/nocai_x33_debug"

cmake -S "${TOOL_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DDIRECT_PRINT_SDK_STRICT=ON
cmake --build "${BUILD_DIR}" --parallel

BINARY="${BUILD_DIR}/bin/nocai_x33_debug"
if [[ "${1:-}" == "--setcap" ]]; then
    sudo setcap cap_net_raw+ep "${BINARY}"
    getcap "${BINARY}"
fi

echo "Built ${BINARY}"
