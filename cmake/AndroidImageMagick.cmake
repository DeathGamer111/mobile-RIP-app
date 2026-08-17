function(printflow_configure_android_imagemagick target)
    if(NOT ANDROID)
        message(FATAL_ERROR "Android ImageMagick can only be configured for an Android target.")
    endif()

    set(PRINTFLOW_ANDROID_IMAGEMAGICK_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imagemagick/android"
        CACHE PATH "Root of the curated Android ImageMagick runtime")

    if(NOT CMAKE_ANDROID_ARCH_ABI)
        message(FATAL_ERROR "CMAKE_ANDROID_ARCH_ABI is required for Android ImageMagick.")
    endif()

    if(NOT CMAKE_ANDROID_ARCH_ABI MATCHES "^(x86_64|arm64-v8a)$")
        message(FATAL_ERROR
            "Android ImageMagick is available only for x86_64 and arm64-v8a; "
            "requested ABI: ${CMAKE_ANDROID_ARCH_ABI}")
    endif()

    set(imagemagick_root "${PRINTFLOW_ANDROID_IMAGEMAGICK_ROOT}")
    set(imagemagick_lib_dir "${imagemagick_root}/lib/${CMAKE_ANDROID_ARCH_ABI}")
    set(imagemagick_include_dir "${imagemagick_root}/include")
    set(imagemagick_config_dir "${imagemagick_root}/config/${CMAKE_ANDROID_ARCH_ABI}")

    set(imagemagick_runtime_libraries
        "${imagemagick_lib_dir}/libmagick++-7.so"
        "${imagemagick_lib_dir}/libmagickwand-7.so"
        "${imagemagick_lib_dir}/libmagickcore-7.so"
        "${imagemagick_lib_dir}/libomp.so"
    )

    foreach(required_path IN ITEMS
        "${imagemagick_include_dir}/Magick++.h"
        "${imagemagick_include_dir}/lcms2.h"
        "${imagemagick_config_dir}/MagickCore/magick-baseconfig.h"
        ${imagemagick_runtime_libraries})
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Android ImageMagick is incomplete: ${required_path}\n"
                "Run scripts/setup_android_imagemagick.sh and configure again.")
        endif()
    endforeach()

    add_library(PrintFlowAndroidOpenMP SHARED IMPORTED GLOBAL)
    set_target_properties(PrintFlowAndroidOpenMP PROPERTIES
        IMPORTED_LOCATION "${imagemagick_lib_dir}/libomp.so")

    add_library(PrintFlowAndroidMagickCore SHARED IMPORTED GLOBAL)
    set_target_properties(PrintFlowAndroidMagickCore PROPERTIES
        IMPORTED_LOCATION "${imagemagick_lib_dir}/libmagickcore-7.so"
        INTERFACE_INCLUDE_DIRECTORIES
            "${imagemagick_config_dir};${imagemagick_include_dir}"
        INTERFACE_COMPILE_DEFINITIONS
            "MAGICKCORE_QUANTUM_DEPTH=16;MAGICKCORE_HDRI_ENABLE=1")
    target_link_libraries(PrintFlowAndroidMagickCore INTERFACE PrintFlowAndroidOpenMP)

    add_library(PrintFlowAndroidMagickWand SHARED IMPORTED GLOBAL)
    set_target_properties(PrintFlowAndroidMagickWand PROPERTIES
        IMPORTED_LOCATION "${imagemagick_lib_dir}/libmagickwand-7.so")
    target_link_libraries(PrintFlowAndroidMagickWand
        INTERFACE PrintFlowAndroidMagickCore)

    add_library(PrintFlowAndroidMagickPP SHARED IMPORTED GLOBAL)
    set_target_properties(PrintFlowAndroidMagickPP PROPERTIES
        IMPORTED_LOCATION "${imagemagick_lib_dir}/libmagick++-7.so")
    target_link_libraries(PrintFlowAndroidMagickPP
        INTERFACE PrintFlowAndroidMagickWand PrintFlowAndroidMagickCore)

    target_link_libraries(${target} PRIVATE PrintFlowAndroidMagickPP)
    set_property(TARGET ${target} APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        ${imagemagick_runtime_libraries})

    message(STATUS
        "Android ImageMagick 7.1.2-29 enabled for ${CMAKE_ANDROID_ARCH_ABI}")
endfunction()
