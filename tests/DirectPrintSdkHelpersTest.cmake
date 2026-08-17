cmake_minimum_required(VERSION 3.16)

if(NOT PRINTFLOW_SOURCE_DIR OR NOT TEST_BINARY_DIR OR NOT TEST_MODE OR NOT TEST_HOST_PROCESSOR)
    message(FATAL_ERROR
        "PRINTFLOW_SOURCE_DIR, TEST_BINARY_DIR, TEST_MODE, and TEST_HOST_PROCESSOR are required.")
endif()

set(PROJECT_SOURCE_DIR "${TEST_BINARY_DIR}/source")
set(CMAKE_INSTALL_BINDIR "bin")
set(CMAKE_INSTALL_LIBDIR "lib")
file(REMOVE_RECURSE "${PROJECT_SOURCE_DIR}")
file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}")
include("${PRINTFLOW_SOURCE_DIR}/cmake/DirectPrintSdk.cmake")

function(assert_equal actual expected description)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${description}: expected '${expected}', received '${actual}'.")
    endif()
endfunction()

if(TEST_MODE STREQUAL "helpers")
    set(DIRECT_PRINT_SDK_STRICT OFF CACHE BOOL "" FORCE)

    _printflow_normalize_sdk_arch("x86_64" normalized socket)
    assert_equal("${normalized}" "x86_64" "x86-64 normalization")
    assert_equal("${socket}" "x64" "x86-64 socket folder")

    _printflow_normalize_sdk_arch("aarch64" normalized socket)
    assert_equal("${normalized}" "arm64" "ARM64 normalization")
    assert_equal("${socket}" "arm64" "ARM64 socket folder")

    _printflow_sdk_paths("/sdk" "linux" "arm64" api socket)
    assert_equal("${api}" "/sdk/linux/arm64/libSYPrintAPIforPROII.so"
        "ARM64 API path")
    assert_equal("${socket}" "/sdk/linux/arm64/PrinterSocket.so"
        "ARM64 socket path")

    _printflow_sdk_paths("/sdk" "linux" "x86_64" api socket)
    assert_equal("${api}" "/sdk/linux/x86_64/libSYPrintAPIforPROII.so"
        "x86-64 API path")
    assert_equal("${socket}" "/sdk/linux/x86_64/PrinterSocket.so"
        "x86-64 socket path")

    foreach(sdk_arch IN ITEMS arm64 x86_64)
        _printflow_sdk_paths("${PRINTFLOW_SOURCE_DIR}/third_party/nocai/direct-print"
            "linux" "${sdk_arch}" api socket)
        foreach(library IN ITEMS "${api}" "${socket}")
            if(NOT EXISTS "${library}")
                message(FATAL_ERROR "Bundled ${sdk_arch} SDK file is missing: ${library}")
            endif()
            _printflow_validate_elf_arch("${library}" "${sdk_arch}" valid_sdk_elf)
            if(NOT valid_sdk_elf)
                message(FATAL_ERROR
                    "Bundled ${sdk_arch} SDK file failed ELF validation: ${library}")
            endif()
        endforeach()
    endforeach()

    _printflow_validate_android_elf(
        "${PRINTFLOW_SOURCE_DIR}/third_party/nocai/direct-print/linux/arm64/libSYPrintAPIforPROII.so"
        linux_arm_is_android)
    if(linux_arm_is_android)
        message(FATAL_ERROR "The Linux ARM64 SDK was accepted as an Android library.")
    endif()

    _printflow_normalize_sdk_arch("${TEST_HOST_PROCESSOR}" host_arch unused_socket)
    _printflow_validate_elf_arch("${CMAKE_COMMAND}" "${host_arch}" valid_host_elf)
    if(NOT valid_host_elf)
        message(FATAL_ERROR "The native CMake executable failed ELF validation.")
    endif()
    if(host_arch STREQUAL "arm64")
        set(opposite_arch "x86_64")
    else()
        set(opposite_arch "arm64")
    endif()
    _printflow_validate_elf_arch("${CMAKE_COMMAND}" "${opposite_arch}" valid_mismatch)
    if(valid_mismatch)
        message(FATAL_ERROR "A deliberate cross-architecture ELF mismatch was accepted.")
    endif()
elseif(TEST_MODE STREQUAL "strict-missing")
    set(DIRECT_PRINT_SDK_STRICT ON CACHE BOOL "" FORCE)
    _printflow_report_sdk_failure("Strict mode rejected a missing direct-print SDK.")
elseif(TEST_MODE STREQUAL "strict-incomplete")
    set(DIRECT_PRINT_SDK_STRICT ON CACHE BOOL "" FORCE)
    _printflow_sdk_paths("${PROJECT_SOURCE_DIR}" "linux" "arm64" api socket)
    file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/linux/arm64")
    file(WRITE "${api}" "incomplete fixture")
    if(NOT EXISTS "${api}" OR NOT EXISTS "${socket}")
        _printflow_report_sdk_failure("Strict mode rejected an incomplete SDK pair.")
    endif()
else()
    message(FATAL_ERROR "Unknown TEST_MODE: ${TEST_MODE}")
endif()
