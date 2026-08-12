#include "NocaiArmCommandTagCompatibility.h"
#include "NocaiArmStaleSessionRecovery.h"

#include <QLibrary>
#include <QString>
#include <QtGlobal>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(__aarch64__)
#include <QByteArray>
#include <QFile>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unordered_set>
#include <unistd.h>

namespace {

constexpr char kValidatedBuildId[] =
    "e7f1dec8ba820d78cb754b603ddd94146f7ddec2";
constexpr std::size_t kSendCommandInstructionOffset = 0x08;
constexpr std::size_t kSendSwathInstructionOffset = 0x2d8;
constexpr std::uint32_t kSendCommandOriginal = 0x52892007; // mov w7, #0x4900
constexpr std::uint32_t kSendSwathOriginal = 0x52892000;   // mov w0, #0x4900
constexpr std::size_t kCreatePasswordInstructionOffset = 0x60;
constexpr std::uint32_t kCreatePasswordOriginal = 0x52892860;  // mov w0, #0x4943
constexpr std::uint32_t kCreatePasswordCorrected = 0x5288e980; // mov w0, #0x474c

struct InstructionPatch
{
    std::size_t offset;
    std::uint32_t original;
    std::uint32_t corrected;
};

constexpr InstructionPatch kSignedHeadOffsetPatches[] = {
    {0x88, 0x3943a89c, 0x39c3a89c},
    {0x90, 0x39442883, 0x39c42883},
    {0xa4, 0x3943c882, 0x39c3c882},
    {0xa8, 0x39444884, 0x39c44884},
};

std::atomic<bool> g_enabled{false};
std::atomic<unsigned int> g_correctionCount{0};
std::mutex g_ownerMutex;
std::unordered_set<const void*> g_owners;

template<typename T>
bool copyObject(const QByteArray& bytes, quint64 offset, T* object)
{
    if (!object || offset > static_cast<quint64>(bytes.size()) ||
        sizeof(T) > static_cast<quint64>(bytes.size()) - offset) {
        return false;
    }
    std::memcpy(object, bytes.constData() + static_cast<qsizetype>(offset),
                sizeof(T));
    return true;
}

quint64 align4(quint64 value)
{
    return (value + 3u) & ~quint64(3u);
}

QString elfBuildId(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();

    Elf64_Ehdr header{};
    if (!copyObject(bytes, 0, &header) ||
        std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 ||
        header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_phentsize < sizeof(Elf64_Phdr)) {
        return {};
    }

    for (quint64 index = 0; index < header.e_phnum; ++index) {
        Elf64_Phdr segment{};
        const quint64 programOffset = header.e_phoff + index * header.e_phentsize;
        if (!copyObject(bytes, programOffset, &segment))
            return {};
        if (segment.p_type != PT_NOTE ||
            segment.p_offset > static_cast<quint64>(bytes.size()) ||
            segment.p_filesz > static_cast<quint64>(bytes.size()) - segment.p_offset) {
            continue;
        }

        quint64 cursor = segment.p_offset;
        const quint64 end = segment.p_offset + segment.p_filesz;
        while (cursor + sizeof(Elf64_Nhdr) <= end) {
            Elf64_Nhdr note{};
            if (!copyObject(bytes, cursor, &note))
                break;
            cursor += sizeof(note);
            const quint64 nameOffset = cursor;
            const quint64 descriptorOffset = align4(nameOffset + note.n_namesz);
            const quint64 next = align4(descriptorOffset + note.n_descsz);
            if (nameOffset > end || note.n_namesz > end - nameOffset ||
                descriptorOffset > end || note.n_descsz > end - descriptorOffset ||
                next > end) {
                break;
            }

            const QByteArray name = bytes.mid(
                static_cast<qsizetype>(nameOffset), note.n_namesz);
            if (note.n_type == NT_GNU_BUILD_ID && name.startsWith("GNU")) {
                const QByteArray id = bytes.mid(
                    static_cast<qsizetype>(descriptorOffset), note.n_descsz);
                return QString::fromLatin1(id.toHex());
            }
            cursor = next;
        }
    }
    return {};
}

bool validateBuild(QFunctionPointer symbol, QString* errorMessage)
{
    Dl_info info{};
    if (!symbol || ::dladdr(reinterpret_cast<const void*>(symbol), &info) == 0 ||
        !info.dli_fname) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not identify the loaded ARM SDK file.");
        return false;
    }

