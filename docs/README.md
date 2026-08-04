# PrintFlow Documentation

This directory contains supporting design and workflow material for PrintFlow.

## Current App Shape

PrintFlow is a Qt 6 C++/QML application targeting Linux desktop first, with Android APK support available for emulator boot testing and physical-device packaging.

The base product identity is vendor-neutral: `PrintFlow`. Customer and vendor variants should be handled through theme configuration rather than by renaming the base target.

The source tree is split by responsibility:

- `src/core/`: shared job models, settings, asset helpers, string resources, themes, and platform capability flags.
- `src/rip/`: native image processing, color conversion, screening, and PRN generation.
- `src/platform/desktop/`: Linux desktop integrations such as CUPS.
- `src/platform/android/`: Android-safe facades used for APK boot while native RIP dependencies are ported.
- `src/vendor/nocai/`: isolated direct-print vendor adapter and neutral output interface implementation.
- `src/third_party/stb/`: third-party single-header image loader.
- `resources/qml/`: Qt Quick UI.
- `resources/assets/`: bundled profiles and runtime assets.
- `resources/themes/`: theme JSON and theme assets.
- `resources/i18n/`: string-resource JSON files.
- `resources/packaging/linux/`: Linux desktop packaging metadata.
- `android/`: Qt Android package template.

## Build Notes

Linux builds use CMake with Qt, CUPS, Magick++, and Little CMS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Run local tests with:

```bash
scripts/run_tests.sh
```

Android builds use Qt's Android `qt-cmake` wrapper and the `apk` target through `scripts/dev_build_android.sh`. The x86_64 emulator build uses Android-safe facades and does not require a direct-print SDK. Android direct-print packaging requires a target-ABI Android API library in the supplied SDK; Linux ARM64 libraries are not treated as Android libraries. Use `./Dev_Build_App.sh --android` for emulator testing and `./Dev_Build_App.sh --android-device` for USB device testing.

Linux SDK drops can be supplied through `DIRECT_PRINT_SDK_ROOT` or `DIRECT_PRINT_SDK_ARCHIVE`. CMake chooses and validates binaries against the target architecture, extracts archives into the build tree, and stages the API plus its required printer-socket subtree beside the executable.

## Files

- `RIP-App_Software_Design_Document.docx`: design document.
- `RIP-APP_Flowchart.drawio`: editable workflow diagram.
- `RIP-APP_Flowchart.png`: exported workflow diagram.
