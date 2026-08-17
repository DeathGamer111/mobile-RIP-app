# PrintFlow printer service API

## Purpose

`PrintFlowPrinterService` is the only Linux process allowed to load the Nocai
API and `PrinterSocket.so`. It starts on the first client request, listens on a
user-only local socket, serializes every controller operation, and remains
alive across GUI restarts. A stable SDK process therefore owns discovery,
Connect, raster upload, and maintenance for the whole user session.

The service deliberately does not start a second backend to probe health. On
ARM64, creating a replacement vendor process can leave the X-33 controller
bound to the old owner even after an apparently successful close frame. If the
service itself is terminated during a controller operation, the printer may
still require controller recovery; the design prevents normal GUI closes and
reopens from causing that state.

## Temporary Android bridge

Until an Android/Bionic vendor SDK is available, the Android app can reuse the
same protocol through an ADB reverse tunnel. Start the opt-in bridge with:

```bash
./scripts/run_android_printer_bridge.sh
```

The service accepts bridge traffic only on Linux loopback port `19733`; ADB
maps Android `127.0.0.1:19733` to it. This enables real SDK discovery, status,
maintenance, and nozzle testing from Android without opening a printer-control
listener on the LAN. It is a development bridge, not an Android SDK replacement
and not proof of Android-native printer transport.

## Public C++ API

The installed `PrintFlowPrinterApi` library provides:

- `IPrintOutputClient.h`: vendor-neutral raster and settings structures.
- `PrinterServiceClient.h`: discovery, selection, connection, maintenance,
  status, and `submitPreparedJob()`.
- `PrinterServiceProtocol.h`: versioned framing and serialization helpers for
  trusted local integrations that need lower-level access.

Use the same source API on x86-64 and ARM64. Only the package's native ELF
binaries and proprietary SDK payload differ.

```cmake
find_package(PrintFlowPrinterApi 1 CONFIG REQUIRED)
target_link_libraries(my_print_client PRIVATE PrintFlow::PrinterApi)
```

```cpp
#include <PrinterServiceClient.h>

PrinterServiceClient printer;
if (printer.refreshPrinters() && printer.choosePrinter(0) &&
    printer.connectPrinter()) {
    // Submit a DirectPrintRaster, query status, or invoke maintenance.
}
```

Constructing the client performs a protocol handshake and starts the service
when needed. Destroying a client never stops the service. Requests from all
clients are serialized in the service event loop, so vendor calls cannot run
concurrently.

## Protocol v1

Messages use a 32-bit length-prefixed Qt `QDataStream` frame containing:

- magic `PFPS` (`0x50465053`)
- protocol version `1`
- a `QVariantMap` request or response

The service rejects corrupt, oversized, and version-mismatched frames. The
socket is created with user-only access under `XDG_RUNTIME_DIR`; set
`PRINTFLOW_PRINTER_SERVICE_SOCKET` only for isolated tests. Raster payloads are
flattened in row-major/channel-order form and reconstructed into service-owned
memory before the vendor backend is called. Vendor ABI structs and pointers
never cross the process boundary.

The public commands are `ping`, `configure`, `refreshPrinters`,
`choosePrinter`, `connectPrinter`, `reconnectPrinter`, `statusText`, `getPrinterStatus`,
`getPrinterInfo`, `abortPrint`, `pausePrint`, `continuePrint`, `maintenance`,
and `submitJob`. `shutdown` is an administrative lifecycle command used during
development rebuilds and package upgrades.

`PrinterServiceClient` also exposes asynchronous, user-requested recovery
operations for reconnecting the current printer and gracefully restarting the
service. Restart is deliberately not automatic: it closes the current SDK
owner, starts one replacement, and performs one Search -> Choose -> Connect
attempt. The client can return the final 512 KiB of the service diagnostic log
for the Printer Settings troubleshooting UI.

## Runtime and diagnostics

The client searches for `PrintFlowPrinterService` beside the application, in
the standard PrintFlow `libexec` locations, and on `PATH`. Set
`PRINTFLOW_PRINTER_SERVICE_EXECUTABLE` to an explicit executable for testing.

Useful controls:

```bash
./build/PrintFlowPrinterService --ping
./build/PrintFlowPrinterService --shutdown
getcap ./build/PrintFlowPrinterService
```

The required capability is `cap_net_raw=ep`. The development build applies it
after every relink. The Debian package's post-install hook applies it to
`/usr/bin/PrintFlowPrinterService`. Service and vendor output is appended to
`~/.local/share/PrintFlow/logs/printer-service.log` and rotated at 10 MiB.

## Packaging model

There is one unified API and package layout, built natively for each host:

```text
PrintFlow
PrintFlowPrinterService
libPrintFlowPrinterApi.so.1
libSYPrintAPIforPROII.so
PrinterSocketDLL/linux/<x64|arm64>/PrinterSocket.so
```

A package never carries both proprietary SDK architectures. x86-64 retains the
known mangled-symbol compatibility resolver and isolated vendor upload worker;
ARM64 retains the validated X-33 wire-tag, signed-offset, stale-local-lock, and
process-scoped receive corrections inside the persistent service.
