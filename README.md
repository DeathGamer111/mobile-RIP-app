# PrintFlow

> Active Qt/QML raster image processing application for desktop print-job preparation and output generation.

PrintFlow is a Qt 6 raster image processing application for preparing print jobs, previewing and editing artwork, managing printer settings, and generating raster/PRN output through pluggable vendor backends.

The current codebase is focused on Linux desktop development with CMake, CUPS, ImageMagick, and Little CMS. Android APK support is available for emulator boot testing and physical-device packaging work.

## Current Status

- Active branch: `master`
- Build system: CMake
- UI framework: Qt Quick/QML
- Main development build script: `./Dev_Build_App.sh`
- Primary executable target: `PrintFlow`
- Linux desktop status: builds and runs with the native RIP pipeline, CUPS integration, ImageMagick, Little CMS, theme resources, string resources, and the local test suite.
- Android APK status: x86_64 emulator builds use boot-safe Android facades. Android direct print is packaged only when the supplied SDK contains an Android API library for the target ABI; Linux ARM64 libraries are not placed into Android APKs.
- Base product identity: `PrintFlow`; customer or vendor display branding belongs in theme configuration.

## Features

- Print job management with Qt model roles and JSON persistence.
- Image loading, validation, metadata extraction, and PDF preview rendering through ImageMagick.
- Image editing tools for crop, rotate, flip, resize, color adjustment, blur, sepia, vignette, swirl, implode, text, rectangle drawing, undo, and redo.
- Imposition view for positioning artwork on the selected media.
- Printer setup flow for desktop printers, prepared PRN output, and optional vendor direct-print workflows.
- ICC profile handling through Little CMS, including bundled CMYK and multi-ink output profiles.
- Color-management settings for default input/output profiles, printer-specific profiles, profile families, and persisted dot strategy settings.
- PRN generation with 2-bit dot classification, stochastic screening, and dot promotion controls.
- Multi-ink PRN generation with 4, 5, 6, 7, 8, and 10 channel ink layouts.
- Linearization support using bundled XML presets.
- Runtime asset preparation for bundled ICC profiles, linearization files, logo assets, and local blue-noise masks.
- Theme and string-resource systems for build-time branding and runtime language selection.

## Supported Ink Layouts

The multi-ink backend currently supports:

- 4 color: `Y M C K`
- 5 color: `Y M C K + White`
- 6 color: `Y M C K + Light Magenta + Light Cyan`
- 7 color: `Y M C K + Light Magenta + Light Cyan + White`
- 8 color: `Y M C K + Light Magenta + Light Cyan + Light Black + Light Light Black`
- 10 color: `Y M C K + Light Magenta + Light Cyan + Light Black + Light Light Black + White + Varnish`

## Project Layout

