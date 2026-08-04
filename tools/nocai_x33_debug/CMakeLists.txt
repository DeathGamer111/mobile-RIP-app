cmake_minimum_required(VERSION 3.16)

project(nocai_x33_debug LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    message(FATAL_ERROR "nocai_x33_debug supports Linux x86-64 only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PRINTFLOW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT DIRECT_PRINT_SDK_ROOT AND NOT DIRECT_PRINT_SDK_ARCHIVE)
    file(GLOB X33_SDK_ARCHIVES CONFIGURE_DEPENDS
        "${PRINTFLOW_ROOT}/DemoForX64Linux*.zip"
        "${PRINTFLOW_ROOT}/DemoForX64Linux*.tar"
        "${PRINTFLOW_ROOT}/DemoForX64Linux*.tar.gz")
    list(SORT X33_SDK_ARCHIVES)
    list(LENGTH X33_SDK_ARCHIVES X33_SDK_ARCHIVE_COUNT)
    if(X33_SDK_ARCHIVE_COUNT GREATER 0)
        list(GET X33_SDK_ARCHIVES 0 X33_SDK_ARCHIVE)
        set(DIRECT_PRINT_SDK_ARCHIVE "${X33_SDK_ARCHIVE}" CACHE FILEPATH
            "Legacy X-33 SDK archive" FORCE)
    endif()
endif()

set(DIRECT_PRINT_SDK_STRICT ON CACHE BOOL
    "The standalone harness requires the x64 SDK" FORCE)

add_executable(nocai_x33_debug
    main.cpp
    elf_symbols.cpp
    elf_symbols.h
    sdk_types.h)
set_target_properties(nocai_x33_debug PROPERTIES
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_options(nocai_x33_debug PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(nocai_x33_debug PRIVATE dl pthread)

include("${PRINTFLOW_ROOT}/cmake/DirectPrintSdk.cmake")
printflow_configure_direct_print_sdk(nocai_x33_debug)
