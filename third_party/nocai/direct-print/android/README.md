# Android direct-print SDK input

The Android direct-print package is not present in the supplied vendor drops.
To enable discovery, maintenance, and direct printing on an ARM64 Android
device, obtain a complete Android/Bionic build and place it here:

```text
android/
`-- arm64/
    |-- libSYPrintAPIforPROII.so
    `-- libPrinterSocket.so
```

Both files must be AArch64 Android NDK libraries. A Linux ARM64 library is not
compatible: dependencies such as `libc.so.6`, `libstdc++.so.6`,
`libgcc_s.so.1`, or `ld-linux-aarch64.so.1` identify a glibc build that Android
cannot load. An Android C++ SDK should use the same NDK `libc++` runtime as the
app, normally `libc++_shared.so`.

Validate a new drop before building:

```bash
scripts/validate_android_direct_print_sdk.sh \
  third_party/nocai/direct-print arm64-v8a
```

The validator checks the pair, architecture, Android runtime dependencies,
required PrintFlow API symbols, and the nozzle/alignment-pattern entry point.
Once it passes, build and install on a physical ARM64 Android device:

```bash
DIRECT_PRINT_SDK_ROOT="$PWD/third_party/nocai/direct-print" \
  ./Dev_Build_App.sh --android-device
```

The emulator validates the UI and Android runtime but is not a substitute for
the physical printer test: it is x86-64, NATed, and does not share the Android
device's Ethernet/Wi-Fi interface or ARM64 native ABI.
