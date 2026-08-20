# PrintFlow

> Active Qt/QML raster image processing application for desktop print-job preparation and output generation.

PrintFlow is a Qt 6 raster image processing application for preparing print jobs, previewing and editing artwork, managing printer settings, and generating raster/PRN output through pluggable vendor backends.

The current codebase supports Linux desktop development and Android APKs with CMake, Qt, ImageMagick, and Little CMS. Linux output uses CUPS; Android uses either the temporary printer-service bridge or a future Android-native vendor SDK.

## Current Status

- Active branch: `master`
- Build system: CMake
- UI framework: Qt Quick/QML
- Main development build script: `./Dev_Build_App.sh`
- Primary executable target: `PrintFlow`
- Linux desktop status: builds and runs with the native RIP pipeline, CUPS integration, ImageMagick, Little CMS, theme resources, string resources, and the local test suite.
- Android APK status: API 36 x86-64 emulator and ARM64 device APKs build successfully with native ImageMagick image editing and RIP code. Import, preview, resize, and save are emulator-verified. Native Android vendor communication still requires an Android/Bionic SDK; the temporary ADB printer bridge routes the app to the Linux SDK for testing.
- Base product identity: `PrintFlow`; customer or vendor display branding belongs in theme configuration.

## Features

- Print job management with Qt model roles and JSON persistence.
- Image loading, validation, metadata extraction, and PDF preview rendering through ImageMagick.
- Image editing tools for crop, rotate, flip, resize, color adjustment, blur, sepia, vignette, swirl, implode, text, rectangle drawing, undo, and redo.
- Job Details with repeatable artwork replacement, live preview refresh, image metadata, output settings, and persisted job options.
- Imposition view for positioning artwork on the selected media and carrying the same origin into direct print.
- Media-size selection for ISO A0-A6, Letter, Legal, Tabloid, common 12x18 through 32x48 sign sizes, 24x36 Coroplast, and custom dimensions.
- Printer setup flow for desktop printers, prepared PRN output, and optional vendor direct-print workflows.
- ICC profile handling through Little CMS, including bundled CMYK and multi-ink output profiles.
- Device-aware Color Management: X-33 exposes CMYK and supported white controls while Multi Ink-only thresholds remain disabled; X-36 Studio exposes its selected multi-channel controls.
- PRN generation with 2-bit dot classification, stochastic screening, and dot promotion controls.
- Shared bounded-memory X-33 and X-36 Studio raster engine. It preserves the
  existing imaging order, screens channel-major strips of at most 128 rows,
  spills ImageMagick caches and canonical CMYK data to application scratch
  storage, and finalizes a checksummed spool before the printer can start.
- Density-aware artwork sizing that preserves embedded input DPI across previews, imposition, X-33 output, and X-36 Studio output; density-less files retain printer-family compatibility defaults.
- Multi-ink PRN generation with 4, 5, 6, 7, 8, and 10 channel ink layouts.
- Linearization support using bundled XML presets.
- Runtime asset preparation for bundled ICC profiles, linearization files, logo assets, and local blue-noise masks.
- Default, Nocai, and Xante/iQueue build themes plus custom theme JSON support.
- Central English and Simplified Chinese string tables with automated key-parity checks for translatable UI text.

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
|-- src/platform/android/          Android output integration and optional fallback facades
|-- src/platform/desktop/          Linux desktop integrations such as CUPS
|-- src/printerservice/            Persistent service, versioned IPC, and public client API
|-- src/rip/                       Native RIP, color, screening, and PRN pipeline
|-- src/third_party/stb/           Third-party single-header image loader
|-- src/vendor/nocai/              Isolated direct-print vendor adapter
|-- third_party/imagemagick/       Curated x86-64 and ARM64 Android runtime
`-- third_party/nocai/direct-print Minimal direct-print SDK payloads
```

## Tests

Run the local Linux test suite with:

```bash
scripts/run_tests.sh
```

The script configures `build-tests`, builds the app and test executables, then runs `ctest --output-on-failure`. Tests cover the job model, asset/platform helpers, string-table parity, theme loading, RIP pipeline behavior, Android-safe stubs, vendor isolation, canonical SDK paths, and ARM64/x86-64 ELF validation. Tests do not require blue-noise mask fixtures or an Android device.

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

