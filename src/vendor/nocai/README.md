# Vendor Direct-Print Adapter

This folder contains the source adapter only. The reviewed Linux runtime files
live centrally under `third_party/nocai/direct-print/`.

## Printer/SDK families

Direct printing is intentionally split by printer raster family:

- **Legacy standard CMYK (`X-33`)** uses the current PROII SDK and
  `PrintJobCMYK`.
- **Newer MultiInk (`X-36 Studio`, vendor model `X-36NC`)** uses `PrintJobMultiInk` and has a separate
  persisted SDK root. Auto-discovery is disabled for this family so it cannot
  accidentally load the X-33 SDK. Its direct path becomes available when the
  newer-model SDK and its adapter are supplied.

`directPrintSdkRootPath` remains the legacy SDK setting for backward
compatibility. `multiInkDirectPrintSdkRootPath` is reserved for the newer
package. Both raster pipelines depend on `IPrintOutputClient`, keeping the SDK
implementation outside the RIP code.

Full proprietary SDK drops, demos, other-platform libraries, generated
binaries, local test copies, and diagnostics remain ignored. CMake selects the
matching ARM64 or x86-64 pair from the central runtime tree, validates its ELF
architecture, and stages the Linux API and `PrinterSocketDLL` subtree. An
advanced `DIRECT_PRINT_SDK_ROOT` override may use the same
`linux/<arm64|x86_64>/` layout. The adapter resolves the documented C API first
and contains compatibility aliases for the July 2026 x86-64 package's mangled
`API_*` exports.

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
writes outside its native allocation. Single-direction output completes the
same raster with both supported header interpretations, so PrintFlow forces
left-to-right mode (`PrintDirection=1`) for legacy X-33 direct jobs only. The
X-36 Studio/MultiInk path is not affected by this workaround.

The vendor defines `EclosionGrade` as a unitless `0..3` feathering grade.
PrintFlow reserves `0` (Off) and presents the usable job choices as Low (`1`),
Medium (`2`), and High (`3`), defaulting new and legacy jobs to Medium.

X-33 linearization uses the same `TransferCurveSet` XML parser as X-36 Studio.
The resolved family-A/printer override is loaded for each job and its Cyan,
Magenta, Yellow, and Black LUTs are applied to the separated tone planes before
white generation and FM screening. A White curve is also applied when present;
otherwise its identity LUT leaves the generated white plate unchanged. The log
reports whether linearization was disabled, bypassed, loaded, and applied.

For each X-33 job, PrintFlow calls `InitPrinter` and converts the job model's
whole-millimeter `offset` to the controller's `uint32` hundredths of a
millimeter. The x86-64 path sends and verifies that value before `StartPrint`,
which is the physically validated legacy sequence. On ARM64, PrintFlow first
reads the active origin and skips a redundant setter. Both ARM origin APIs
open and close separate Command-205 sessions, so they complete and receive a
one-second socket-settle interval before `InitPrinter`; nothing is allowed
between the validated `InitPrinter -> StartPrint` boundary. A changed ARM
value is still sent and verified. Applying XY after `StartPrint` interrupts
the active data connection. Job Details and Imposition both persist the same
`offset`, so an imposition move automatically becomes the next X-33 print
origin. An unset offset requests raw `0, 0`.

`CPrinter_Model_X33` initializes with the generic two-head configuration and
maps that configuration to `HeadSelect=0`. Linux x86-64 physical job uploads
run in an isolated invocation of the PrintFlow executable because that vendor
SDK can fault inside `Andy_SwathProcessMul` instead of returning a failure.
ARM64 jobs remain in the GUI process and reuse the Printer Setup connection:
the X-33 does not accept an immediate reconnect after a successful ARM session
is released, and the supplied ARM `PrinterSocket.so` is ELF `NODELETE`. This
keeps one raw receiver and one controller session throughout Setup,
Maintenance, and Print while preserving the proven x86-64 isolation path.

Hardware traces established that the ARM vendor `PrinterSocket.so` only
receives the X-33's protocol-233 replies while the selected Ethernet interface
has a promiscuous packet membership. When a temporary `tcpdump` ended between
ConnectPrinter and StartPrint, the SDK immediately entered its unbounded data
socket reconnect loop. Before its one Connect attempt, PrintFlow now opens a
filtered `AF_PACKET` guard on the selected PC-IP interface and retains its
process-scoped `PACKET_MR_PROMISC` membership for the application lifetime.
The filter discards every packet on the guard socket; the SDK remains the only
consumer. This requires `CAP_NET_RAW` but does not globally change the link or
require `CAP_NET_ADMIN`. Repeating Search -> Select -> Connect still cannot
recover an already failed vendor socket, so PrintFlow performs one asynchronous
connection lifecycle and reports the result immediately. The x86-64 sequence
is unchanged.

After a successful ARM `ConnectPrinter`, PrintFlow treats that controller
initialization as process-lifetime state. Printer refresh and repeated Connect
buttons become idempotent and do not issue another Search/Select/Connect
lifecycle. PrintFlow makes one `StartPrint` attempt, matching the successful ARM
debugger sequence; it does not accumulate failed data sockets through automatic
retries. Zero-offset jobs also omit the otherwise unnecessary Command-205
origin query so the validated `InitPrinter -> StartPrint` boundary remains
uninterrupted. Nonzero user offsets retain the explicit origin path. The x86-64
lifecycle remains unchanged.

