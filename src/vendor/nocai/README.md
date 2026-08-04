# Vendor Direct-Print Adapter

This folder contains the source adapter only.

## Printer/SDK families

Direct printing is intentionally split by printer raster family:

- **Legacy standard CMYK (`X-33`)** uses the current PROII SDK and
  `PrintJobCMYK`.
- **Newer MultiInk (`X-36NC`)** uses `PrintJobMultiInk` and has a separate
  persisted SDK root. Auto-discovery is disabled for this family so it cannot
  accidentally load the X-33 SDK. Its direct path becomes available when the
  newer-model SDK and its adapter are supplied.

`directPrintSdkRootPath` remains the legacy SDK setting for backward
compatibility. `multiInkDirectPrintSdkRootPath` is reserved for the newer
package. Both raster pipelines depend on `IPrintOutputClient`, keeping the SDK
implementation outside the RIP code.

Proprietary SDK drops, demo packages, shared libraries, generated binaries, and local test copies must remain local and ignored by git. Do not commit vendor SDK files here.

Set `DIRECT_PRINT_SDK_ROOT` to the local vendor SDK path before configuring or building when direct-print SDK packaging is needed:

```bash
export DIRECT_PRINT_SDK_ROOT=/path/to/local/vendor/sdk/drop
```

Archive drops can be used without extracting them into the source tree:

```bash
export DIRECT_PRINT_SDK_ARCHIVE=/path/to/local/vendor/sdk/drop.zip
```

CMake validates the target ELF architecture and stages the matching Linux API and `PrinterSocketDLL` subtree. The adapter resolves the documented C API first and contains compatibility aliases for the July 2026 x86-64 package's mangled `API_*` exports.

That x86-64 package also imports four plain wrappers that it does not define.
On Linux x86-64, PrintFlow exports the mappings confirmed from the matching ARM
SDK:

- `StartPrint` → `API_StartPrint`
- `WriteRipData` → `API_PrintALine`
- `EndRipData` → `API_EndPrint`
- `ExitPrinter` → `API_ClosePrint`

These callbacks are installed only while the mangled-ABI SDK is loaded. The
executable must retain `ENABLE_EXPORTS` so the vendor library can resolve them.

## X-33 raster upload contract

The legacy X-33 interface accepts exactly one color plane of `BytesPerLine`
bytes per `API_PrintALine` call. A four-color job sends `Height * 4` calls in
per-row `Y`, `M`, `C`, `K` order. Do not combine all four planes into one
`Colors * BytesPerLine` call. After the final plane line, the documented API
sequence is `API_EndPrint` immediately followed by `API_ClosePrint`.

An X-33 white job keeps that same 48-byte header and lifecycle, sets
`Colors=6`, and sends `Height * 6` calls in per-row `Y`, `M`, `C`, `K`, `W`,
`W` order. PrintFlow builds one logical screened white plate and references it
for both physical W planes, matching the Nocai printing-process document and
the `CPrinter_Model_X33` iQueue integration. White `Off` continues to use the
proven four-plane raster contract. Auto Underbase, Flood, and an
external grayscale Plate are supported; `WCSequence` controls white-under
versus color-under ordering in the legacy job settings.

The July 2026 Linux x86-64 SDK has a reproducible defect in its X-33
bidirectional (`PrintDirection=0`) swath formatter. Near the sixth swath it
computes a negative reverse-direction length, treats it as unsigned, and
writes outside its native allocation. Left-to-right mode
(`PrintDirection=1`) completes the same raster with both supported header
interpretations, so PrintFlow forces that direction for legacy X-33 direct
jobs only. The X-36NC/MultiInk path is not affected by this workaround.

For each X-33 job, PrintFlow calls `InitPrinter`, converts the job model's
whole-millimeter `offset` to the controller's `uint32` hundredths of a
millimeter, sends it through `SetPrintXYValue`, verifies it with
`GetPrintXYValue`, and then calls `StartPrint`. This order and unit conversion
were confirmed on physical hardware: applying XY after `StartPrint` interrupts
the active data connection, while applying it before `InitPrinter` can be
superseded. Job Details and Imposition both persist the same `offset`, so an
imposition move automatically becomes the next X-33 print origin. An unset
offset sends raw `0, 0`, explicitly returning the next job to the origin.

`CPrinter_Model_X33` initializes with the generic two-head configuration and
maps that configuration to `HeadSelect=0`. Linux x64 physical job uploads run
in an isolated invocation of the PrintFlow executable because this vendor SDK
can fault inside `Andy_SwathProcessMul` instead of returning a failure. A
vendor fault therefore fails the job without terminating the GUI process.

The Linux `PrinterSocket` discovery implementation opens raw sockets. A local
hardware-test build therefore needs `CAP_NET_RAW`, for example:

```bash
sudo setcap cap_net_raw+ep /absolute/path/to/PrintFlow
```

Grant that capability only to a trusted, locally built executable. Rebuilding
or replacing the executable removes the file capability.

The adapter captures the vendor library's stdout while its API is active.
Routine interface enumeration, ARP table dumps, port-allocation messages, and
known localized success messages are discarded; checked API results and all
PrintFlow diagnostics remain in English on the normal Qt log channels.

The Linux development build script applies this capability after linking. It
also creates an idempotent NetworkManager IPv4 link-local profile when exactly
one wired interface has carrier and no IPv4 address. Use
`PRINTFLOW_PRINTER_INTERFACE` to select a specific printer-facing interface or
`PRINTFLOW_CONFIGURE_PRINTER_NETWORK=0` to opt out.

## Maintenance terminology

The vendor's public wrapper calls the operation `SpitPrintHead`, while the
exported implementation is `API_StartSpitInk`. The supplied demo also names the
associated maintenance position `FlashSprayHeight` and references a hold-fire
state. PrintFlow therefore presents this operation as **Flash Spray**: rapidly
firing selected nozzles at the maintenance station to keep them wet or clear
light drying. The original vendor names remain in the adapter so symbol mapping
stays unambiguous.

The documented nozzle check is alignment-pattern enum value
`E_NOZZLE_CHECK` (`0`). PrintFlow exposes it as a dedicated maintenance action
instead of requiring an operator to enter the numeric pattern type.

The C++ adapter sources in this directory are part of the application and should remain tracked.
