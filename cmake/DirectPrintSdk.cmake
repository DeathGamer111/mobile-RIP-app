include_guard(GLOBAL)
include(GNUInstallDirs)

set(DIRECT_PRINT_SDK_ROOT "$ENV{DIRECT_PRINT_SDK_ROOT}" CACHE PATH
    "Extracted vendor direct-print SDK root.")
set(DIRECT_PRINT_SDK_ARCHIVE "$ENV{DIRECT_PRINT_SDK_ARCHIVE}" CACHE FILEPATH
    "Vendor direct-print SDK zip/tar archive. Extracted into the build tree.")
option(DIRECT_PRINT_SDK_STRICT
    "Fail configuration when a requested direct-print SDK is unusable." OFF)

function(_printflow_normalize_sdk_arch raw_arch out_arch out_socket_arch)
    string(TOLOWER "${raw_arch}" arch)
    if(arch MATCHES "^(x86_64|amd64)$")
        set(normalized "x86_64")
        set(socket_arch "x64")
    elseif(arch MATCHES "^(aarch64|arm64|arm64-v8a)$")
        set(normalized "arm64")
        set(socket_arch "arm64")
    elseif(arch MATCHES "^(x86|i[3-6]86|armeabi-v7a)$")
        set(normalized "x86")
        set(socket_arch "x86")
    else()
        set(normalized "${arch}")
        set(socket_arch "${arch}")
    endif()

    set(${out_arch} "${normalized}" PARENT_SCOPE)
    set(${out_socket_arch} "${socket_arch}" PARENT_SCOPE)
endfunction()

function(_printflow_report_sdk_failure message_text)
    if(DIRECT_PRINT_SDK_STRICT)
        message(FATAL_ERROR "${message_text}")
    endif()
    message(WARNING "${message_text}")
endfunction()

function(_printflow_find_auto_sdk_archive target_arch out_archive)
    set(archive_candidates)
    if(target_arch STREQUAL "x86_64")
        set(archive_patterns
            "${PROJECT_SOURCE_DIR}/DemoForX64Linux*.zip"
            "${PROJECT_SOURCE_DIR}/DemoForX64Linux*.tar"
            "${PROJECT_SOURCE_DIR}/DemoForX64Linux*.tar.gz")
    elseif(target_arch STREQUAL "arm64")
        set(archive_patterns
            "${PROJECT_SOURCE_DIR}/DemoForARM64Linux*.zip"
            "${PROJECT_SOURCE_DIR}/DemoForARM64Linux*.tar"
            "${PROJECT_SOURCE_DIR}/DemoForARM64Linux*.tar.gz")
    endif()
    if(archive_patterns)
        if(CMAKE_SCRIPT_MODE_FILE)
            file(GLOB archive_candidates ${archive_patterns})
        else()
            file(GLOB archive_candidates CONFIGURE_DEPENDS ${archive_patterns})
        endif()
    endif()

    list(SORT archive_candidates)
    list(LENGTH archive_candidates archive_count)
    if(archive_count GREATER 1)
        _printflow_report_sdk_failure(
            "Multiple direct-print SDK archives match ${target_arch}: ${archive_candidates}")
    endif()
    if(archive_count GREATER 0)
        list(GET archive_candidates 0 selected_archive)
        set(${out_archive} "${selected_archive}" PARENT_SCOPE)
    else()
        set(${out_archive} "" PARENT_SCOPE)
    endif()
endfunction()

