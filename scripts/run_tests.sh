#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-tests}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

cmake_args=(
    -S "${REPO_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DBUILD_TESTING=ON
    -DRIP_EMBED_BLUE_NOISE_MASKS=OFF
)

if [[ -n "${RIP_THEME:-}" ]]; then
    cmake_args+=("-DRIP_THEME=${RIP_THEME}")
fi

if [[ -n "${RIP_THEME_FILE:-}" ]]; then
    cmake_args+=("-DRIP_THEME_FILE=${RIP_THEME_FILE}")
fi

printf '==> Configuring tests in %s\n' "${BUILD_DIR}"
cmake "${cmake_args[@]}"

printf '\n==> Building tests\n'
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

printf '\n==> Running CTest\n'
ctest --test-dir "${BUILD_DIR}" --output-on-failure --progress
