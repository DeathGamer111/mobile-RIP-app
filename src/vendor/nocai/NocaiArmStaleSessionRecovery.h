#pragma once

#include <string>

namespace NocaiArmStaleSessionRecovery {

struct LocalLockResult
{
    bool attempted = false;
    bool completed = false;
    int inspected = 0;
    int cleared = 0;
    std::string detail;
};

struct PromiscuousReceiveResult
{
    bool attempted = false;
    bool completed = false;
    bool alreadyActive = false;
    std::string interfaceName;
    std::string detail;
};

// The ARM PrinterSocket receiver only accepts the X-33's protocol-233 replies
// while the selected Ethernet interface has a promiscuous packet membership.
// Keep a filtered AF_PACKET socket open for the lifetime of this process so
// that membership cannot disappear between ConnectPrinter and StartPrint.
// This is process-scoped and requires CAP_NET_RAW, not CAP_NET_ADMIN.
PromiscuousReceiveResult ensurePromiscuousReceive(
    const void* printerAddressList, int printerIndex);

// PrinterSocket serializes each raw channel with a named POSIX semaphore. A
// process killed while it owns one leaves the semaphore permanently at zero;
// later processes then time out locally and report the misleading
// "open socket fail" message. Remove only zero-valued semaphores whose name
// matches the selected PC IP and printer MAC. Cleanup is refused while another
// process has the Nocai API or PrinterSocket library loaded.
LocalLockResult clearStaleLocalLocks(const void* printerAddressList,
                                     int printerIndex);

} // namespace NocaiArmStaleSessionRecovery
