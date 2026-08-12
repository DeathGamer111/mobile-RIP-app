# Disposable X-33 SDK debug harness

This standalone Linux ARM64/x86-64 tool loads the native legacy PROII/X-33 SDK,
inspects an existing packed PRN, and can stream its original raster without
involving Qt or PrintFlow. It defaults to `./test.prn` and the vendor demo's
packed-header conversion; pass `--prn FILE` to select another input.

Build and grant raw-network access:

```bash
./tools/nocai_x33_debug/build.sh --setcap
./build/nocai_x33_debug-arm64/bin/nocai_x33_debug
```

The build selects `DemoForARM64Linux*` on ARM64 and `DemoForX64Linux*` on
x86-64, then stages only the matching API and PrinterSocket libraries. ARM64
uses the SDK's documented plain exports by default. Use `--sdk-abi internal`
only to compare the vendor's internal mangled entry points.

Run the safe connection probe (search, select only when exactly one printer is
found, then connect; no print or maintenance commands):

```bash
timeout 30s ./build/nocai_x33_debug-arm64/bin/nocai_x33_debug --probe
```

The X-33 controller may advertise discovery before its command service is
ready; allow 60 seconds after printer power-on before the first Connect. Use
`--probe` only when the connection result itself is the test. This controller
does not reliably admit a new SDK process after a successful owner exits, even
when its close frame was acknowledged. For a print or maintenance test, launch
the interactive debugger directly and keep that same process alive through
Search, Select, Connect, and every subsequent command.

The 2026 ARM API contains three confirmed porting differences from the proven
x64 X-33 API: it emits controller command tag `0x49` instead of `0x47`, adds
`0x4943` instead of `0x474c` when validating response passwords, and
zero-extends signed 360-DPI head-alignment bytes. The last defect turns a valid
`-1` alignment into `255`, wraps the formatter's maximum offset, and can make a
nozzle pattern crash. The debugger enables the guarded ARM compatibility mode
automatically. The older explicit flag remains accepted by existing scripts:

```bash
timeout 30s ./build/nocai_x33_debug-arm64/bin/nocai_x33_debug \
  --arm-command-tag-compat --probe
```

The mode is ARM64-only. It validates the SDK Build ID and every original
instruction before correcting the response-password and signed-offset
instructions in process memory. It also corrects the protocol-233 tag at the
final socket boundary and restores the vendor send buffer immediately. It does
not modify either vendor library on disk. Uncorrected hardware commands are no
longer exposed because the printer does not acknowledge their Command 3 tag.

If a test exits abnormally, stop every PrintFlow/debugger process before
starting a fresh debugger lifecycle. Before Connect, the ARM debugger inspects
the selected PC-IP/printer-MAC POSIX semaphores used by `PrinterSocket.so` and
unlinks only zero-valued locks left by a killed process. It refuses cleanup if
another process has either Nocai library loaded. Do not run automatic reconnect
loops.

For the validated ARM API build, `close-control --execute` calls the inspected
internal `Net_CloseSock_Car(int)` path. It closes the persistent receiver/raw
socket and marks the importer for reopening. `lifecycle COUNT settle-ms MS
--execute` repeats connection-only Search -> Select -> Connect -> Close cycles;
it never prints or runs maintenance. The close call synchronously joins the
local receiver thread, and packet capture confirms that the printer
acknowledges its 26-byte close frame. This command is a lifecycle diagnostic,
not a promise that a fresh controller session will be available after an
abruptly terminated process. The debugger invokes the local close path after
failed Connect calls and during orderly teardown, including when Connect never
returned success.
Connect itself is attempted exactly once. The debugger does not synthesize a
controller close packet or retry behind the operator's back. A controller
session abandoned by SIGKILL may still require a printer reset; customer builds
avoid that normal path by retaining one SDK owner for the application lifetime
and translating SIGINT/SIGTERM into orderly shutdown.

At the prompt, run `help`. Read-only commands need no confirmation. Commands
that change printer state require a literal `--execute`; raw network packets
require `--unsafe`. `abort`, `pause`, and stop commands remain immediately
available. A normal test sequence is:

```text
inspect
search
select 0
connect
status
globals interesting
trace window 1900 2600
print --execute
```

On the validated ARM SDK, `connect` also opens a filtered process-owned packet
guard on the selected PC-IP interface. The guard retains the promiscuous
membership required by `PrinterSocket.so` through StartPrint and raster upload;
it captures no traffic and closes automatically with the debugger. The rebuilt
debugger therefore needs `cap_net_raw=ep`, just like PrintFlow.

For a single vendor-generated nozzle test after `status` reports standby, run:

```text
pattern 0 --execute
status
```

To test an iQueue-style per-job origin without changing PrintFlow, include
`xy-mm X Y` on the print command. The harness converts millimeters to the
controller's `uint32` hundredths-of-a-millimeter representation and verifies
the raw readback before uploading any raster data. On ARM64, the origin APIs
finish and settle for one second before the uninterrupted
`InitPrinter -> StartPrint` boundary. The x86-64 harness retains its validated
post-Init origin order:

```text
print vendor xy-mm 0 150 --execute
```

Use `start-probe --execute` to test only the SDK's
`InitPrinter -> StartPrint -> AbortPrint -> ClosePrint` boundary with the
canonical 720 x 1440 header. It submits no raster rows. Optional
`xy-mm X Y` and `settle-ms MS` clauses make it possible to distinguish an
origin-setting socket conflict from header or controller-state rejection. On
ARM64, an optional origin is read/set/verified and settled before
`InitPrinter`; use `start-probe xy-mm 0 0 settle-ms 1000 --execute` to mirror
PrintFlow's current print-start sequence.

The x64 SDK names the corresponding exported function
`API_SetPrintXYValue(uint32_t,uint32_t)`; its machine code stores both values
unchanged. The separate `xy set-raw` command remains available for low-level
experiments.

Each run writes timestamped text and JSONL diagnostics under `logs/` beside the
binary unless `--log-dir` is supplied. `core on` enables an unlimited core-size
limit without installing a fatal-signal handler.
