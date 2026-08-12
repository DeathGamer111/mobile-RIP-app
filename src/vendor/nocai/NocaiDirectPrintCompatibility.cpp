#include "NocaiDirectPrintCompatibility.h"

#include <atomic>
#include <mutex>

namespace {

std::mutex g_lifecycleMutex;
const void* g_owner = nullptr;
std::atomic<NocaiDirectPrintCompatibility::StartPrintFn> g_startPrint{nullptr};
std::atomic<NocaiDirectPrintCompatibility::PrintALineFn> g_printALine{nullptr};
std::atomic<NocaiDirectPrintCompatibility::NoArgFn> g_endPrint{nullptr};
std::atomic<NocaiDirectPrintCompatibility::NoArgFn> g_closePrint{nullptr};

} // namespace

namespace NocaiDirectPrintCompatibility {

bool install(const void* owner, const Callbacks& callbacks)
{
    if (!owner || !callbacks.startPrint || !callbacks.printALine ||
        !callbacks.endPrint || !callbacks.closePrint) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (g_owner && g_owner != owner)
        return false;

    g_startPrint.store(callbacks.startPrint, std::memory_order_release);
    g_printALine.store(callbacks.printALine, std::memory_order_release);
    g_endPrint.store(callbacks.endPrint, std::memory_order_release);
    g_closePrint.store(callbacks.closePrint, std::memory_order_release);
    g_owner = owner;
    return true;
}

void uninstall(const void* owner)
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (g_owner != owner)
        return;

    g_startPrint.store(nullptr, std::memory_order_release);
    g_printALine.store(nullptr, std::memory_order_release);
    g_endPrint.store(nullptr, std::memory_order_release);
    g_closePrint.store(nullptr, std::memory_order_release);
    g_owner = nullptr;
}

bool isInstalledFor(const void* owner)
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    return owner && g_owner == owner;
}

} // namespace NocaiDirectPrintCompatibility

#if defined(__linux__) && \
    (defined(__x86_64__) || defined(__aarch64__)) && !defined(__ANDROID__)

// These names and signatures match the vendor's host callback ABI. They must
// remain exported by the executable: the x86-64 library has lazy imports for
// them, and the ARM library uses them for vendor-generated pattern jobs.
extern "C" __attribute__((visibility("default"))) int StartPrint(void* property)
{
    const auto fn = g_startPrint.load(std::memory_order_acquire);
    return fn ? fn(property) : 0;
}

extern "C" __attribute__((visibility("default"))) int WriteRipData(
    char* data, std::uint32_t size)
{
    const auto fn = g_printALine.load(std::memory_order_acquire);
    return fn ? fn(data, size) : -1;
}

extern "C" __attribute__((visibility("default"))) int EndRipData()
{
    const auto fn = g_endPrint.load(std::memory_order_acquire);
    return fn ? fn() : 0;
}

extern "C" __attribute__((visibility("default"))) int ExitPrinter()
{
    const auto fn = g_closePrint.load(std::memory_order_acquire);
    return fn ? fn() : 0;
}

#endif