- Output ICC profiles for 4-color, 8-color, X-33 1440 plain default, 1440 plain neutral, and generic CMYK workflows. The bundled X-33 profile is atomically refreshed in runtime storage when its packaged contents change.
- `sRGBProfile.icm`
- Linearization XML presets for X-36 Studio 4/8-color output and the X-33 1440-DPI pipeline. The X-33 profile and linearization are seeded as that printer's defaults while valid user overrides remain intact.
- `logo.png`

Large blue-noise mask directories are intentionally ignored by Git:

```text
resources/assets/blue_noise_mask_*/**
```

For local builds that generate multi-ink output, the app expects `resources/assets/blue_noise_mask_512_12000/` to exist locally with the mask TIFF files used by `scripts/dev_build_linux.sh`. The masks can be embedded into Qt resources with `-DRIP_EMBED_BLUE_NOISE_MASKS=ON`. Desktop builds leave them as local runtime assets to avoid very large generated resource objects. Android builds embed them by default because a clean mobile installation has no separately provisioned runtime-assets directory; set `RIP_EMBED_BLUE_NOISE_MASKS=OFF` only for UI-only development APKs.

Theme assets live under `resources/themes/<theme-id>/assets/` or `resources/vendor/<vendor-id>/assets/` and are compiled into Qt resources when referenced by theme JSON. Raw vendor drops, demo programs, and diagnostics stay ignored; the four reviewed Linux runtime libraries are centralized under `third_party/nocai/direct-print/`.

## Hybrid raster and spool output

Production X-33 and X-36 Studio output prioritizes the faster full-frame raster
path when its conservative native-memory estimate is no more than 4 GiB. Jobs
above that limit automatically use the bounded strip path. If a fast-path
allocation fails, the job safely retries with bounded strips.

The bounded path uses a 512 MiB RIP working-memory budget: 192 MiB for
ImageMagick memory, 128 MiB for its mapped cache, 128 MiB for native strip and
mask buffers, and 64 MiB of contingency. Before bounded rasterization, the app
reports and validates scratch-space requirements for ImageMagick caches,
canonical CMYK/plate data, packed spool data, an optional PRN, and a 20 percent
safety margin. Insufficient disk, allocation failures, cancellation, and cache
limit failures return a normal job error and remove partial output.

Raster rows are written immediately to a versioned, SHA-256-validated,
channel-major spool. Direct SDK workers open that spool rather than receiving a
reconstructed full image. Printer-service clients upload sequential 1 MiB
chunks; the service verifies offsets, metadata, size, and checksum before it
calls `StartPrint`. The prior one-frame protocol remains available for older
small clients.

The normal test suite exercises strip heights of 1, 17, and 128 rows and checks
spool/PRN equivalence, corruption rejection, physical plane order, and streamed
service failure cases. The real ONYX evaluation image is an opt-in resource test:

```bash
QTEST_FUNCTION_TIMEOUT=3600000 \
  PRINTFLOW_ONYX_EVAL_IMAGE=/path/to/Quality-Evaluation-CMYK.tif \
  build-tests/tests/PrintFlowCMYKAssetManagerTest onyxEvaluationImageStaysBounded
```

## Direct-print SDK

The Linux SDK is maintained in one minimal, architecture-keyed location:

```text
third_party/nocai/direct-print/
`-- linux/
    |-- arm64/{libSYPrintAPIforPROII.so,PrinterSocket.so}
    `-- x86_64/{libSYPrintAPIforPROII.so,PrinterSocket.so}
```

CMake chooses the pair from the target processor, validates both ELF machine types, stages the API beside `PrintFlowPrinterService`, and recreates the vendor-required `PrinterSocketDLL/linux/<arm64|x64>/PrinterSocket.so` runtime path. Development builds, the standalone X-33 debugger, AppImages, and Debian packages all consume this same location. Native packages include only their target architecture; the GUI never loads the proprietary library directly.

The original demo trees, demo executables, other operating-system libraries, unmatched 32-bit socket, archives, and packet-capture diagnostics are not build inputs. See `third_party/nocai/direct-print/README.md` for provenance, checksums, and the supported architecture boundary. `DIRECT_PRINT_SDK_ROOT` remains available only as an advanced override and must use the same `<platform>/<architecture>` layout.

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

Android builds use the native ImageMagick 7.1.2-29 Magick++ stack for image editing, color handling, and RIP processing. The curated Q16 HDRI runtime supports `x86_64` emulator and `arm64-v8a` device builds and lives under `third_party/imagemagick/android`. CUPS remains behind an Android output implementation. Until the vendor supplies Android/Bionic printer libraries, Android builds use the versioned PrintFlow printer-service client over a loopback-only ADB reverse tunnel.

