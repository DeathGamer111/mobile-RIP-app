#include "NocaiArmStaleSessionRecovery.h"

#if defined(__linux__) && defined(__aarch64__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <string_view>

namespace {

constexpr int kMaxPrinters = 100;
constexpr std::size_t kAddressListHeaderSize = 8;
constexpr std::size_t kAddressRecordSize = 312;
constexpr std::size_t kPcIpOffset = 276;
constexpr std::size_t kPrinterMacOffset = 280;

std::mutex g_promiscuousSocketMutex;
std::unordered_map<unsigned int, int> g_promiscuousSockets;

const unsigned char* selectedRecord(const void* printerAddressList,
                                    int printerIndex,
                                    std::string* error)
{
    if (!printerAddressList || printerIndex < 0) {
        if (error)
            *error = "printer address state is unavailable";
        return nullptr;
    }

    const auto* list = static_cast<const unsigned char*>(printerAddressList);
    std::uint32_t count = 0;
    std::memcpy(&count, list, sizeof(count));
    if (count == 0 || count > kMaxPrinters ||
        printerIndex >= static_cast<int>(count)) {
        if (error)
            *error = "selected printer is outside the validated address list";
        return nullptr;
    }

    return list + kAddressListHeaderSize +
           static_cast<std::size_t>(printerIndex) * kAddressRecordSize;
}

std::string ipv4Text(const unsigned char* address)
{
    char text[INET_ADDRSTRLEN]{};
    return ::inet_ntop(AF_INET, address, text, sizeof(text))
               ? std::string(text)
               : std::string("unknown");
}

std::string interfaceForIpv4(const unsigned char* address)
{
    ifaddrs* addresses = nullptr;
    if (::getifaddrs(&addresses) != 0)
        return {};

    std::string match;
    for (const ifaddrs* item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !item->ifa_name) {
            continue;
        }
        const auto* socketAddress =
            reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (std::memcmp(&socketAddress->sin_addr, address,
                        sizeof(socketAddress->sin_addr)) == 0) {
            match = item->ifa_name;
            break;
        }
    }
    ::freeifaddrs(addresses);
    return match;
}

std::string systemError(const std::string& operation, int error)
{
    return std::string(operation) + ": " + std::strerror(error);
}

bool validMac(const unsigned char* mac)
{
    bool nonzero = false;
    for (int index = 0; index < 6; ++index)
        nonzero = nonzero || mac[index] != 0;
    return nonzero && (mac[0] & 0x01u) == 0;
}

