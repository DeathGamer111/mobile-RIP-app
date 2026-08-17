include_guard(GLOBAL)
include(GNUInstallDirs)

set(_printflow_bundled_sdk_root
    "${PROJECT_SOURCE_DIR}/third_party/nocai/direct-print")
if(DEFINED ENV{DIRECT_PRINT_SDK_ROOT} AND
   NOT "$ENV{DIRECT_PRINT_SDK_ROOT}" STREQUAL "")
    set(_printflow_bundled_sdk_root "$ENV{DIRECT_PRINT_SDK_ROOT}")
endif()
set(DIRECT_PRINT_SDK_ROOT "${_printflow_bundled_sdk_root}" CACHE PATH
    "Direct-print SDK root using the PrintFlow architecture layout.")
unset(_printflow_bundled_sdk_root)

option(DIRECT_PRINT_SDK_STRICT
    "Fail configuration when the target direct-print SDK is unusable." OFF)
if(ANDROID)
    set(_printflow_direct_print_sdk_default OFF)
else()
    set(_printflow_direct_print_sdk_default ON)
endif()
option(DIRECT_PRINT_SDK_ENABLED
    "Stage the proprietary direct-print SDK for the target platform."
    ${_printflow_direct_print_sdk_default})
unset(_printflow_direct_print_sdk_default)

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

# Source SDK files use one predictable platform/architecture layout. The
# vendor's PrinterSocketDLL folder name is recreated only in the staged runtime
# because the proprietary API searches for it there.
function(_printflow_sdk_paths sdk_root target_platform target_arch out_api out_socket)
    set(architecture_root "${sdk_root}/${target_platform}/${target_arch}")
    set(${out_api}
        "${architecture_root}/libSYPrintAPIforPROII.so" PARENT_SCOPE)
    if(target_platform STREQUAL "android")
        set(socket_name "libPrinterSocket.so")
    else()
        set(socket_name "PrinterSocket.so")
    endif()
    set(${out_socket} "${architecture_root}/${socket_name}" PARENT_SCOPE)
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

# A Linux/glibc shared object can have the correct ARM64 machine type while
# still being unloadable on Android. Reject the characteristic glibc runtime
# dependencies before Gradle packages a vendor binary into an APK.
function(_printflow_validate_android_elf library_path out_valid)
    file(STRINGS "${library_path}" runtime_names
        REGEX "^(ld-linux.*\\.so(\\.[0-9]+)*|libc\\.so\\.6|libstdc\\+\\+\\.so\\.6|libgcc_s\\.so\\.1)$")
    if(runtime_names)
        list(REMOVE_DUPLICATES runtime_names)
        message(WARNING
            "Direct-print SDK file uses Linux/glibc runtime libraries and is not "
            "Android/Bionic compatible: ${library_path} (${runtime_names})")
        set(${out_valid} FALSE PARENT_SCOPE)
        return()
    endif()
    set(${out_valid} TRUE PARENT_SCOPE)
endfunction()

function(printflow_configure_direct_print_sdk target)
    if(NOT DIRECT_PRINT_SDK_ENABLED)
        message(STATUS "Direct-print SDK staging is disabled for this target.")
        return()
    endif()

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

    if(NOT IS_DIRECTORY "${DIRECT_PRINT_SDK_ROOT}")
        _printflow_report_sdk_failure(
            "Direct-print SDK root does not exist: ${DIRECT_PRINT_SDK_ROOT}")
        return()
    endif()

    _printflow_sdk_paths("${DIRECT_PRINT_SDK_ROOT}" "${target_platform}"
        "${target_arch}" sdk_api sdk_socket)
    if(NOT EXISTS "${sdk_api}" OR NOT EXISTS "${sdk_socket}")
        _printflow_report_sdk_failure(
            "The ${target_platform}/${target_arch} direct-print SDK pair is incomplete under "
            "${DIRECT_PRINT_SDK_ROOT}. Expected ${sdk_api} and ${sdk_socket}.")
        return()
    endif()

    _printflow_validate_elf_arch("${sdk_api}" "${target_arch}" sdk_api_matches_target)
    _printflow_validate_elf_arch("${sdk_socket}" "${target_arch}" sdk_socket_matches_target)
    if(NOT sdk_api_matches_target OR NOT sdk_socket_matches_target)
        _printflow_report_sdk_failure(
            "The direct-print SDK pair does not match target architecture ${target_arch}.")
        return()
    endif()

    if(ANDROID)
        _printflow_validate_android_elf("${sdk_api}" sdk_api_matches_android)
        _printflow_validate_android_elf("${sdk_socket}" sdk_socket_matches_android)
        if(NOT sdk_api_matches_android OR NOT sdk_socket_matches_android)
            _printflow_report_sdk_failure(
                "The direct-print SDK pair is Linux/glibc, not Android/Bionic.")
            return()
        endif()
    endif()

    if(ANDROID)
        set_property(TARGET "${target}" APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "${sdk_api}" "${sdk_socket}")
        message(STATUS "Android direct-print SDK libraries: ${sdk_api};${sdk_socket}")
        return()
    endif()

    set(runtime_root "$<TARGET_FILE_DIR:${target}>")
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${sdk_api}" "${runtime_root}/libSYPrintAPIforPROII.so"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${runtime_root}/PrinterSocketDLL/linux/${socket_arch}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${sdk_socket}"
            "${runtime_root}/PrinterSocketDLL/linux/${socket_arch}/PrinterSocket.so"
        COMMENT "Staging ${target_platform}/${target_arch} direct-print SDK")

    install(FILES "${sdk_api}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME "libSYPrintAPIforPROII.so")
    install(FILES "${sdk_socket}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/PrinterSocketDLL/linux/${socket_arch}")

    target_compile_definitions("${target}" PRIVATE
        RIP_DIRECT_PRINT_SDK_BUNDLED=1
        RIP_DIRECT_PRINT_SDK_ARCH="${target_arch}")
    get_filename_component(sdk_api_root "${sdk_api}" DIRECTORY)
    set_property(TARGET "${target}" PROPERTY
        PRINTFLOW_DIRECT_PRINT_SDK_ROOT "${sdk_api_root}")
    message(STATUS "Direct-print SDK API: ${sdk_api}")
    message(STATUS "Direct-print socket: ${sdk_socket}")
endfunction()