    const QString buildId = elfBuildId(QString::fromLocal8Bit(info.dli_fname));
    if (buildId != QLatin1String(kValidatedBuildId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The ARM direct-print SDK Build ID is %1; expected validated X-33 build %2. The compatibility correction was refused.")
                                .arg(buildId.isEmpty() ? QStringLiteral("unavailable") : buildId,
                                     QLatin1String(kValidatedBuildId));
        }
        return false;
    }
    return true;
}

bool readInstruction(QFunctionPointer symbol, std::size_t offset,
                     std::uint32_t* instruction)
{
    if (!symbol || !instruction)
        return false;
    const auto address = reinterpret_cast<quintptr>(symbol);
    std::memcpy(instruction, reinterpret_cast<const void*>(address + offset),
                sizeof(*instruction));
    return true;
}

bool ensureInstructionPatch(QFunctionPointer symbol,
                            const InstructionPatch& patch,
                            const char* label,
                            bool* changed,
                            QString* errorMessage)
{
    if (!symbol) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The validated ARM direct-print SDK is missing %1.")
                                .arg(QString::fromLatin1(label));
        }
        return false;
    }

    auto* const instruction = reinterpret_cast<unsigned char*>(symbol) + patch.offset;
    std::uint32_t current = 0;
    std::memcpy(&current, instruction, sizeof(current));
    if (current == patch.corrected)
        return true;
    if (current != patch.original) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The ARM direct-print SDK %1 instruction does not match the validated X-33 build; compatibility was refused.")
                                .arg(QString::fromLatin1(label));
        }
        return false;
    }

    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || (pageSize & (pageSize - 1)) != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not determine a valid page size for ARM SDK compatibility.");
        return false;
    }
    const auto pageMask = static_cast<quintptr>(pageSize - 1);
    const auto instructionAddress = reinterpret_cast<quintptr>(instruction);
    void* const page = reinterpret_cast<void*>(instructionAddress & ~pageMask);
    if (::mprotect(page, static_cast<std::size_t>(pageSize),
                   PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not make the ARM SDK %1 instruction writable: %2")
                                .arg(QString::fromLatin1(label),
                                     QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    std::memcpy(instruction, &patch.corrected, sizeof(patch.corrected));
    __builtin___clear_cache(reinterpret_cast<char*>(instruction),
                            reinterpret_cast<char*>(instruction + sizeof(patch.corrected)));
    if (::mprotect(page, static_cast<std::size_t>(pageSize),
                   PROT_READ | PROT_EXEC) == 0) {
        if (changed)
            *changed = true;
        return true;
    }

    const int restoreError = errno;
    std::memcpy(instruction, &patch.original, sizeof(patch.original));
    __builtin___clear_cache(reinterpret_cast<char*>(instruction),
                            reinterpret_cast<char*>(instruction + sizeof(patch.original)));
    (void)::mprotect(page, static_cast<std::size_t>(pageSize),
                     PROT_READ | PROT_EXEC);
    if (errorMessage) {
        *errorMessage = QStringLiteral(
            "Could not restore ARM SDK %1 code protection: %2")
                            .arg(QString::fromLatin1(label),
                                 QString::fromLocal8Bit(std::strerror(restoreError)));
    }
    return false;
}

std::unique_ptr<unsigned char[]> correctedWireCopy(
    const void* buffer, std::size_t length, unsigned int* command)
{
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    if (!g_enabled.load(std::memory_order_acquire) || !bytes || length < 10)
        return {};

    std::size_t payloadOffset = std::numeric_limits<std::size_t>::max();
    if (length >= 30 && (bytes[0] >> 4) == 4) {
        const std::size_t ipHeaderSize = (bytes[0] & 0x0f) * 4;
        if (ipHeaderSize >= 20 && length >= ipHeaderSize + 10 && bytes[9] == 233)
            payloadOffset = ipHeaderSize;
    } else if (length >= 44 && bytes[12] == 0x08 && bytes[13] == 0x00 &&
               (bytes[14] >> 4) == 4) {
        const std::size_t ipHeaderSize = (bytes[14] & 0x0f) * 4;
        if (ipHeaderSize >= 20 && length >= 14 + ipHeaderSize + 10 &&
            bytes[23] == 233) {
            payloadOffset = 14 + ipHeaderSize;
        }
    } else if (bytes[0] == 0x00 && bytes[1] == 0x19 && bytes[3] == 0x58) {
        payloadOffset = 0;
    }

    if (payloadOffset == std::numeric_limits<std::size_t>::max() ||
        bytes[payloadOffset + 9] != 0x49) {
        return {};
    }

    std::unique_ptr<unsigned char[]> corrected(
        new (std::nothrow) unsigned char[length]);
    if (!corrected)
        return {};
    std::memcpy(corrected.get(), buffer, length);
    if (command)
        *command = corrected[payloadOffset + 8];
    corrected[payloadOffset + 9] = 0x47;
    return corrected;
}

void logCorrection(unsigned int command)
{
    const unsigned int count = g_correctionCount.fetch_add(
        1, std::memory_order_relaxed) + 1;
    std::fprintf(stderr,
                 "PrintFlow: ARM X-33 wire-tag correction #%u: command=%u "
                 "0x49 -> 0x47\n",
                 count, command);
}

} // namespace

extern "C" __attribute__((visibility("default"))) ssize_t sendto(
    int socket, const void* buffer, std::size_t length, int flags,
    const sockaddr* destination, socklen_t destinationLength)
{
    using RealSendToFn = ssize_t (*)(int, const void*, std::size_t, int,
                                     const sockaddr*, socklen_t);
    static const auto realSendTo = reinterpret_cast<RealSendToFn>(
        dlsym(RTLD_NEXT, "sendto"));
    if (!realSendTo) {
        errno = ENOSYS;
        return -1;
    }

    unsigned int command = 0;
    if (auto corrected = correctedWireCopy(buffer, length, &command)) {
        const ssize_t result = realSendTo(
            socket, corrected.get(), length, flags,
            destination, destinationLength);
        logCorrection(command);
        return result;
    }
    return realSendTo(socket, buffer, length, flags,
                      destination, destinationLength);
}

extern "C" __attribute__((visibility("default"))) ssize_t send(
    int socket, const void* buffer, std::size_t length, int flags)
{
    using RealSendFn = ssize_t (*)(int, const void*, std::size_t, int);
    static const auto realSend = reinterpret_cast<RealSendFn>(
        dlsym(RTLD_NEXT, "send"));
    if (!realSend) {
        errno = ENOSYS;
        return -1;
    }

    unsigned int command = 0;
    if (auto corrected = correctedWireCopy(buffer, length, &command)) {
        const ssize_t result = realSend(socket, corrected.get(), length, flags);
        logCorrection(command);
        return result;
    }
    return realSend(socket, buffer, length, flags);
}
#endif

namespace NocaiArmCommandTagCompatibility {

bool install(const void* owner, QLibrary& library, QString* errorMessage)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(__aarch64__)
    if (!owner) {
        if (errorMessage)
            *errorMessage = QStringLiteral("ARM X-33 compatibility owner is invalid.");
        return false;
    }

    const QFunctionPointer sendCommand = library.resolve("_Z8send_cmdiPcji");
    const QFunctionPointer sendSwathCommand = library.resolve(
        "_Z22Hr_SendNetSwathCommandP9ThreadMsgP11SWATH_QUEUE");
    const QFunctionPointer createPassword = library.resolve("_Z14CreatePasswordt");
    const QFunctionPointer getChanOffset = library.resolve(
        "_Z18Andy_GetChanOffsethjhP11UISetupPara");
    if (!sendCommand || !sendSwathCommand || !createPassword || !getChanOffset) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The ARM direct-print SDK is not the validated X-33 build; required command symbols are unavailable.");
        }
        return false;
    }
    if (!validateBuild(sendCommand, errorMessage))
        return false;

    std::uint32_t sendInstruction = 0;
    std::uint32_t swathInstruction = 0;
    if (!readInstruction(sendCommand, kSendCommandInstructionOffset, &sendInstruction) ||
        !readInstruction(sendSwathCommand, kSendSwathInstructionOffset, &swathInstruction) ||
        sendInstruction != kSendCommandOriginal ||
        swathInstruction != kSendSwathOriginal) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The ARM direct-print SDK command layout does not match the validated X-33 build; the wire correction was refused.");
        }
        return false;
    }

    const std::lock_guard<std::mutex> lock(g_ownerMutex);
    bool passwordChanged = false;
    if (!ensureInstructionPatch(
            createPassword,
            {kCreatePasswordInstructionOffset,
             kCreatePasswordOriginal,
             kCreatePasswordCorrected},
            "CreatePassword", &passwordChanged, errorMessage)) {
        return false;
    }

    bool signedOffsetsChanged = false;
    for (const InstructionPatch& patch : kSignedHeadOffsetPatches) {
        bool changed = false;
        if (!ensureInstructionPatch(getChanOffset, patch,
                                    "Andy_GetChanOffset signed-offset",
                                    &changed, errorMessage)) {
            return false;
        }
        signedOffsetsChanged = signedOffsetsChanged || changed;
    }

    const bool wasEmpty = g_owners.empty();
    g_owners.insert(owner);
    if (wasEmpty) {
        g_correctionCount.store(0, std::memory_order_release);
        g_enabled.store(true, std::memory_order_release);
        std::fprintf(stderr,
                     "PrintFlow: enabled validated ARM X-33 compatibility: "
                     "response password 0x4943 -> 0x474c%s, signed head offsets%s, "
                     "and wire tag 0x49 -> 0x47.\n",
                     passwordChanged ? "" : " (already active)",
                     signedOffsetsChanged ? "" : " (already active)");
    }
    return true;