bool anotherNocaiProcessIsRunning()
{
    namespace fs = std::filesystem;
    std::error_code error;
    for (const fs::directory_entry& entry :
         fs::directory_iterator("/proc", error)) {
        if (error)
            break;
        const std::string pidText = entry.path().filename().string();
        if (pidText.empty() ||
            pidText.find_first_not_of("0123456789") != std::string::npos)
            continue;
        if (pidText == std::to_string(::getpid()))
            continue;

        std::ifstream maps(entry.path() / "maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("libSYPrintAPIforPROII.so") != std::string::npos ||
                line.find("PrinterSocket.so") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

std::string semaphorePrefix(const unsigned char* pcIp,
                            const unsigned char* printerMac)
{
    std::uint32_t pcIpValue = 0;
    std::memcpy(&pcIpValue, pcIp, sizeof(pcIpValue));
    std::uint64_t printerMacValue = 0;
    for (int index = 5; index >= 0; --index) {
        printerMacValue <<= 8;
        printerMacValue |= printerMac[index];
    }
    return "SY" + std::to_string(pcIpValue) +
           std::to_string(printerMacValue);
}

} // namespace
#endif

namespace NocaiArmStaleSessionRecovery {

PromiscuousReceiveResult ensurePromiscuousReceive(
    const void* printerAddressList, int printerIndex)
{
#if defined(__linux__) && defined(__aarch64__)
    PromiscuousReceiveResult result;
    std::string selectionError;
    const unsigned char* record = selectedRecord(
        printerAddressList, printerIndex, &selectionError);
    if (!record) {
        result.detail = selectionError;
        return result;
    }

    result.attempted = true;
    const unsigned char* pcIp = record + kPcIpOffset;
    result.interfaceName = interfaceForIpv4(pcIp);
    if (result.interfaceName.empty()) {
        result.detail = "no local interface owns selected PC IPv4 address " +
                        ipv4Text(pcIp);
        return result;
    }

    const unsigned int interfaceIndex =
        ::if_nametoindex(result.interfaceName.c_str());
    if (interfaceIndex == 0) {
        result.detail = systemError("if_nametoindex failed", errno);
        return result;
    }

    const std::lock_guard<std::mutex> lock(g_promiscuousSocketMutex);
    if (g_promiscuousSockets.find(interfaceIndex) !=
        g_promiscuousSockets.end()) {
        result.completed = true;
        result.alreadyActive = true;
        result.detail = "retained process-scoped promiscuous receive on " +
                        result.interfaceName;
        return result;
    }

    const int socketFd = ::socket(
        AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (socketFd < 0) {
        result.detail = systemError(
            "could not open the promiscuous receive guard (CAP_NET_RAW is required)",
            errno);
        return result;
    }

    // The socket exists only to retain PACKET_MR_PROMISC. Drop every packet at
    // the socket filter so it cannot build a receive queue or compete with the
    // vendor SDK's own packet socket.
    sock_filter dropAll[] = {
        {static_cast<unsigned short>(BPF_RET | BPF_K), 0, 0, 0},
    };
    sock_fprog filter{
        static_cast<unsigned short>(sizeof(dropAll) / sizeof(dropAll[0])),
        dropAll,
    };
    if (::setsockopt(socketFd, SOL_SOCKET, SO_ATTACH_FILTER,
                     &filter, sizeof(filter)) != 0) {
        const int error = errno;
        ::close(socketFd);
        result.detail = systemError(
            "could not filter the promiscuous receive guard", error);
        return result;
    }

    sockaddr_ll bindAddress{};
    bindAddress.sll_family = AF_PACKET;
    bindAddress.sll_protocol = htons(ETH_P_ALL);
    bindAddress.sll_ifindex = static_cast<int>(interfaceIndex);
    if (::bind(socketFd, reinterpret_cast<const sockaddr*>(&bindAddress),
               sizeof(bindAddress)) != 0) {
        const int error = errno;
        ::close(socketFd);
        result.detail = systemError(
            "could not bind the promiscuous receive guard to " +
                result.interfaceName,
            error);
        return result;
    }

    packet_mreq membership{};
    membership.mr_ifindex = static_cast<int>(interfaceIndex);
    membership.mr_type = PACKET_MR_PROMISC;
    if (::setsockopt(socketFd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
                     &membership, sizeof(membership)) != 0) {
        const int error = errno;
        ::close(socketFd);
        result.detail = systemError(
            "could not enable process-scoped promiscuous receive on " +
                result.interfaceName,
            error);
        return result;
    }

    g_promiscuousSockets.emplace(interfaceIndex, socketFd);
    result.completed = true;
    result.detail = "enabled process-scoped promiscuous receive on " +
                    result.interfaceName;
    return result;
#else
    (void)printerAddressList;
    (void)printerIndex;
    return {false, false, false, {},
            "promiscuous receive guard is ARM64 Linux only"};
#endif
}

LocalLockResult clearStaleLocalLocks(const void* printerAddressList,
                                     int printerIndex)
{
#if defined(__linux__) && defined(__aarch64__)
    LocalLockResult result;
    std::string selectionError;
    const unsigned char* record = selectedRecord(
        printerAddressList, printerIndex, &selectionError);
    if (!record) {
        result.detail = selectionError;
        return result;
    }

    result.attempted = true;
    if (anotherNocaiProcessIsRunning()) {
        result.detail =
            "another process has a Nocai SDK library loaded; local lock cleanup was refused";
        return result;
    }

    const auto* pcIp = record + kPcIpOffset;
    const auto* printerMac = record + kPrinterMacOffset;
    if (!validMac(printerMac)) {
        result.detail = "printer address list contains an invalid MAC address";
        return result;
    }

    const std::string prefix = semaphorePrefix(pcIp, printerMac);
    namespace fs = std::filesystem;
    std::error_code error;
    for (const fs::directory_entry& entry :
         fs::directory_iterator("/dev/shm", error)) {
        if (error)
            break;
        const std::string filename = entry.path().filename().string();
        constexpr std::string_view filePrefix{"sem."};
        if (filename.rfind(filePrefix, 0) != 0)
            continue;
        const std::string semaphoreName = filename.substr(filePrefix.size());
        if (semaphoreName.rfind(prefix, 0) != 0)
            continue;
        const std::string channel = semaphoreName.substr(prefix.size());
        if (channel.empty() ||
            channel.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }

        ++result.inspected;
        const std::string posixName = "/" + semaphoreName;
        sem_t* semaphore = ::sem_open(posixName.c_str(), 0);
        if (semaphore == SEM_FAILED)
            continue;
        int value = 0;
        const bool locked = ::sem_getvalue(semaphore, &value) == 0 && value == 0;
        ::sem_close(semaphore);
        if (locked && ::sem_unlink(posixName.c_str()) == 0)
            ++result.cleared;
    }

    if (error) {
        result.detail = "could not inspect PrinterSocket semaphore state: " +
                        error.message();
    } else if (result.cleared > 0) {
        result.detail = "cleared " + std::to_string(result.cleared) +
                        " crash-stale PrinterSocket lock(s)";
    } else if (result.inspected > 0) {
        result.detail = "all matching PrinterSocket locks are available";
    } else {
        result.detail = "no matching PrinterSocket locks exist";
    }
    result.completed = !error;
    return result;
#else
    (void)printerAddressList;
    (void)printerIndex;
    return {false, false, 0, 0,
            "local lock recovery is ARM64 Linux only"};
#endif
}

} // namespace NocaiArmStaleSessionRecovery
