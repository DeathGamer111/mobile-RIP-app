cmake_minimum_required(VERSION 3.16)

project(RIPPrinterApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appRIPPrinterApp
    main.cpp
    PrintJobModel.cpp
    ImageLoader.h ImageLoader.cpp
    stb_image.h
    JobListView.qml
    JobDetailsView.qml
    PrintJobOutput.h PrintJobOutput.cpp
    ToDoList
    Toast.qml
)

target_sources(appRIPPrinterApp PRIVATE
    PrintJobModel.h
    PrintJob.h
    ImageLoader.h
    PrintJobOutput.h
)

qt_add_resources(appRIPPrinterApp "qml"
    FILES
        Main.qml
        JobListView.qml
        JobDetailsView.qml
        Toast.qml
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
    PRIVATE cups
)

include(GNUInstallDirs)
install(TARGETS appRIPPrinterApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