The validated ARM API also exports an internal `Net_CloseSock_Car(int)` helper
that routes through its persistent `PrinterSocketDLLImporter::CloseSocket`.
The public SDK has no disconnect API, so PrintFlow resolves this helper only
after the exact ARM Build ID and compatibility instructions have passed their
existing guards. A failed Connect and orderly SDK shutdown explicitly close
the local control importer. A raster/data-channel failure aborts and closes the
job but preserves the already working control session; dropping that session
was the reason a print error used to make Setup and Maintenance unusable.
PrintFlow never performs a hidden reconnect loop. Preventing an ARM
GUI-to-worker handoff is still mandatory because a new process cannot inherit
the GUI process's live SDK state during a print submission.

`PrinterSocket.so` serializes raw channels with POSIX semaphores named from the
PC IP, printer MAC, and channel. SIGKILL leaves a held semaphore at zero, so a
later process times out locally and reports the misleading `open socket fail`
message even when packet capture shows normal printer replies. Immediately
before the one ARM Connect attempt, PrintFlow inspects only the selected
printer's matching semaphores and unlinks zero-valued entries. Cleanup is
refused while another process has the Nocai API or PrinterSocket library
loaded. Available locks and unrelated `/dev/shm` entries are never removed.

A controller session abandoned by SIGKILL is separate from the local semaphore
problem and may still require a printer reset. The reliability strategy is to
prevent that state: one persistent `PrintFlowPrinterService` process owns the
ARM SDK, Setup, Maintenance, and printing reuse that owner, print failures
retain its control channel, and SIGINT/SIGTERM follow orderly Qt teardown. The
GUI can close and reopen without replacing the SDK process. PrintFlow does not
send speculative controller recovery frames or repeatedly call Connect.

Hardware testing also established that the printer can answer discovery before
its command service is ready during boot. Allow 60 seconds after power-on before
the first Connect attempt. More importantly, after one SDK process connects and
exits, this controller may refuse a new process even though it acknowledged the
old process's close frame. Connection probes must therefore not run immediately
before PrintFlow: the printer service must be the first post-boot owner and
retains that owner across application lifetimes.

The desktop entry point also enforces one GUI process per user. Other trusted
clients may use the public service API without loading separate raw receivers
or competing for the controller. The intentional x86-64 isolated print worker
is spawned and coordinated by the service.

On Linux, `SIGINT` and `SIGTERM` are translated into a normal Qt shutdown so
SDK objects and their socket-library globals reach process teardown. This
covers terminal stops, service stops, and orderly system shutdown instead of
abruptly abandoning the controller session.

The ARM X-33 upload path also leaves the engine's persistent `JobSettings`
unchanged. The vendor documentation places `SetJobSettings` under parameter
setting rather than print control and notes that changing parameters requires a
reconnect. Invoking it after a successful connection caused repeated Command
205 connection attempts, destroyed the active session, and prevented the
documented `InitPrinter -> StartPrint` lifecycle from beginning. The x86-64
settings path is unchanged.

The Linux `PrinterSocket` implementation opens raw sockets. On ARM, the service
also retains the process-scoped promiscuous membership described above. A local
hardware-test build therefore needs `CAP_NET_RAW`, for example:

```bash
sudo setcap cap_net_raw+ep /absolute/path/to/PrintFlowPrinterService
```

Grant that capability only to a trusted, locally built executable. Rebuilding
or replacing the executable removes the file capability.

The inspected ARM64 X-33 API build requires three narrow compatibility
corrections established against the working x86-64 exchange and a successful
ARM print. It generates protocol-233 frames with tag `0x49` instead of `0x47`,
validates the response with `0x4943` instead of the printer's `0x474c`, and
zero-extends four signed one-byte head offsets. On Linux ARM64, PrintFlow
validates the exact API Build ID and instruction layouts, corrects the wire tag
on a copied transmit buffer, and updates only the loaded code mapping for the
other two instructions. The vendor file is never modified, unknown ARM builds
or layouts are rejected, and the x86-64 path is unchanged.

The executable exports and installs the SDK's four host print callbacks
(`StartPrint`, `WriteRipData`, `EndRipData`, and `ExitPrinter`) on ARM as well as
the legacy x86-64 compatibility path. The ARM nozzle/alignment pattern engine
uses those callbacks even when the documented `API_*` entry points are loaded.

The packed X-33 PRN header and the `API_StartPrint` structure are both 48 bytes
but are not layout-compatible. The file stores Colors and Bits as adjacent
16-bit values; the SDK ABI widens each to 32 bits. The known-good debugger PRN
therefore reaches StartPrint as `Bits=0`, `Pass=1`, and `VsdMode=1`. ARM direct
printing performs that member-wise widening instead of the old raw memcpy,
which incorrectly sent `Bits=1`, `Pass=1`, and `VsdMode=0`. The proven x86-64
mapping is unchanged.

Maintenance commands initiated by the QML maintenance page run through a
single background queue. Controls and status polling pause while an SDK call is
active, so the vendor's internal retry loop cannot block the GUI event thread.

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
state. PrintFlow therefore presents this operation as **Flushing**: rapidly
firing selected nozzles at the maintenance station to keep them wet or clear
light drying. The original vendor names remain in the adapter so symbol mapping
stays unambiguous.

The documented nozzle check is alignment-pattern enum value
`E_NOZZLE_CHECK` (`0`). PrintFlow exposes it as a dedicated maintenance action
instead of requiring an operator to enter the numeric pattern type.

The C++ adapter sources in this directory are part of the application and should remain tracked.