#else
    Q_UNUSED(owner);
    Q_UNUSED(library);
    Q_UNUSED(errorMessage);
    return true;
#endif
}

void uninstall(const void* owner)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(__aarch64__)
    const std::lock_guard<std::mutex> lock(g_ownerMutex);
    g_owners.erase(owner);
    if (g_owners.empty())
        g_enabled.store(false, std::memory_order_release);
#else
    Q_UNUSED(owner);
#endif
}

bool ensurePromiscuousReceive(QLibrary& library, int printerIndex,
                              QString* detailMessage)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(__aarch64__)
    const QFunctionPointer sendCommand = library.resolve("_Z8send_cmdiPcji");
    if (!validateBuild(sendCommand, detailMessage))
        return false;

    const QFunctionPointer addressList = library.resolve("g_PrinterAddrList");
    if (!addressList) {
        if (detailMessage) {
            *detailMessage = QStringLiteral(
                "The validated ARM SDK does not expose its selected printer address list.");
        }
        return false;
    }

    const auto result = NocaiArmStaleSessionRecovery::ensurePromiscuousReceive(
        reinterpret_cast<const void*>(addressList), printerIndex);
    if (detailMessage)
        *detailMessage = QString::fromStdString(result.detail);
    return result.completed;
#else
    Q_UNUSED(library);
    Q_UNUSED(printerIndex);
    if (detailMessage)
        detailMessage->clear();
    return false;
#endif
}

bool clearStaleLocalSocketLocks(QLibrary& library, int printerIndex,
                                QString* detailMessage)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(__aarch64__)
    const QFunctionPointer sendCommand = library.resolve("_Z8send_cmdiPcji");
    if (!validateBuild(sendCommand, detailMessage))
        return false;

    const QFunctionPointer addressList = library.resolve("g_PrinterAddrList");
    if (!addressList) {
        if (detailMessage) {
            *detailMessage = QStringLiteral(
                "The validated ARM SDK does not expose its selected printer address list.");
        }
        return false;
    }

    const auto result = NocaiArmStaleSessionRecovery::clearStaleLocalLocks(
        reinterpret_cast<const void*>(addressList), printerIndex);
    if (detailMessage)
        *detailMessage = QString::fromStdString(result.detail);
    return result.completed;
#else
    Q_UNUSED(library);
    Q_UNUSED(printerIndex);
    if (detailMessage)
        detailMessage->clear();
    return false;
#endif
}

} // namespace NocaiArmCommandTagCompatibility