function(_printflow_find_single_file out_var description)
    set(matches)
    foreach(pattern IN LISTS ARGN)
        file(GLOB_RECURSE pattern_matches CONFIGURE_DEPENDS "${pattern}")
        list(APPEND matches ${pattern_matches})
    endforeach()
    list(REMOVE_DUPLICATES matches)
    list(SORT matches)

    list(LENGTH matches match_count)
    if(match_count GREATER 1)
        _printflow_report_sdk_failure(
            "Multiple ${description} candidates found: ${matches}")
    endif()
    if(match_count GREATER 0)
        list(GET matches 0 selected)
        set(${out_var} "${selected}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(_printflow_validate_elf_arch library_path expected_arch out_valid)
    file(READ "${library_path}" elf_magic OFFSET 0 LIMIT 4 HEX)
    file(READ "${library_path}" elf_machine OFFSET 18 LIMIT 2 HEX)
    string(TOLOWER "${elf_magic}" elf_magic)
    string(TOLOWER "${elf_machine}" elf_machine)

    if(NOT elf_magic STREQUAL "7f454c46")
        message(WARNING "Direct-print SDK file is not ELF: ${library_path}")
        set(${out_valid} FALSE PARENT_SCOPE)
        return()
    endif()

    if(expected_arch STREQUAL "x86_64")
        set(expected_machine "3e00")
    elseif(expected_arch STREQUAL "arm64")
        set(expected_machine "b700")
    elseif(expected_arch STREQUAL "x86")
        set(expected_machine "0300")
    else()
        message(WARNING "Cannot validate direct-print ELF architecture '${expected_arch}'.")
        set(${out_valid} TRUE PARENT_SCOPE)
        return()
    endif()

    if(NOT elf_machine STREQUAL expected_machine)
        message(WARNING
            "Direct-print SDK architecture mismatch: ${library_path} has ELF machine "
            "${elf_machine}, expected ${expected_machine} for ${expected_arch}.")
        set(${out_valid} FALSE PARENT_SCOPE)
        return()
    endif()

    set(${out_valid} TRUE PARENT_SCOPE)
endfunction()

function(printflow_configure_direct_print_sdk target)
    if(ANDROID)
        set(target_platform "android")
        if(CMAKE_ANDROID_ARCH_ABI)
            set(raw_arch "${CMAKE_ANDROID_ARCH_ABI}")
        elseif(QT_ANDROID_ABIS)
            list(GET QT_ANDROID_ABIS 0 raw_arch)
        else()
            set(raw_arch "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(target_platform "linux")
        set(raw_arch "${CMAKE_SYSTEM_PROCESSOR}")
    else()
        message(STATUS "Direct-print SDK staging is unsupported on ${CMAKE_SYSTEM_NAME}.")
        return()
    endif()

    _printflow_normalize_sdk_arch("${raw_arch}" target_arch socket_arch)
    message(STATUS "Direct-print SDK target: ${target_platform}/${target_arch}")

    set(sdk_root "${DIRECT_PRINT_SDK_ROOT}")
    set(sdk_archive "${DIRECT_PRINT_SDK_ARCHIVE}")
    if(sdk_root AND sdk_archive)
        message(FATAL_ERROR
            "Set only one of DIRECT_PRINT_SDK_ROOT or DIRECT_PRINT_SDK_ARCHIVE.")
    endif()

    if(NOT sdk_root AND NOT sdk_archive AND target_platform STREQUAL "linux")
        _printflow_find_auto_sdk_archive("${target_arch}" sdk_archive)
        if(sdk_archive)
            message(STATUS "Auto-detected direct-print SDK archive: ${sdk_archive}")
        endif()
    endif()

    if(sdk_archive)
        if(NOT EXISTS "${sdk_archive}")
            _printflow_report_sdk_failure(
                "DIRECT_PRINT_SDK_ARCHIVE does not exist: ${sdk_archive}")
            return()
        endif()

        set(extract_root "${CMAKE_CURRENT_BINARY_DIR}/direct-print-sdk/source")
        file(REMOVE_RECURSE "${extract_root}")
        file(MAKE_DIRECTORY "${extract_root}")
        file(ARCHIVE_EXTRACT INPUT "${sdk_archive}" DESTINATION "${extract_root}")
        set(sdk_root "${extract_root}")
    endif()

    if(NOT sdk_root)
        if(DIRECT_PRINT_SDK_STRICT)
            message(FATAL_ERROR
                "No ${target_platform}/${target_arch} direct-print SDK archive or root was supplied.")
        endif()
        message(STATUS "No direct-print SDK supplied; direct print will remain unavailable.")
        return()
    endif()
    if(NOT IS_DIRECTORY "${sdk_root}")
        _printflow_report_sdk_failure(
            "DIRECT_PRINT_SDK_ROOT is not a directory: ${sdk_root}")
        return()
    endif()

    if(target_platform STREQUAL "android")
        _printflow_find_single_file(sdk_api "Android ${target_arch} print API"
            "${sdk_root}/android/${target_arch}/libSYPrintAPIforPROII.so"
            "${sdk_root}/*/android/${target_arch}/libSYPrintAPIforPROII.so"
            "${sdk_root}/android/${raw_arch}/libSYPrintAPIforPROII.so"
            "${sdk_root}/*/android/${raw_arch}/libSYPrintAPIforPROII.so")
        _printflow_find_single_file(sdk_socket "Android ${target_arch} printer socket"
            "${sdk_root}/PrinterSocketDLL/android/${socket_arch}/libPrinterSocket.so"
            "${sdk_root}/*/PrinterSocketDLL/android/${socket_arch}/libPrinterSocket.so")
    else()
        if(target_arch STREQUAL "x86_64")
            _printflow_find_single_file(sdk_api "Linux x86-64 print API"
                "${sdk_root}/libSYPrintAPIforPROII.so"
                "${sdk_root}/linux/x64/libSYPrintAPIforPROII.so"
                "${sdk_root}/x64/libSYPrintAPIforPROII.so"
                "${sdk_root}/libSYPrintAPIforPROII-x86_64.so")
        elseif(target_arch STREQUAL "arm64")
            _printflow_find_single_file(sdk_api "Linux ARM64 print API"
                "${sdk_root}/libSYPrintAPIforPROII.so")
        else()
            _printflow_find_single_file(sdk_api "Linux ${target_arch} print API"
                "${sdk_root}/libSYPrintAPIforPROII.so"
                "${sdk_root}/linux/${socket_arch}/libSYPrintAPIforPROII.so")
        endif()
        _printflow_find_single_file(sdk_socket "Linux ${target_arch} printer socket"
            "${sdk_root}/PrinterSocketDLL/linux/${socket_arch}/PrinterSocket.so"
            "${sdk_root}/*/PrinterSocketDLL/linux/${socket_arch}/PrinterSocket.so")
    endif()

    if(NOT sdk_api)
        set(message_text
            "No ${target_platform}/${target_arch} libSYPrintAPIforPROII.so was found under ${sdk_root}.")
        _printflow_report_sdk_failure(
            "${message_text} Direct print will remain unavailable.")
        return()
    endif()

    _printflow_validate_elf_arch("${sdk_api}" "${target_arch}" sdk_api_matches_target)
    if(NOT sdk_api_matches_target)
        _printflow_report_sdk_failure(
            "Direct-print SDK API does not match target architecture; ignoring it.")
        return()
    endif()
    if(sdk_socket)
        _printflow_validate_elf_arch("${sdk_socket}" "${target_arch}" sdk_socket_matches_target)
        if(NOT sdk_socket_matches_target)
            _printflow_report_sdk_failure(
                "Direct-print socket does not match target architecture; ignoring it.")
            set(sdk_socket "")
        endif()
    endif()

    if(NOT sdk_socket AND DIRECT_PRINT_SDK_STRICT)
        message(FATAL_ERROR
            "PrinterSocket library was not found for ${target_platform}/${target_arch}.")
    endif()

    if(ANDROID)
        set(extra_libs "${sdk_api}")
        if(sdk_socket)
            list(APPEND extra_libs "${sdk_socket}")
        endif()
        set_property(TARGET "${target}" APPEND PROPERTY QT_ANDROID_EXTRA_LIBS "${extra_libs}")
        message(STATUS "Android direct-print SDK libraries: ${extra_libs}")
        return()
    endif()

    set(runtime_root "$<TARGET_FILE_DIR:${target}>")
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${sdk_api}" "${runtime_root}/libSYPrintAPIforPROII.so"
        COMMENT "Staging ${target_platform}/${target_arch} direct-print API")

    if(sdk_socket)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${runtime_root}/PrinterSocketDLL/linux/${socket_arch}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${sdk_socket}"
                "${runtime_root}/PrinterSocketDLL/linux/${socket_arch}/PrinterSocket.so"
            COMMENT "Staging ${target_platform}/${target_arch} printer socket")
    else()
        message(WARNING
            "PrinterSocket.so was not found for ${target_platform}/${target_arch}; "
            "the print API may load but printer discovery will fail.")
    endif()

    install(FILES "${sdk_api}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME "libSYPrintAPIforPROII.so")
    if(sdk_socket)
        install(FILES "${sdk_socket}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/PrinterSocketDLL/linux/${socket_arch}")
    endif()

    target_compile_definitions("${target}" PRIVATE
        RIP_DIRECT_PRINT_SDK_BUNDLED=1
        RIP_DIRECT_PRINT_SDK_ARCH="${target_arch}")
    get_filename_component(sdk_api_root "${sdk_api}" DIRECTORY)
    set_property(TARGET "${target}" PROPERTY
        PRINTFLOW_DIRECT_PRINT_SDK_ROOT "${sdk_api_root}")
    message(STATUS "Direct-print SDK API: ${sdk_api}")
    message(STATUS "Direct-print socket: ${sdk_socket}")
endfunction()
