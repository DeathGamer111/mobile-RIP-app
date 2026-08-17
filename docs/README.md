# PrintFlow Documentation

This directory contains supporting design and workflow material for PrintFlow.

## Current App Shape

PrintFlow is a Qt 6 C++/QML application targeting Linux desktop and Android, with Android APK support for x86-64 emulator testing and ARM64 device packaging.

The base product identity is vendor-neutral: `PrintFlow`. Customer and vendor variants should be handled through theme configuration rather than by renaming the base target.

The source tree is split by responsibility:

- `src/core/`: shared job models, settings, asset helpers, string resources, themes, and platform capability flags.
- `src/rip/`: native image processing, color conversion, screening, and PRN generation.
- `src/platform/desktop/`: Linux desktop integrations such as CUPS.
- `src/platform/android/`: Android output integration and boot-only fallback facades.
- `src/vendor/nocai/`: isolated direct-print vendor adapter and neutral output interface implementation.
- `src/third_party/stb/`: third-party single-header image loader.
- `resources/qml/`: Qt Quick UI.
- `resources/assets/`: bundled profiles and runtime assets.
- `resources/themes/`: theme JSON and theme assets.
- `resources/i18n/`: string-resource JSON files.
- `resources/packaging/linux/`: Linux desktop packaging metadata.
- `third_party/nocai/direct-print/`: minimal ARM64 and x86-64 Linux SDK runtime pairs.
- `third_party/imagemagick/android/`: curated ImageMagick Android headers and x86-64/ARM64 runtimes.
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

Android builds use Qt's Android `qt-cmake` wrapper and the `apk` target through `scripts/dev_build_android.sh`. Native ImageMagick/Magick++ processing is enabled by default for both supported ABIs; `scripts/setup_android_imagemagick.sh` verifies and stages its pinned runtime. For temporary printer testing, `scripts/run_android_printer_bridge.sh` maps the Android printer-service client through ADB to a loopback-only Linux service running the real vendor SDK. Native Android printer communication still requires a complete target pair under `android/<arm64|x86_64>/`; `scripts/validate_android_direct_print_sdk.sh` rejects Linux/glibc libraries before packaging. Set `RIP_EMBED_BLUE_NOISE_MASKS=ON` for full local print rasterization. Use `./Dev_Build_App.sh --android` for emulator testing and `./Dev_Build_App.sh --android-device` for USB device testing.

Linux CMake builds select the target pair from `third_party/nocai/direct-print/linux/<architecture>`, validate both ELF files, and stage the API plus its required printer-socket subtree beside the service. AppImage and Debian packaging include only the selected native pair.

## Files

- `RIP-App_Software_Design_Document.docx`: design document.
- `RIP-APP_Flowchart.drawio`: editable workflow diagram.
- `RIP-APP_Flowchart.png`: exported workflow diagram.
