cmake_minimum_required(VERSION 3.16)

project(nocai_x33_debug LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "nocai_x33_debug supports Linux only.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" NOCAI_HOST_PROCESSOR)
if(NOCAI_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(NOCAI_SDK_ARCH "x86_64")
    set(NOCAI_SDK_ARCHIVE_PREFIX "DemoForX64Linux")
elseif(NOCAI_HOST_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(NOCAI_SDK_ARCH "arm64")
    set(NOCAI_SDK_ARCHIVE_PREFIX "DemoForARM64Linux")
else()
    message(FATAL_ERROR
        "nocai_x33_debug supports Linux x86-64 and ARM64; got ${CMAKE_SYSTEM_PROCESSOR}.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PRINTFLOW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT DIRECT_PRINT_SDK_ROOT AND NOT DIRECT_PRINT_SDK_ARCHIVE AND
   "$ENV{DIRECT_PRINT_SDK_ROOT}" STREQUAL "" AND
   "$ENV{DIRECT_PRINT_SDK_ARCHIVE}" STREQUAL "")
    file(GLOB X33_SDK_ARCHIVES CONFIGURE_DEPENDS
        "${PRINTFLOW_ROOT}/${NOCAI_SDK_ARCHIVE_PREFIX}*.zip"
        "${PRINTFLOW_ROOT}/${NOCAI_SDK_ARCHIVE_PREFIX}*.tar"
        "${PRINTFLOW_ROOT}/${NOCAI_SDK_ARCHIVE_PREFIX}*.tar.gz")
    list(SORT X33_SDK_ARCHIVES)
    list(LENGTH X33_SDK_ARCHIVES X33_SDK_ARCHIVE_COUNT)
    if(X33_SDK_ARCHIVE_COUNT GREATER 1)
        message(FATAL_ERROR
            "Multiple ${NOCAI_SDK_ARCH} SDK archives found: ${X33_SDK_ARCHIVES}")
    elseif(X33_SDK_ARCHIVE_COUNT EQUAL 1)
        list(GET X33_SDK_ARCHIVES 0 X33_SDK_ARCHIVE)
        set(DIRECT_PRINT_SDK_ARCHIVE "${X33_SDK_ARCHIVE}" CACHE FILEPATH
            "Legacy X-33 SDK archive" FORCE)
    endif()
endif()

set(DIRECT_PRINT_SDK_STRICT ON CACHE BOOL
    "The standalone harness requires the native SDK" FORCE)

message(STATUS "nocai_x33_debug target architecture: ${NOCAI_SDK_ARCH}")

add_executable(nocai_x33_debug
    main.cpp
    elf_symbols.cpp
    elf_symbols.h
    sdk_types.h
    "${PRINTFLOW_ROOT}/src/vendor/nocai/NocaiArmStaleSessionRecovery.cpp"
    "${PRINTFLOW_ROOT}/src/vendor/nocai/NocaiArmStaleSessionRecovery.h")
set_target_properties(nocai_x33_debug PROPERTIES
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_options(nocai_x33_debug PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_include_directories(nocai_x33_debug PRIVATE
    "${PRINTFLOW_ROOT}/src/vendor/nocai")
target_link_libraries(nocai_x33_debug PRIVATE dl pthread)

include("${PRINTFLOW_ROOT}/cmake/DirectPrintSdk.cmake")
printflow_configure_direct_print_sdk(nocai_x33_debug)
