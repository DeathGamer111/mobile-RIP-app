cmake_minimum_required(VERSION 3.16)

project(PrintFlow VERSION 0.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

option(RIP_ANDROID_ENABLE_RIP_PROCESSING "Build Android with the native RIP dependency stack instead of boot-safe stubs." OFF)
option(RIP_EMBED_BLUE_NOISE_MASKS "Embed local blue-noise TIFF masks into the Qt resource bundle." OFF)
set(RIP_THEME "default" CACHE STRING "Built-in theme id to embed as the selected theme: default, nocai, or xante.")
set(RIP_THEME_FILE "" CACHE FILEPATH "Optional custom theme JSON file to embed instead of a built-in selected theme.")
set_property(CACHE RIP_THEME PROPERTY STRINGS default nocai xante)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Concurrent Widgets QuickControls2 Test Xml Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(RIP_CORE_SOURCES
    src/app/main.cpp
    src/core/AppStrings.h src/core/AppStrings.cpp
    src/core/AssetManager.h src/core/AssetManager.cpp
    src/core/ColorManagementManager.h src/core/ColorManagementManager.cpp
    src/core/ImageImportManager.h src/core/ImageImportManager.cpp
    src/core/IPrintOutputClient.h
    src/core/PlatformCapabilities.h src/core/PlatformCapabilities.cpp
    src/core/PrintJob.h
    src/core/PrintJobModel.h src/core/PrintJobModel.cpp
    src/core/ThemeManager.h src/core/ThemeManager.cpp
    src/vendor/nocai/NocaiPrnWriter.h src/vendor/nocai/NocaiPrnWriter.cpp
)

set(PRINTFLOW_DIRECT_PRINT_BACKEND_SOURCES
    src/vendor/nocai/NocaiDirectPrintClient.h src/vendor/nocai/NocaiDirectPrintClient.cpp
    src/vendor/nocai/NocaiArmCommandTagCompatibility.h src/vendor/nocai/NocaiArmCommandTagCompatibility.cpp
    src/vendor/nocai/NocaiArmStaleSessionRecovery.h src/vendor/nocai/NocaiArmStaleSessionRecovery.cpp
    src/vendor/nocai/NocaiDirectPrintCompatibility.h src/vendor/nocai/NocaiDirectPrintCompatibility.cpp
    src/vendor/nocai/NocaiPrnWriter.h src/vendor/nocai/NocaiPrnWriter.cpp
)

if(ANDROID)
    list(APPEND RIP_CORE_SOURCES ${PRINTFLOW_DIRECT_PRINT_BACKEND_SOURCES})
endif()

set(RIP_NATIVE_SOURCES
    src/rip/ColorProfile.h src/rip/ColorProfile.cpp
    src/rip/ImageEditor.h src/rip/ImageEditor.cpp
    src/rip/ImageLoader.h src/rip/ImageLoader.cpp
    src/rip/MagickCompatibility.h
    src/rip/MultiInkLinearization.h src/rip/MultiInkLinearization.cpp
    src/rip/MultiInkScreenEngine.h src/rip/MultiInkScreenEngine.cpp
    src/rip/MultiInkToneBuilder.h src/rip/MultiInkToneBuilder.cpp
    src/rip/MultiInkTypes.h
    src/rip/RasterAlphaMask.h src/rip/RasterAlphaMask.cpp
    src/rip/X33WhiteToneBuilder.h src/rip/X33WhiteToneBuilder.cpp
    src/rip/PrintJobMultiInk.h src/rip/PrintJobMultiInk.cpp
    src/rip/PrintJobCMYK.h src/rip/PrintJobCMYK.cpp
)

set(RIP_ANDROID_STUB_SOURCES
    src/platform/android/ColorProfile.h src/platform/android/ColorProfile.cpp
    src/platform/android/ImageEditor.h src/platform/android/ImageEditor.cpp
    src/platform/android/ImageLoader.h src/platform/android/ImageLoader.cpp
    src/platform/android/PrintJobMultiInk.h src/platform/android/PrintJobMultiInk.cpp
    src/platform/android/PrintJobCMYK.h src/platform/android/PrintJobCMYK.cpp
)

if(ANDROID AND NOT RIP_ANDROID_ENABLE_RIP_PROCESSING)
    set(RIP_PROCESSING_SOURCES ${RIP_ANDROID_STUB_SOURCES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ImageMagick REQUIRED IMPORTED_TARGET Magick++)
    pkg_check_modules(LCMS REQUIRED IMPORTED_TARGET lcms2)
    set(RIP_PROCESSING_SOURCES ${RIP_NATIVE_SOURCES})
endif()

if(ANDROID)
    set(RIP_PLATFORM_SOURCES
        src/platform/android/PrintJobOutput.h src/platform/android/PrintJobOutput.cpp
    )
else()
    set(RIP_PLATFORM_SOURCES
        src/platform/desktop/PrintJobOutput.h src/platform/desktop/PrintJobOutput.cpp
    )
endif()

qt_add_executable(PrintFlow
    ${RIP_CORE_SOURCES}
    ${RIP_PROCESSING_SOURCES}
    ${RIP_PLATFORM_SOURCES}
    src/third_party/stb/stb_image.h
)

if(NOT ANDROID)
    add_library(PrintFlowPrinterApi SHARED
        src/printerservice/PrintFlowPrinterApiExport.h
        src/printerservice/PrinterServiceProtocol.h
        src/printerservice/PrinterServiceProtocol.cpp
        src/printerservice/PrinterServiceClient.h
        src/printerservice/PrinterServiceClient.cpp
        src/core/IPrintOutputClient.h
    )
    add_library(PrintFlow::PrinterApi ALIAS PrintFlowPrinterApi)
    set_target_properties(PrintFlowPrinterApi PROPERTIES
        VERSION 1.0.0
        SOVERSION 1
        OUTPUT_NAME PrintFlowPrinterApi
        EXPORT_NAME PrinterApi
    )
    target_compile_definitions(PrintFlowPrinterApi PRIVATE
        PRINTFLOW_PRINTER_API_LIBRARY=1
    )
    target_include_directories(PrintFlowPrinterApi
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/printerservice>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/PrintFlowPrinterApi>
    )
    target_link_libraries(PrintFlowPrinterApi
        PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network
    )
endif()

target_include_directories(PrintFlow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    src/app
    src/core
    src/third_party/stb
    src/vendor/nocai
    src/printerservice
)

if(ANDROID AND NOT RIP_ANDROID_ENABLE_RIP_PROCESSING)
    target_include_directories(PrintFlow PRIVATE src/platform/android)
else()
    target_include_directories(PrintFlow PRIVATE src/rip)
endif()

if(ANDROID)
    target_include_directories(PrintFlow PRIVATE src/platform/android)
    if(TARGET Qt6::CorePrivate)
        target_link_libraries(PrintFlow PRIVATE Qt6::CorePrivate)
    endif()
else()
    target_include_directories(PrintFlow PRIVATE src/platform/desktop)
endif()

target_link_libraries(PrintFlow
    PRIVATE
        Qt6::Quick
        Qt6::Concurrent
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::Xml
        Qt6::Network
)

if(TARGET PrintFlowPrinterApi)
    target_link_libraries(PrintFlow PRIVATE PrintFlow::PrinterApi)
endif()

if(NOT ANDROID)
    find_path(CUPS_INCLUDE_DIR cups/cups.h PATH_SUFFIXES cups)
    find_library(CUPS_LIBRARY NAMES cups)

    if(CUPS_INCLUDE_DIR AND CUPS_LIBRARY)
        message(STATUS "CUPS found: ${CUPS_LIBRARY}")
        target_include_directories(PrintFlow PRIVATE ${CUPS_INCLUDE_DIR})
        target_link_libraries(PrintFlow PRIVATE ${CUPS_LIBRARY})
    else()
        message(FATAL_ERROR "CUPS library not found")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(PrintFlow PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(NOT ANDROID OR RIP_ANDROID_ENABLE_RIP_PROCESSING)
    target_link_libraries(PrintFlow
        PRIVATE
            PkgConfig::ImageMagick
            PkgConfig::LCMS
    )
endif()

if(ANDROID AND RIP_ANDROID_ENABLE_RIP_PROCESSING)
    target_compile_definitions(PrintFlow PRIVATE RIP_ANDROID_ENABLE_RIP_PROCESSING=1)
endif()

set(RIP_BUILTIN_THEMES default nocai xante)
if(RIP_THEME_FILE)
    if(NOT EXISTS "${RIP_THEME_FILE}")
        message(FATAL_ERROR "RIP_THEME_FILE does not exist: ${RIP_THEME_FILE}")
    endif()

    file(READ "${RIP_THEME_FILE}" RIP_CUSTOM_THEME_JSON)
    string(JSON RIP_CUSTOM_THEME_ID ERROR_VARIABLE RIP_CUSTOM_THEME_JSON_ERROR GET "${RIP_CUSTOM_THEME_JSON}" id)
    if(RIP_CUSTOM_THEME_JSON_ERROR)
        message(FATAL_ERROR "RIP_THEME_FILE is invalid JSON or is missing required field 'id': ${RIP_CUSTOM_THEME_JSON_ERROR}")
    endif()
    foreach(required_field displayName appName)
        string(JSON RIP_CUSTOM_THEME_REQUIRED_VALUE ERROR_VARIABLE RIP_CUSTOM_THEME_REQUIRED_ERROR GET "${RIP_CUSTOM_THEME_JSON}" "${required_field}")
        if(RIP_CUSTOM_THEME_REQUIRED_ERROR OR RIP_CUSTOM_THEME_REQUIRED_VALUE STREQUAL "")
            message(FATAL_ERROR "RIP_THEME_FILE is missing required field '${required_field}': ${RIP_THEME_FILE}")
        endif()
    endforeach()

    set(RIP_SELECTED_THEME_RESOURCE ":/themes/custom/theme.json")
    set(RIP_GENERATED_THEME_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/resources/themes/custom")
    file(MAKE_DIRECTORY "${RIP_GENERATED_THEME_DIR}")
    configure_file("${RIP_THEME_FILE}" "${RIP_GENERATED_THEME_DIR}/theme.json" COPYONLY)
    set(RIP_CUSTOM_THEME_RESOURCE_FILE "${RIP_GENERATED_THEME_DIR}/theme.json")
    message(STATUS "Selected custom theme file: ${RIP_THEME_FILE}")
else()
    if(NOT RIP_THEME IN_LIST RIP_BUILTIN_THEMES)
        message(FATAL_ERROR "Unknown RIP_THEME '${RIP_THEME}'. Expected one of: ${RIP_BUILTIN_THEMES}")
    endif()

    set(RIP_SELECTED_THEME_RESOURCE ":/themes/${RIP_THEME}/theme.json")
    message(STATUS "Selected built-in theme: ${RIP_THEME}")
endif()

target_compile_definitions(PrintFlow PRIVATE
    RIP_SELECTED_THEME_RESOURCE="${RIP_SELECTED_THEME_RESOURCE}"
)

if(ANDROID)
    set_target_properties(PrintFlow PROPERTIES
        QT_ANDROID_APP_NAME "PrintFlow"
        QT_ANDROID_PACKAGE_NAME "com.ripapp.printer"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()

include(cmake/DirectPrintSdk.cmake)
if(ANDROID)
    printflow_configure_direct_print_sdk(PrintFlow)
endif()

if(NOT ANDROID)
    set(PRINTFLOW_PRINTER_SERVICE_SOURCES
        src/printerservice/main.cpp
        src/printerservice/PrinterServiceServer.h src/printerservice/PrinterServiceServer.cpp
        src/printerservice/PrinterServiceProtocol.h src/printerservice/PrinterServiceProtocol.cpp
        src/core/IPrintOutputClient.h
        ${PRINTFLOW_DIRECT_PRINT_BACKEND_SOURCES}
    )
    add_executable(PrintFlowPrinterService ${PRINTFLOW_PRINTER_SERVICE_SOURCES})
    set_target_properties(PrintFlowPrinterService PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(PrintFlowPrinterService PRIVATE
        src/core
        src/printerservice
        src/vendor/nocai
    )
    target_link_libraries(PrintFlowPrinterService PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Network
        ${CMAKE_DL_LIBS}
    )
    printflow_configure_direct_print_sdk(PrintFlowPrinterService)
    add_dependencies(PrintFlow PrintFlowPrinterService)
endif()

file(GLOB RIP_QML_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/qml/*.qml"
)

set(RIP_ASSET_FILES
    resources/assets/logo.png
    resources/assets/RIP_App_1440_Plain_Default.icc
    resources/assets/RIP_App_1440_Plain_Neutral.icc
    resources/assets/RIP_App_1200_8C.icc
    resources/assets/RIP_App_1200_4C.icc
    resources/assets/RIP_App_Linearization_1200_4C.xml
    resources/assets/RIP_App_Linearization_1200_8C.xml
    resources/assets/RIP_App_Generic_CMYK.icc
    resources/assets/sRGBProfile.icm
)

set(RIP_I18N_FILES
    resources/i18n/strings.json
    resources/i18n/strings.zh-Hans.json
)

file(GLOB_RECURSE RIP_THEME_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/*/theme.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/*/assets/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/vendor/*/assets/*"
)

if(RIP_EMBED_BLUE_NOISE_MASKS)
    file(GLOB RIP_BLUE_NOISE_MASKS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/blue_noise_mask_512_12000/*.tiff"
    )

    if(RIP_BLUE_NOISE_MASKS)
        list(APPEND RIP_ASSET_FILES ${RIP_BLUE_NOISE_MASKS})
    else()
        message(WARNING "Blue-noise mask TIFFs were not found under resources/assets/blue_noise_mask_512_12000.")
    endif()
else()
    message(STATUS "Blue-noise mask resource embedding is disabled. Set RIP_EMBED_BLUE_NOISE_MASKS=ON to package local masks into the Qt resource bundle.")
endif()

qt_add_resources(PrintFlow "app_resources"
    PREFIX "/"
    BASE "resources"
    FILES
        ${RIP_QML_FILES}
        ${RIP_ASSET_FILES}
        ${RIP_I18N_FILES}
        ${RIP_THEME_FILES}
)

if(RIP_CUSTOM_THEME_RESOURCE_FILE)
    qt_add_resources(PrintFlow "custom_theme_resource"
        PREFIX "/"
        BASE "${CMAKE_CURRENT_BINARY_DIR}/generated/resources"
        FILES
            "${RIP_CUSTOM_THEME_RESOURCE_FILE}"
    )
endif()

set_target_properties(PrintFlow PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set(PRINTFLOW_INSTALL_TARGETS PrintFlow)
if(TARGET PrintFlowPrinterService)
    list(APPEND PRINTFLOW_INSTALL_TARGETS PrintFlowPrinterService)
endif()
install(TARGETS ${PRINTFLOW_INSTALL_TARGETS}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TARGET PrintFlowPrinterApi)
    install(TARGETS PrintFlowPrinterApi
        EXPORT PrintFlowPrinterApiTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        cmake/PrintFlowPrinterApiConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/PrintFlowPrinterApiConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PrintFlowPrinterApi"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/PrintFlowPrinterApiConfigVersion.cmake"
        VERSION 1.0.0
        COMPATIBILITY SameMajorVersion
    )
    install(EXPORT PrintFlowPrinterApiTargets
        FILE PrintFlowPrinterApiTargets.cmake
        NAMESPACE PrintFlow::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PrintFlowPrinterApi"
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PrintFlowPrinterApiConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PrintFlowPrinterApiConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PrintFlowPrinterApi"
    )
    install(FILES
        src/core/IPrintOutputClient.h
        src/printerservice/PrintFlowPrinterApiExport.h
        src/printerservice/PrinterServiceClient.h
        src/printerservice/PrinterServiceProtocol.h
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/PrintFlowPrinterApi"
    )
endif()

if(UNIX AND NOT ANDROID)
    install(FILES resources/packaging/linux/printflow.desktop
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES resources/assets/logo.png
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps"
        RENAME PrintFlow.png)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(PRINTFLOW_PACKAGE_ARCH x86_64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(PRINTFLOW_PACKAGE_ARCH aarch64)
    else()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PRINTFLOW_PACKAGE_ARCH)
    endif()
    set(CPACK_PACKAGE_NAME PrintFlow)
    set(CPACK_PACKAGE_VENDOR "PrintFlow")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "PrintFlow RIP and persistent unified printer service")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME
        "PrintFlow-${PROJECT_VERSION}-${PRINTFLOW_PACKAGE_ARCH}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "PrintFlow")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcap2-bin, network-manager")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/packaging/linux/debian/preinst"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/packaging/linux/debian/postinst")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    include(CPack)
endif()

if(NOT ANDROID AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
