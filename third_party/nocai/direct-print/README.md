# Nocai direct-print runtime

This directory is the single source of proprietary direct-print runtime files
used by Linux builds. It intentionally contains no demos, sample executables,
logs, captures, Windows/macOS binaries, or unmatched 32-bit libraries.

```text
linux/
|-- arm64/
|   |-- libSYPrintAPIforPROII.so
|   `-- PrinterSocket.so
`-- x86_64/
    |-- libSYPrintAPIforPROII.so
    `-- PrinterSocket.so
```

CMake selects one directory from the target processor, validates both ELF
machine types, and stages the selected pair beside `PrintFlowPrinterService`.
The staged socket is placed at the vendor-required runtime path
`PrinterSocketDLL/linux/<arm64|x64>/PrinterSocket.so`. Native packages contain
only the selected architecture.

The source files and SHA-256 checksums are:

| Target | File | Vendor source | SHA-256 |
| --- | --- | --- | --- |
| ARM64 | `libSYPrintAPIforPROII.so` | `DemoForARM64Linux-260612` | `ae901072ee37577fd723c47fc6bb49d7556f33e934a80a92d79447df34fe82f8` |
| ARM64 | `PrinterSocket.so` | `DemoForARM64Linux-260612` | `b0e55d7e8849d73a88c2a03e3effd821112ae8ac89d6013d1f2d5477b23062f1` |
| x86-64 | `libSYPrintAPIforPROII.so` | `DemoForX64Linux_PROII20260722` | `ed842950e0a1a6d978b9d4db171f9e0bb31f06a3c2f67ad28d8ed12b054d9cac` |
| x86-64 | `PrinterSocket.so` | `DemoForX64Linux_PROII20260722` | `d639bfd064080c3cfe4820d0ac82db20e3dbf2402d298580667de0e864db9530` |

No working 32-bit x86 pair was supplied: the vendor drops contain an x86
`PrinterSocket.so` but no matching x86 print API. PrintFlow therefore supports
the complete x86-64 and ARM64 Linux pairs only.

These files remain proprietary vendor components. Confirm redistribution terms
before publishing or delivering packages outside the authorized product scope.