```text
.
|-- android/                       Qt Android package template
|-- docs/                          Flowchart and software design document
|-- resources/assets/              Bundled ICC profiles, linearization XML, and logo
|-- resources/i18n/                Runtime string resources
|-- resources/packaging/linux/     Linux desktop/AppImage metadata
|-- resources/qml/                 Qt Quick user interface
|-- resources/themes/              Built-in theme JSON and theme assets
|-- scripts/                       Linux, Android, packaging, and policy helper scripts
|-- src/app/                       Application bootstrap
|-- src/core/                      Shared models, settings, assets, strings, themes, and capabilities
|-- src/platform/android/          Android-safe platform facades for APK boot
|-- src/platform/desktop/          Linux desktop integrations such as CUPS
|-- src/printerservice/            Persistent service, versioned IPC, and public client API
|-- src/rip/                       Native RIP, color, screening, and PRN pipeline
|-- src/third_party/stb/           Third-party single-header image loader
`-- src/vendor/nocai/              Isolated direct-print vendor adapter
```

## Tests

Run the local Linux test suite with:

```bash
scripts/run_tests.sh
```

The script configures `build-tests`, builds the app and test executables, then runs `ctest --output-on-failure`. Tests cover the job model, asset/platform helpers, string resources, theme loading, RIP pipeline behavior, Android-safe stubs, vendor isolation, SDK architecture normalization, archive selection, and ELF validation. Ordinary test builds do not require `DIRECT_PRINT_SDK_ROOT`, blue-noise mask fixtures, or an Android device.

Important QML views include:

- `resources/qml/Main.qml`
- `resources/qml/JobListView.qml`
- `resources/qml/JobDetailsView.qml`
- `resources/qml/ImageEditorView.qml`
- `resources/qml/ImpositionView.qml`
- `resources/qml/PrinterSetupView.qml`
- `resources/qml/PrinterMaintenanceView.qml`
- `resources/qml/ColorManagementView.qml`

## Assets

Small runtime assets are tracked in `resources/assets/`, including:

- Output ICC profiles for 4-color, 8-color, 1440 plain default, 1440 plain neutral, and generic CMYK workflows.
- `sRGBProfile.icm`
- Linearization XML presets for 4-color and 8-color output.
- `logo.png`

Large blue-noise mask directories are intentionally ignored by Git:

```text
resources/assets/blue_noise_mask_*/**
```

For local builds that generate multi-ink output, the app expects `resources/assets/blue_noise_mask_512_12000/` to exist locally with the mask TIFF files used by `scripts/dev_build_linux.sh`. The masks can be embedded into Qt resources with `-DRIP_EMBED_BLUE_NOISE_MASKS=ON`, but the default leaves them as local runtime assets to avoid very large generated resource objects.

Theme assets live under `resources/themes/<theme-id>/assets/` or `resources/vendor/<vendor-id>/assets/` and are compiled into Qt resources when referenced by theme JSON. Vendor SDK drops and local vendor assets that are not safe to publish should remain outside tracked files or under ignored local paths.

## Direct-print SDK selection

CMake selects direct-print binaries using the target platform and target processor, including when cross-compiling.

Supply either an extracted SDK root or an archive:

```bash
cmake -S . -B build -DDIRECT_PRINT_SDK_ROOT=/path/to/extracted/sdk
cmake -S . -B build -DDIRECT_PRINT_SDK_ARCHIVE=/path/to/vendor-sdk.zip
```

The settings may also be exported as environment variables. When neither is set, Linux builds auto-detect one matching local `DemoForX64Linux*.zip` or `DemoForARM64Linux*.tar*` archive. Archives are extracted only under the build directory.

For Linux, CMake validates the ELF machine type and stages `libSYPrintAPIforPROII.so` beside `PrintFlowPrinterService`, preserving the required `PrinterSocketDLL/linux/<architecture>/PrinterSocket.so` subtree. The desktop GUI does not load the proprietary library. Set `-DDIRECT_PRINT_SDK_STRICT=ON` to make missing, ambiguous, incomplete, or architecture-mismatched SDK inputs a configuration error. The development and package workflows always enable strict mode; ordinary CMake and test builds may omit the proprietary SDK.

The July 2026 x86-64 SDK exports internal `API_*` C++ symbols instead of most documented C names. The adapter prefers the documented interface and falls back to the known x86-64 aliases, allowing this package to load while remaining compatible with a corrected vendor build.

## Requirements

The development script installs/checks the main Linux dependencies:

- CMake and a C++ compiler
- Qt 6 Quick, Widgets, Quick Controls 2, QML tooling, and related QML modules
- CUPS development libraries
- ImageMagick 6 or 7 Magick++ (the scripts select the available development package)
- Little CMS 2
- AppImage/package helper tooling used by the local workflow

On Debian/Ubuntu-style systems, use the main development script when you want the full local setup path:

```bash
./Dev_Build_App.sh
```

The root script delegates to `scripts/dev_build_linux.sh`. It uses `sudo apt-get`, relaxes ImageMagick policy limits through `scripts/Relax_ImageMagick_Limits.sh`, clears the local app cache/build folder, runs CMake, builds the app, and copies blue-noise masks into:

```text
~/.local/share/PrintFlow/runtime_assets/
```

For direct-attached Nocai printers, the Linux development build also:

- grants `PrintFlowPrinterService` `CAP_NET_RAW`, which the vendor SDK needs
  for raw-socket printer discovery and which the ARM service uses to retain the
  selected interface's process-scoped promiscuous receive membership; and
- configures a wired interface with carrier but no existing IPv4 address for
  IPv4 link-local networking (`169.254.0.0/16`).

Set `PRINTFLOW_PRINTER_INTERFACE=enp1s0` (or another interface name) to select
an interface explicitly. Set `PRINTFLOW_CONFIGURE_PRINTER_NETWORK=0` to leave
network configuration untouched. The capability is intentionally applied after
the build because relinking the service executable removes file capabilities.

## Persistent printer service and public API

Linux printing is owned by one per-user `PrintFlowPrinterService` process. The
GUI starts it on demand over a user-only local socket, and the service remains
alive when the GUI closes. Setup, status, maintenance, nozzle checks, and raster
submission are serialized through the same SDK instance. This prevents a GUI
restart or a second API consumer from abandoning the controller session and
then competing for the ARM raw socket.

`PrintFlowPrinterApi` is the public Qt/C++ client library shared by ARM64 and
x86-64 builds. Its protocol is explicitly versioned, validates framed messages,
and transports canonical direct-print settings and raster data without exposing
vendor structs. CMake installs the headers and a `PrintFlow::PrinterApi` target.
See `src/printerservice/README.md` for the client example and protocol contract.

Service output, including redirected vendor stdout/stderr, is retained in
`~/.local/share/PrintFlow/logs/printer-service.log` (with one rotated copy).
For controlled upgrades, the executable supports `--ping` and `--shutdown`
without loading a second SDK owner.

## Linux AppImage package

Build a native, architecture-specific package with the same theme and SDK overrides as the development build:

```bash
./Dev_Build_App.sh --linux-package
RIP_THEME=nocai ./Dev_Build_App.sh --linux-package
DIRECT_PRINT_SDK_ARCHIVE=/path/to/vendor-sdk.tar ./Dev_Build_App.sh --linux-package
```

The workflow selects `x86_64` or `aarch64` from the host architecture, requires the matching direct-print SDK, and packages only that API and printer-socket library. It uses the official `linuxdeploy` Qt and AppImage plugins cached under ignored `.tools/`, validates the GUI, service, public API, and SDK ELF architectures, and writes `output/PrintFlow-<version>-<arch>.AppImage`. It also produces a native Debian package containing the GUI, persistent service, public API development package, and matching SDK. Its install hook grants only the service `CAP_NET_RAW`; this is the recommended customer deployment because capabilities inside a FUSE-mounted AppImage are not reliable. Packages are native per architecture rather than universal.

## Standard Build

For a normal compile without the dependency-install and policy steps:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Run the app with:

```bash
./build/PrintFlow
```

## Theme Builds

PrintFlow supports build-time theme selection for normal/basic and customer-branded builds. If no theme variable is set, the default/basic theme is used.

Built-in theme ids:

- `default`: neutral PrintFlow branding.
- `nocai`: vendor-oriented direct-print theme.
- `xante`: vendor-oriented production workflow theme.

Build with a built-in theme:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-nocai -DCMAKE_BUILD_TYPE=Debug -DRIP_THEME=nocai
cmake -S . -B build-xante -DCMAKE_BUILD_TYPE=Debug -DRIP_THEME=xante
```

The development scripts forward these environment variables to CMake:

```bash
RIP_THEME=nocai scripts/dev_build_linux.sh
RIP_THEME=xante scripts/dev_build_android.sh
```

Custom theme files can be embedded at configure time:

```bash
RIP_THEME_FILE=/path/to/custom-theme.json scripts/dev_build_linux.sh
cmake -S . -B build-custom -DCMAKE_BUILD_TYPE=Debug -DRIP_THEME_FILE=/path/to/custom-theme.json
```

Custom theme JSON must include `id`, `displayName`, and `appName`. Other fields fall back to the default theme when omitted:

```json
{
  "id": "customer",
  "displayName": "Customer Theme",
  "appName": "Customer PrintFlow",
  "primaryColor": "#14181F",
  "secondaryColor": "#1FB8A6",
  "backgroundColor": "#0F131A",
  "surfaceColor": "#14181F",
  "surface2Color": "#1A202A",
  "textColor": "#E6EAF2",
  "subtextColor": "#A7B0C0",
  "dividerColor": "#263042",
  "lightBackgroundColor": "#ECEFF4",
  "lightSurfaceColor": "#F6F7FB",
  "lightSurface2Color": "#E3E7EF",
  "lightTextColor": "#1F2937",
  "lightSubtextColor": "#4B5563",
  "lightDividerColor": "#C7CEDB",
  "accentColor": "#2DD4BF",
  "logoPath": "qrc:/themes/customer/assets/logo.png",
  "splashLogoPath": "qrc:/themes/customer/assets/splash.png",
  "logoWidth": 96,
  "logoHeight": 40,
  "aboutVendorName": "Customer",
  "supportUrl": "https://example.com/support",
  "copyrightText": "Copyright (c) 2026 Customer"
}
```

Custom file builds fail during CMake configure when the file is missing, invalid JSON, or missing required identity fields.

## Android Build

The Android milestone is an APK that builds, installs, and boots in an emulator while keeping desktop-only dependencies behind platform facades. Android x86_64 emulator builds use safe facades for CUPS and the native RIP dependency stack and do not require a direct-print SDK.

Required environment variables for emulator builds:

```bash
export QT_ANDROID_CMAKE="$HOME/Qt/<version>/android_x86_64/bin/qt-cmake"
export ANDROID_SDK_ROOT="$PWD/.android-sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/<version>"
```

Optional environment variable for physical-device direct-print packaging:

```bash
export DIRECT_PRINT_SDK_ROOT="/path/to/local/vendor/sdk/drop"
# Or:
export DIRECT_PRINT_SDK_ARCHIVE="/path/to/local/vendor/sdk/drop.zip"
```

Use only one SDK variable at a time. Android packaging requires `libSYPrintAPIforPROII.so` under an `android/<abi>/` directory in the supplied drop. A Linux/glibc API library is deliberately rejected for Android even when its CPU architecture matches. If `.android-env` points at the x86_64 Qt kit, the build script switches to the sibling `android_arm64_v8a` kit when it exists. SDK files remain local and ignored.

Install the local Android SDK command-line tools, emulator packages, and a Pixel-style AVD:

```bash
scripts/setup_android_emulator.sh
```

Build the APK:

```bash
scripts/dev_build_android.sh
```

Build, install, and launch it on the emulator:

```bash
scripts/android_build_install_run.sh
```

Build, install, and launch it on a USB Android device:

```bash
./Dev_Build_App.sh --android-device
```

or directly:

```bash
ANDROID_TARGET=device scripts/android_build_install_run.sh
```

The device path defaults to `ANDROID_ABI=arm64-v8a`, `BUILD_DIR=build-android-device`, and `adb -d`. Set `ANDROID_SERIAL=<serial>` when more than one physical device is connected.

The Android build defaults to `ANDROID_ABI=x86_64` for emulator testing on Linux. The x86_64 emulator requires KVM/VM acceleration with writable `/dev/kvm`; without it, `scripts/start_android_emulator.sh` and `scripts/run_android_emulator.sh` fail early with a host-setup message.

## Development Notes

- `build/` is ignored and should not be committed.
- The generated Qt resource output under `.rcc/` is ignored.
- The blue-noise mask source directory is ignored because the masks are large local runtime assets.
- Vendor SDK drops are local-only and should stay outside tracked public documentation and commits.
- The tracked ICC and XML assets are required by the color-management and multi-ink paths.
- `Dev_Build_App.sh` is a compatibility wrapper around `scripts/dev_build_linux.sh`.
- `scripts/setup_android_emulator.sh` installs local Android SDK/emulator packages and creates the default AVD.
- `scripts/dev_build_android.sh` validates the Android toolchain and builds the APK target.
- `scripts/start_android_emulator.sh` starts the configured AVD without requiring an APK.
- `scripts/run_android_emulator.sh` installs and launches the latest built APK.
- `scripts/run_android_device.sh` installs and launches the latest built APK on a USB Android device.
- `scripts/android_build_install_run.sh` builds, installs, and launches in one step.

## Verification

A useful quick verification path is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
timeout 8s ./build/PrintFlow
scripts/run_tests.sh
```

The timeout command is only a smoke test for startup and QML/runtime initialization; it stops the GUI after a few seconds.

## Documentation

Additional project documentation is in `docs/`:

- `docs/RIP-App_Software_Design_Document.docx`
- `docs/RIP-APP_Flowchart.drawio`
- `docs/RIP-APP_Flowchart.png`
- `docs/README.md`

## License

MIT License. See `LICENSE`.

## Author

Created and maintained by **DeathGamer111**.
