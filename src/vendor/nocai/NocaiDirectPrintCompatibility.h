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

// The July 2026 Linux x86-64 SDK omits four plain C wrappers that its
// alignment-pattern implementation still imports. The ARM SDK shows that
// those wrappers forward directly to the corresponding API_* functions.
bool install(const void* owner, const Callbacks& callbacks);
void uninstall(const void* owner);
bool isInstalledFor(const void* owner);

} // namespace NocaiDirectPrintCompatibility
