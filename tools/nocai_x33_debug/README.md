# Disposable X-33 SDK debug harness

This standalone Linux x86-64 tool loads the legacy PROII/X-33 SDK, inspects an
existing packed PRN, and can stream its original raster without involving Qt or
PrintFlow. It defaults to `/home/xante-admin/Downloads/TestPrint.prn` and the
vendor demo's packed-header conversion.

Build and grant raw-network access:

```bash
./tools/nocai_x33_debug/build.sh --setcap
./build/nocai_x33_debug/bin/nocai_x33_debug
```

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

To test an iQueue-style per-job origin without changing PrintFlow, include
`xy-mm X Y` on the print command. The harness converts millimeters to the
controller's `uint32` hundredths-of-a-millimeter representation, applies the
value after `InitPrinter` but immediately before `StartPrint`, and verifies the
raw readback before uploading any raster data:

```text
print vendor xy-mm 0 150 --execute
```

The x64 SDK names the corresponding exported function
`API_SetPrintXYValue(uint32_t,uint32_t)`; its machine code stores both values
unchanged. The separate `xy set-raw` command remains available for low-level
experiments.

Each run writes timestamped text and JSONL diagnostics under `logs/` beside the
binary unless `--log-dir` is supplied. `core on` enables an unlimited core-size
limit without installing a fatal-signal handler.
