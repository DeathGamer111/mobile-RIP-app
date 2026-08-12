#pragma once

#include <cstdint>

namespace NocaiDirectPrintCompatibility {

using StartPrintFn = int (*)(void*);
using PrintALineFn = int (*)(char*, std::uint32_t);
using NoArgFn = int (*)();

struct Callbacks
{
    StartPrintFn startPrint = nullptr;
    PrintALineFn printALine = nullptr;
    NoArgFn endPrint = nullptr;
    NoArgFn closePrint = nullptr;
};

// The Linux SDKs call four host symbols from vendor-generated pattern jobs.
// x86-64 exposes them as lazy imports; ARM also needs them while using the
// documented API_* entry points. They forward to the resolved API functions.
bool install(const void* owner, const Callbacks& callbacks);
void uninstall(const void* owner);
bool isInstalledFor(const void* owner);

} // namespace NocaiDirectPrintCompatibility