Required environment variables for emulator builds:

```bash
export QT_ANDROID_CMAKE="$HOME/Qt/<version>/android_x86_64/bin/qt-cmake"
export ANDROID_SDK_ROOT="$PWD/.android-sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/<version>"
```

Optional advanced override for a future complete Android SDK tree:

```bash
export DIRECT_PRINT_SDK_ROOT="/path/to/sdk/root"
```

Android packaging requires `android/<arm64|x86_64>/libSYPrintAPIforPROII.so` and `android/<arm64|x86_64>/libPrinterSocket.so` below that root. The bundled Linux/glibc libraries are deliberately excluded from APKs even when the CPU architecture matches. If `.android-env` points at the x86_64 Qt kit, the build script switches to the sibling `android_arm64_v8a` kit when it exists.

Validate an Android SDK before building:

```bash
scripts/validate_android_direct_print_sdk.sh \
  third_party/nocai/direct-print arm64-v8a
```

The device build enables strict SDK packaging only after this check passes. It rejects missing pairs, wrong ELF architectures, Linux/glibc dependencies, and missing required API symbols. Android manifests include Internet, network-state, Wi-Fi-state, and multicast permissions for local printer discovery. API 36 and lower use `INTERNET` for LAN access; a future target-SDK 37 update will also need the Android 17 local-network runtime permission flow.

The temporary bridge exercises the real Linux vendor SDK but does not validate Android-native printer networking. Start it after the Linux app and Android APK have been built:

```bash
./scripts/run_android_printer_bridge.sh
```

The script selects one attached Android target (or `ANDROID_SERIAL`), installs an ADB reverse mapping from Android `127.0.0.1:19733` to the same loopback-only port on Linux, and starts `PrintFlowPrinterService` with its opt-in bridge listener. No printer-control TCP port is exposed to the LAN. The Android app can then use setup, status, maintenance, and nozzle-check calls through the real SDK. Native image processing is enabled in the default APK; set `RIP_EMBED_BLUE_NOISE_MASKS=ON` when building an APK that must rasterize a complete print job locally.

The Android build runs `scripts/setup_android_imagemagick.sh` automatically. That helper downloads pinned x86-64 and ARM64 release archives, verifies their SHA-256 checksums, and stages only Magick++, MagickWand, MagickCore, and OpenMP. It deliberately excludes the command-line program, demos, static libraries, and the release archive's duplicate C++ runtime. Set `RIP_ANDROID_ENABLE_RIP_PROCESSING=OFF` only when a boot-only fallback APK is required.

An emulator is suitable for Qt/QML, application-flow, and bridged maintenance testing. Final direct printer validation must use a physical ARM64 Android device on the printer-facing network with the Android/Bionic vendor SDK. See `third_party/nocai/direct-print/android/README.md` for the required vendor payload.

Install the local Android SDK command-line tools, emulator packages, and a Pixel-style AVD:

```bash
scripts/setup_android_emulator.sh
```

Build the APK:

```bash
scripts/dev_build_android.sh
```

Build a full local-rasterization APK with the blue-noise print masks:

```bash
RIP_EMBED_BLUE_NOISE_MASKS=ON scripts/dev_build_android.sh
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

The Android build defaults to `ANDROID_ABI=x86_64` for emulator testing on Linux and still compiles against API 36. The x86_64 emulator requires KVM/VM acceleration with writable `/dev/kvm`; without it, `scripts/start_android_emulator.sh` and `scripts/run_android_emulator.sh` fail early with a host-setup message. The local `PrintFlow_Pixel_1080p_API35` AVD uses the stable API 35 Google image and is tuned for this test box with 3 guest CPU cores, 6 GB RAM, host GPU rendering, and a 1080x1920 display. API 35 avoids the recurring System UI watchdog failures observed with the API 36 emulator image while continuing to run the API 36-compiled APK. Set `EMULATOR_HEADLESS=1` only for unattended runs.

## Development Notes

- `build/` is ignored and should not be committed.
- The generated Qt resource output under `.rcc/` is ignored.
- The blue-noise mask source directory is ignored because the masks are large local runtime assets.
- Only the four curated Linux SDK runtime files belong under `third_party/nocai/direct-print`; full vendor drops and diagnostics remain ignored.
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
