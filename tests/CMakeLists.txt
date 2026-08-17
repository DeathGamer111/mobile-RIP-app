function(add_printflow_qt_test target test_name)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/core
        ${PROJECT_SOURCE_DIR}/src/printerservice
        ${PROJECT_SOURCE_DIR}/src/rip
        ${PROJECT_SOURCE_DIR}/src/vendor/nocai
    )
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_test(NAME PrintFlow.Build.DirectPrintSdkHelpers
    COMMAND "${CMAKE_COMMAND}"
        -DPRINTFLOW_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/direct-print-sdk-helper-test
        -DTEST_MODE=helpers
        -DTEST_HOST_PROCESSOR=${CMAKE_HOST_SYSTEM_PROCESSOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/DirectPrintSdkHelpersTest.cmake)

add_test(NAME PrintFlow.Build.DirectPrintSdkStrictMissing
    COMMAND "${CMAKE_COMMAND}"
        -DPRINTFLOW_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/direct-print-sdk-strict-missing-test
        -DTEST_MODE=strict-missing
        -DTEST_HOST_PROCESSOR=${CMAKE_HOST_SYSTEM_PROCESSOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/DirectPrintSdkHelpersTest.cmake)
set_tests_properties(PrintFlow.Build.DirectPrintSdkStrictMissing PROPERTIES
    WILL_FAIL TRUE)

add_test(NAME PrintFlow.Build.DirectPrintSdkStrictIncomplete
    COMMAND "${CMAKE_COMMAND}"
        -DPRINTFLOW_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/direct-print-sdk-strict-incomplete-test
        -DTEST_MODE=strict-incomplete
        -DTEST_HOST_PROCESSOR=${CMAKE_HOST_SYSTEM_PROCESSOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/DirectPrintSdkHelpersTest.cmake)
set_tests_properties(PrintFlow.Build.DirectPrintSdkStrictIncomplete PROPERTIES
    WILL_FAIL TRUE)

set(RIP_TEST_ASSET_FILES ${RIP_ASSET_FILES})
list(TRANSFORM RIP_TEST_ASSET_FILES PREPEND "${PROJECT_SOURCE_DIR}/")

set(RIP_TEST_I18N_FILES ${RIP_I18N_FILES})
list(TRANSFORM RIP_TEST_I18N_FILES PREPEND "${PROJECT_SOURCE_DIR}/")

add_printflow_qt_test(PrintFlowPrintJobModelTest PrintFlow.Core.PrintJobModel
    PrintJobModelTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/PrintJob.h
    ${PROJECT_SOURCE_DIR}/src/core/PrintJobModel.h
    ${PROJECT_SOURCE_DIR}/src/core/PrintJobModel.cpp
)

target_link_libraries(PrintFlowPrintJobModelTest PRIVATE Qt6::Gui)

add_printflow_qt_test(PrintFlowAssetPlatformTest PrintFlow.Core.AssetManagerAndPlatformCapabilities
    AssetPlatformTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.h
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/PlatformCapabilities.h
    ${PROJECT_SOURCE_DIR}/src/core/PlatformCapabilities.cpp
)

add_printflow_qt_test(PrintFlowStringsTest PrintFlow.Core.AppStrings
    AppStringsTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/AppStrings.h
    ${PROJECT_SOURCE_DIR}/src/core/AppStrings.cpp
)

qt_add_resources(PrintFlowStringsTest "strings_test_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        ${RIP_TEST_I18N_FILES}
)

add_printflow_qt_test(PrintFlowThemeManagerTest PrintFlow.Core.ThemeManager
    ThemeManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ThemeManager.h
    ${PROJECT_SOURCE_DIR}/src/core/ThemeManager.cpp
)

target_link_libraries(PrintFlowThemeManagerTest PRIVATE Qt6::Gui)
target_compile_definitions(PrintFlowThemeManagerTest PRIVATE
    RIP_SELECTED_THEME_RESOURCE="${RIP_SELECTED_THEME_RESOURCE}"
)

qt_add_resources(PrintFlowThemeManagerTest "theme_test_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        ${RIP_THEME_FILES}
)

add_printflow_qt_test(PrintFlowRipPipelineTest PrintFlow.Rip.PipelineValidation
    RipPipelineTest.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkLinearization.h
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkLinearization.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkScreenEngine.h
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkScreenEngine.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkToneBuilder.h
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkToneBuilder.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/MultiInkTypes.h
    ${PROJECT_SOURCE_DIR}/src/rip/RasterAlphaMask.h
    ${PROJECT_SOURCE_DIR}/src/rip/RasterAlphaMask.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/X33WhiteToneBuilder.h
    ${PROJECT_SOURCE_DIR}/src/rip/X33WhiteToneBuilder.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/PrintJobMultiInk.h
    ${PROJECT_SOURCE_DIR}/src/rip/PrintJobMultiInk.cpp
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.h
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ColorManagementManager.h
    ${PROJECT_SOURCE_DIR}/src/core/ColorManagementManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/IPrintOutputClient.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.cpp
)

target_link_libraries(PrintFlowRipPipelineTest PRIVATE
    Qt6::Concurrent
    Qt6::Xml
    PkgConfig::ImageMagick
    PkgConfig::LCMS
)

qt_add_resources(PrintFlowRipPipelineTest "rip_pipeline_test_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        ${RIP_TEST_ASSET_FILES}
)

add_printflow_qt_test(PrintFlowVendorIsolationTest PrintFlow.Vendor.DirectPrintUnavailable
    VendorIsolationTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/IPrintOutputClient.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiDirectPrintClient.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiDirectPrintClient.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmCommandTagCompatibility.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmCommandTagCompatibility.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmStaleSessionRecovery.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmStaleSessionRecovery.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiDirectPrintCompatibility.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiDirectPrintCompatibility.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.cpp
)
target_link_libraries(PrintFlowVendorIsolationTest PRIVATE Qt6::Concurrent)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64|ARM64)$")
    set_target_properties(PrintFlowVendorIsolationTest PROPERTIES ENABLE_EXPORTS ON)
    target_link_libraries(PrintFlowVendorIsolationTest PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(PrintFlowFakeMangledDirectPrintSdk SHARED
    FakeMangledDirectPrintSdk.cpp
)
set_target_properties(PrintFlowFakeMangledDirectPrintSdk PROPERTIES
    OUTPUT_NAME SYPrintAPIforPROII
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake-sdk"
)
add_dependencies(PrintFlowVendorIsolationTest PrintFlowFakeMangledDirectPrintSdk)
target_compile_definitions(PrintFlowVendorIsolationTest PRIVATE
    PRINTFLOW_FAKE_MANGLED_SDK_PATH="$<TARGET_FILE:PrintFlowFakeMangledDirectPrintSdk>"
)
if(TARGET PrintFlowPrinterService)
    get_property(PRINTFLOW_CONFIGURED_SDK_ROOT TARGET PrintFlowPrinterService
        PROPERTY PRINTFLOW_DIRECT_PRINT_SDK_ROOT)
else()
    get_property(PRINTFLOW_CONFIGURED_SDK_ROOT TARGET PrintFlow
        PROPERTY PRINTFLOW_DIRECT_PRINT_SDK_ROOT)
endif()
if(PRINTFLOW_CONFIGURED_SDK_ROOT)
    target_compile_definitions(PrintFlowVendorIsolationTest PRIVATE
        PRINTFLOW_CONFIGURED_SDK_ROOT="${PRINTFLOW_CONFIGURED_SDK_ROOT}"
    )
endif()

add_printflow_qt_test(PrintFlowNocaiPrnWriterTest PrintFlow.Vendor.NocaiPrnWriter
    NocaiPrnWriterTest.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.cpp
)

add_printflow_qt_test(PrintFlowNocaiArmStaleSessionRecoveryTest
    PrintFlow.Vendor.NocaiArmStaleSessionRecovery
    NocaiArmStaleSessionRecoveryTest.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmStaleSessionRecovery.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiArmStaleSessionRecovery.cpp
)

add_printflow_qt_test(PrintFlowPrinterServiceProtocolTest
    PrintFlow.PrinterService.Protocol
    PrinterServiceProtocolTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/IPrintOutputClient.h
    ${PROJECT_SOURCE_DIR}/src/printerservice/PrinterServiceProtocol.h
    ${PROJECT_SOURCE_DIR}/src/printerservice/PrinterServiceProtocol.cpp
)
target_link_libraries(PrintFlowPrinterServiceProtocolTest PRIVATE Qt6::Network)

add_executable(PrintFlowPrinterApiSurfaceTest PrinterApiSurfaceTest.cpp)
target_link_libraries(PrintFlowPrinterApiSurfaceTest PRIVATE
    PrintFlow::PrinterApi Qt6::Test)
add_test(NAME PrintFlow.PrinterService.PublicApi
         COMMAND PrintFlowPrinterApiSurfaceTest)

add_printflow_qt_test(PrintFlowCMYKAssetManagerTest PrintFlow.Rip.CMYKAssetManager
    PrintJobCMYKAssetManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.h
    ${PROJECT_SOURCE_DIR}/src/core/AssetManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ColorManagementManager.h
    ${PROJECT_SOURCE_DIR}/src/core/ColorManagementManager.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/RasterAlphaMask.h
    ${PROJECT_SOURCE_DIR}/src/rip/RasterAlphaMask.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/X33WhiteToneBuilder.h
    ${PROJECT_SOURCE_DIR}/src/rip/X33WhiteToneBuilder.cpp
    ${PROJECT_SOURCE_DIR}/src/rip/PrintJobCMYK.h
    ${PROJECT_SOURCE_DIR}/src/rip/PrintJobCMYK.cpp
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.h
    ${PROJECT_SOURCE_DIR}/src/vendor/nocai/NocaiPrnWriter.cpp
)

target_link_libraries(PrintFlowCMYKAssetManagerTest PRIVATE
    Qt6::Concurrent
    Qt6::Xml
    PkgConfig::ImageMagick
    PkgConfig::LCMS
)

qt_add_resources(PrintFlowCMYKAssetManagerTest "cmyk_asset_test_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        ${RIP_TEST_ASSET_FILES}
)

add_executable(PrintFlowAndroidStubTest
    AndroidStubTest.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/android/ImageLoader.h
    ${PROJECT_SOURCE_DIR}/src/platform/android/ImageLoader.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/android/PrintJobCMYK.h
    ${PROJECT_SOURCE_DIR}/src/platform/android/PrintJobCMYK.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/android/PrintJobMultiInk.h
    ${PROJECT_SOURCE_DIR}/src/platform/android/PrintJobMultiInk.cpp
    ${PROJECT_SOURCE_DIR}/src/core/IPrintOutputClient.h
)

target_include_directories(PrintFlowAndroidStubTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/platform/android
)

target_link_libraries(PrintFlowAndroidStubTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME PrintFlow.Platform.AndroidSafeStubs
         COMMAND PrintFlowAndroidStubTest)
