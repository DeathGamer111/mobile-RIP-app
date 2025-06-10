cmake_minimum_required(VERSION 3.16)

project(RIPPrinterApp VERSION 0.1 LANGUAGES CXX)

# QT Setup
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Quick Concurrent)

# qt_standard_project_setup(REQUIRES 6.8)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ImageMagick Setup
find_package(PkgConfig REQUIRED)
pkg_check_modules(ImageMagick REQUIRED IMPORTED_TARGET Magick++)

# Little CMS Setup
pkg_check_modules(LCMS REQUIRED IMPORTED_TARGET lcms2)

# Executable sources
qt_add_executable(appRIPPrinterApp
    main.cpp
    PrintJobModel.h PrintJobModel.cpp
    PrintJobOutput.h PrintJobOutput.cpp
    PrintJobNocai.h PrintJobNocai.cpp
    ImageLoader.h ImageLoader.cpp
    ImageEditor.h ImageEditor.cpp
    ColorProfile.h ColorProfile.cpp
    stb_image.h
)

# Target C++ sources
target_sources(appRIPPrinterApp PRIVATE
    PrintJobModel.h
    PrintJob.h
    ImageLoader.h
    ImageEditor.h
    PrintJobOutput.h
    PrintJobNocai.h
    ColorProfile.h
)

# Mark this as the default QML resource prefix
set_target_properties(appRIPPrinterApp PROPERTIES QT_RESOURCE_PREFIX "/")

qt_add_resources(appRIPPrinterApp
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/JobListView.qml
        qml/JobDetailsView.qml
        qml/ImageEditorView.qml
        qml/ImpositionView.qml
        qml/PrinterSetupView.qml
        qml/DraggableItem.qml
        qml/Toast.qml
        assets/logo.png
        assets/blue_noise_mask_256/mask_c.tiff
        assets/blue_noise_mask_256/mask_m.tiff
        assets/blue_noise_mask_256/mask_y.tiff
        assets/blue_noise_mask_256/mask_k.tiff
        assets/blue_noise_mask_512/mask_c.tiff
        assets/blue_noise_mask_512/mask_m.tiff
        assets/blue_noise_mask_512/mask_y.tiff
        assets/blue_noise_mask_512/mask_k.tiff
        assets/RIP_App_Plain_Paper.icm
        assets/sRGBProfile.icm
        assets/magick
        scripts/cmyk_dither_mask.sh
)


# Platform-specific CUPS linking
if(UNIX AND NOT APPLE)  # Linux, Android
    find_path(CUPS_INCLUDE_DIR cups/cups.h PATH_SUFFIXES cups)
    find_library(CUPS_LIBRARY NAMES cups)

    if(CUPS_INCLUDE_DIR AND CUPS_LIBRARY)
        message(STATUS "CUPS found: ${CUPS_LIBRARY}")
        target_include_directories(appRIPPrinterApp PRIVATE ${CUPS_INCLUDE_DIR})
        target_link_libraries(appRIPPrinterApp PRIVATE ${CUPS_LIBRARY})
    else()
        message(FATAL_ERROR "CUPS library not found")
    endif()
endif()


# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appRIPPrinterApp PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appRIP-Printer-App
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)


target_link_libraries(appRIPPrinterApp
    PRIVATE Qt6::Quick
    PRIVATE Qt6::Concurrent
    PRIVATE cups
    PRIVATE PkgConfig::ImageMagick
    PRIVATE PkgConfig::LCMS
)


include(GNUInstallDirs)
install(TARGETS appRIPPrinterApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
