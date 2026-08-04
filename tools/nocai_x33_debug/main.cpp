#include "elf_symbols.h"
#include "sdk_types.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <link.h>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kSdkSucceeded = 0x01;
constexpr std::size_t kRawResponseCapacity = 4096;

struct PrintOrigin
{
    double xMm = 0.0;
    double yMm = 0.0;
    std::uint32_t xHundredthsMm = 0;
    std::uint32_t yHundredthsMm = 0;
};

using StartPrintFn = int (*)(SdkPrintJobProperty*);
using PrintALineFn = int (*)(char*, std::uint32_t);
using NoArgFn = int (*)();

std::atomic<StartPrintFn> g_callbackStartPrint{nullptr};
std::atomic<PrintALineFn> g_callbackPrintALine{nullptr};
std::atomic<NoArgFn> g_callbackEndPrint{nullptr};
std::atomic<NoArgFn> g_callbackClosePrint{nullptr};

std::string timestamp(bool filename = false)
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) %
                        1000;
    std::tm local{};
    localtime_r(&seconds, &local);
    std::ostringstream output;
    output << std::put_time(&local, filename ? "%Y%m%d-%H%M%S" : "%F %T")
           << (filename ? "-" : ".") << std::setw(3) << std::setfill('0')
           << millis.count();
    return output.str();
}

std::string jsonEscape(const std::string& value)
{
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20)
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(ch) << std::dec;
            else
                output << ch;
        }
    }
    return output.str();
}

std::string hexValue(std::uint64_t value, int width = 0)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0');
    if (width > 0)
        output << std::setw(width);
    output << value;
    return output.str();
}

std::string hexBytes(const unsigned char* bytes, std::size_t size)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        if (i)
            output << ' ';
        output << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return output.str();
}

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    char quote = 0;
    bool escaped = false;
    for (const char ch : line) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (quoted) {
            if (ch == quote)
                quoted = false;
            else
                current.push_back(ch);
        } else if (ch == '"' || ch == '\'') {
            quoted = true;
            quote = ch;
        } else if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (escaped)
        current.push_back('\\');
    if (!current.empty())
        result.push_back(current);
    return result;
}

bool consumeFlag(std::vector<std::string>& words, const std::string& flag)
{
    const auto it = std::find(words.begin(), words.end(), flag);
    if (it == words.end())
        return false;
    words.erase(it);
    return true;
}

std::optional<long long> parseInteger(const std::string& text)
{
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used, 0);
        if (used != text.size())
            return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDouble(const std::string& text)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size())
            return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> millimetersToHundredths(double millimeters)
{
    if (!std::isfinite(millimeters) || millimeters < 0.0)
        return std::nullopt;
    const long double scaled = static_cast<long double>(millimeters) * 100.0L;
    if (scaled > static_cast<long double>(UINT32_MAX))
        return std::nullopt;
    return static_cast<std::uint32_t>(std::llround(scaled));
}

std::optional<std::vector<unsigned char>> parseHex(const std::string& text)
{
    std::string compact;
    for (const char ch : text) {
        if (ch == ' ' || ch == ':' || ch == '-' || ch == ',')
            continue;
        compact.push_back(ch);
    }
    if (compact.rfind("0x", 0) == 0 || compact.rfind("0X", 0) == 0)
        compact.erase(0, 2);
    if (compact.empty() || compact.size() % 2 != 0)
        return std::nullopt;
    std::vector<unsigned char> bytes;
    bytes.reserve(compact.size() / 2);
    for (std::size_t i = 0; i < compact.size(); i += 2) {
        try {
            std::size_t used = 0;
            const unsigned long value = std::stoul(compact.substr(i, 2), &used, 16);
            if (used != 2 || value > 0xff)
                return std::nullopt;
            bytes.push_back(static_cast<unsigned char>(value));
        } catch (...) {
            return std::nullopt;
        }
    }
    return bytes;
}

bool processHasNetRaw()
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("CapEff:", 0) != 0)
            continue;
        std::istringstream value(line.substr(7));
        std::uint64_t capabilities = 0;
        value >> std::hex >> capabilities;
        return (capabilities & (std::uint64_t{1} << 13)) != 0;
    }
    return false;
}

class Logger
{
public:
    bool open(const fs::path& directory)
    {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error) {
            std::cerr << "Cannot create log directory " << directory << ": "
                      << error.message() << '\n';
            return false;
        }
        const std::string stem = "nocai-x33-" + timestamp(true);
        m_textPath = directory / (stem + ".log");
        m_jsonPath = directory / (stem + ".jsonl");
        m_text.open(m_textPath, std::ios::out | std::ios::trunc);
        m_json.open(m_jsonPath, std::ios::out | std::ios::trunc);
        if (!m_text || !m_json) {
            std::cerr << "Cannot open harness log files in " << directory << '\n';
            return false;
        }
        return true;
    }

    void event(const std::string& message)
    {
        const std::string time = timestamp();
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << '[' << time << "] " << message << '\n';
        if (m_text) {
            m_text << '[' << time << "] " << message << '\n';
            m_text.flush();
        }
        if (m_json) {
            m_json << "{\"time\":\"" << jsonEscape(time)
                   << "\",\"event\":\"message\",\"message\":\""
                   << jsonEscape(message) << "\"}\n";
            m_json.flush();
        }
    }

    void snapshot(const std::string& label,
                  const std::map<std::string, std::string>& fields,
                  bool printFields)
    {
        const std::string time = timestamp();
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << '[' << time << "] snapshot " << label;
        if (printFields) {
            for (const auto& [name, value] : fields)
                std::cout << "\n  " << name << " = " << value;
        }
        std::cout << '\n';
        if (m_text) {
            m_text << '[' << time << "] snapshot " << label;
            for (const auto& [name, value] : fields)
                m_text << "\n  " << name << " = " << value;
            m_text << '\n';
            m_text.flush();
        }
        if (m_json) {
            m_json << "{\"time\":\"" << jsonEscape(time)
                   << "\",\"event\":\"snapshot\",\"label\":\""
                   << jsonEscape(label) << "\",\"fields\":{";
            bool first = true;
            for (const auto& [name, value] : fields) {
                if (!first)
                    m_json << ',';
                first = false;
                m_json << '"' << jsonEscape(name) << "\":\""
                       << jsonEscape(value) << '"';
            }
            m_json << "}}\n";
            m_json.flush();
        }
    }

    const fs::path& textPath() const { return m_textPath; }
    const fs::path& jsonPath() const { return m_jsonPath; }

private:
    std::mutex m_mutex;
    fs::path m_textPath;
    fs::path m_jsonPath;
    std::ofstream m_text;
    std::ofstream m_json;
};

enum class HeaderMode { Vendor, PrintFlow };

std::string modeName(HeaderMode mode)
{
    return mode == HeaderMode::Vendor ? "vendor" : "printflow";
}

struct PrnInspection
{
    fs::path path;
    X33DiskHeader disk{};
    SdkPrintJobProperty vendor{};
    SdkPrintJobProperty printflow{};
    std::uintmax_t actualSize = 0;
    std::uintmax_t expectedSize = 0;
    bool valid = false;
    std::string error;
};

SdkPrintJobProperty vendorHeader(const X33DiskHeader& disk)
{
    SdkPrintJobProperty sdk{};
    sdk.Signature = disk.Signature;
    sdk.XDPI = disk.XDPI;
    sdk.YDPI = disk.YDPI;
    sdk.BytesPerLine = disk.BytesPerLine;
    sdk.Height = disk.Height;
    sdk.Width = disk.Width;
    sdk.PaperWidth = disk.PaperWidth;
    sdk.Colors = disk.Colors;
    sdk.Bits = disk.Bits;
    sdk.Pass = disk.Pass;
    sdk.VsdMode = disk.VsdMode;
    sdk.Reserved = disk.Reserved[0];
    return sdk;
}

SdkPrintJobProperty printFlowHeader(const X33DiskHeader& disk)
{
    std::array<std::uint32_t, 12> words{};
    static_assert(sizeof(words) == sizeof(disk));
    std::memcpy(words.data(), &disk, sizeof(words));
    return {words[0], words[1], words[2], words[3], words[4], words[5],
            words[6], words[7], words[8], words[9], words[10], words[11]};
}

PrnInspection inspectPrn(const fs::path& path)
{
    PrnInspection result;
    result.path = path;
    std::error_code fileError;
    result.actualSize = fs::file_size(path, fileError);
    if (fileError) {
        result.error = "cannot stat PRN: " + fileError.message();
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(&result.disk),
                              sizeof(result.disk))) {
        result.error = "cannot read the complete 48-byte PRN header";
        return result;
    }
    result.vendor = vendorHeader(result.disk);
    result.printflow = printFlowHeader(result.disk);
    if (result.disk.Signature != kX33PrnSignature) {
        result.error = "signature is " + hexValue(result.disk.Signature) +
                       ", expected 0x5555";
        return result;
    }
    if (result.disk.Width == 0 || result.disk.Height == 0 ||
        result.disk.BytesPerLine == 0 || result.disk.Colors == 0 ||
        result.disk.Colors > 16) {
        result.error = "zero/unsupported dimensions, line size, or channel count";
        return result;
    }
    const std::uintmax_t rowBytes =
        static_cast<std::uintmax_t>(result.disk.BytesPerLine) * result.disk.Colors;
    if (result.disk.Height >
        (std::numeric_limits<std::uintmax_t>::max() - sizeof(X33DiskHeader)) /
            rowBytes) {
        result.error = "declared payload size overflows the host size type";
        return result;
    }
    result.expectedSize = sizeof(X33DiskHeader) +
                          rowBytes * result.disk.Height;
    if (result.actualSize != result.expectedSize) {
        std::ostringstream message;
        message << "file is " << result.actualSize << " bytes; header requires "
                << result.expectedSize;
        result.error = message.str();
        return result;
    }
    result.valid = true;
    return result;
}

std::string headerSummary(const SdkPrintJobProperty& header)
{
    std::ostringstream output;
    output << "sig=" << hexValue(header.Signature) << ", "
           << header.Width << 'x' << header.Height << ", dpi="
           << header.XDPI << 'x' << header.YDPI << ", BPL="
           << header.BytesPerLine << ", Colors=" << header.Colors
           << ", Bits=" << header.Bits << ", Pass=" << header.Pass
           << ", VsdMode=" << header.VsdMode << ", Reserved="
           << header.Reserved;
    return output.str();
}

struct Api
{
    using SearchFn = int (*)(PrinterInfoList*, int);
    using SelectFn = int (*)(int);
    using HeadMaskFn = int (*)(int);
    using MoveAxisFn = int (*)(int, int);
    using AxisPosFn = int (*)(int, int*);
    using SetHeightFn = int (*)(std::uint16_t);
    using GetHeightFn = int (*)(std::uint16_t*);
    using SetJobFn = int (*)(JobSettings*, int);
    using GetJobFn = int (*)(JobSettings*, int);
    using SetAlignFn = int (*)(AlignmentValues*, int, int);
    using GetAlignFn = int (*)(AlignmentValues*, int);
    using ConfigFn = int (*)(char*);
    using PatternFn = int (*)(int);
    using StatusFn = int (*)(PrinterStatus*, int);
    using InfoFn = int (*)(PrinterInfo*, int);
    using SetXYFn = int (*)(std::uint32_t, std::uint32_t);
    using GetXYFn = int (*)(std::uint32_t*, std::uint32_t*);
    using SetUVFn = int (*)(UVParamValues*, int, int);
    using GetUVFn = int (*)(UVParamValues*, int);
    using NewUVActionFn = int (*)(int);
    using SetNewUVFn = int (*)(NewUVParamValues*, int, int);
    using GetNewUVFn = int (*)(NewUVParamValues*, int);
    using NetFn = int (*)(char*, std::uint32_t, char*, std::uint32_t*, char);

    SearchFn search = nullptr;
    SelectFn select = nullptr;
    NoArgFn connect = nullptr;
    NoArgFn init = nullptr;
    StartPrintFn startPrint = nullptr;
    PrintALineFn printALine = nullptr;
    NoArgFn endPrint = nullptr;
    NoArgFn closePrint = nullptr;
    NoArgFn abortPrint = nullptr;
    NoArgFn pausePrint = nullptr;
    NoArgFn continuePrint = nullptr;
    HeadMaskFn wipe = nullptr;
    HeadMaskFn clean = nullptr;
    HeadMaskFn startPump = nullptr;
    NoArgFn stopPump = nullptr;
    HeadMaskFn startSpit = nullptr;
    NoArgFn stopSpit = nullptr;
    NoArgFn cap = nullptr;
    MoveAxisFn moveAxis = nullptr;
    AxisPosFn stopAxis = nullptr;
    AxisPosFn saveAxis = nullptr;
    SetHeightFn setHeight = nullptr;
    GetHeightFn getHeight = nullptr;
    SetJobFn setJob = nullptr;
    GetJobFn getJob = nullptr;
    SetAlignFn setAlign = nullptr;
    GetAlignFn getAlign = nullptr;
    ConfigFn exportConfig = nullptr;
    ConfigFn importConfig = nullptr;
    PatternFn pattern = nullptr;
    StatusFn status = nullptr;
    InfoFn info = nullptr;
    SetXYFn setXY = nullptr;
    GetXYFn getXY = nullptr;
    SetUVFn setUV = nullptr;
    GetUVFn getUV = nullptr;
    NoArgFn supportsNewUV = nullptr;
    NewUVActionFn newUVAction = nullptr;
    SetNewUVFn setNewUV = nullptr;
    GetNewUVFn getNewUV = nullptr;
    NetFn netOrder = nullptr;
    NetFn netData = nullptr;
};

} // namespace

extern "C" __attribute__((visibility("default"))) int StartPrint(void* property)
{
    const auto function = g_callbackStartPrint.load(std::memory_order_acquire);
    return function ? function(static_cast<SdkPrintJobProperty*>(property)) : 0;
}

extern "C" __attribute__((visibility("default"))) int WriteRipData(
    char* data, std::uint32_t size)
{
    const auto function = g_callbackPrintALine.load(std::memory_order_acquire);
    return function ? function(data, size) : -1;
}

extern "C" __attribute__((visibility("default"))) int EndRipData()
{
    const auto function = g_callbackEndPrint.load(std::memory_order_acquire);
    return function ? function() : 0;
}

extern "C" __attribute__((visibility("default"))) int ExitPrinter()
{
    const auto function = g_callbackClosePrint.load(std::memory_order_acquire);
    return function ? function() : 0;
}

namespace {

class Harness
{
public:
    Harness(fs::path sdkRoot, fs::path defaultPrn, fs::path logDirectory)
        : m_sdkRoot(std::move(sdkRoot)), m_defaultPrn(std::move(defaultPrn))
    {
        m_logger.open(logDirectory);
    }

    ~Harness()
    {
        stopWorker();
        g_callbackStartPrint.store(nullptr, std::memory_order_release);
        g_callbackPrintALine.store(nullptr, std::memory_order_release);
        g_callbackEndPrint.store(nullptr, std::memory_order_release);
        g_callbackClosePrint.store(nullptr, std::memory_order_release);
        if (m_library)
            dlclose(m_library);
    }

    bool load()
    {
        const fs::path libraryPath = m_sdkRoot / "libSYPrintAPIforPROII.so";
        m_logger.event("SDK root: " + m_sdkRoot.string());
        m_logger.event("PRN default: " + m_defaultPrn.string());
        if (!processHasNetRaw())
            m_logger.event("WARNING: CAP_NET_RAW is not effective; SearchPrinter will not be able to open raw sockets");
        if (::chdir(m_sdkRoot.c_str()) != 0) {
            m_logger.event("chdir failed: " + std::string(std::strerror(errno)));
            return false;
        }
        dlerror();
        m_library = dlopen(libraryPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        if (!m_library) {
            m_logger.event("dlopen failed: " + std::string(dlerror()));
            return false;
        }
        link_map* map = nullptr;
        if (dlinfo(m_library, RTLD_DI_LINKMAP, &map) == 0 && map)
            m_libraryBase = static_cast<std::uintptr_t>(map->l_addr);

        std::string elfError;
        if (!m_symbols.load(libraryPath, elfError)) {
            m_logger.event("ELF symbols unavailable: " + elfError);
            return false;
        }

        bool required = true;
        required &= resolve(m_api.search, "API_SearchPrinter",
                            "_Z17API_SearchPrinterP15PrinterInfoListi");
        required &= resolve(m_api.select, "API_SelectPrinter",
                            "_Z17API_SelectPrinteri", "ChoosePrinter");
        required &= resolve(m_api.init, "API_InitPrinter", "_Z15API_InitPrinterv");
        required &= resolve(m_api.startPrint, "API_StartPrint",
                            "_Z14API_StartPrintP19tagPrintJobProperty");
        required &= resolve(m_api.printALine, "API_PrintALine",
                            "_Z14API_PrintALinePcj");
        required &= resolve(m_api.endPrint, "API_EndPrint", "_Z12API_EndPrintv");
        required &= resolve(m_api.closePrint, "API_ClosePrint",
                            "_Z14API_ClosePrintv");
        required &= resolve(m_api.abortPrint, "API_AbortPrint",
                            "_Z14API_AbortPrintv");
        required &= resolve(m_api.pausePrint, "API_PausePrint",
                            "_Z14API_PausePrintv");
        required &= resolve(m_api.continuePrint, "API_ContinuePrint",
                            "_Z17API_ContinuePrintv");

        resolve(m_api.connect, "API_ConnectPrinter", "_Z18API_ConnectPrinterv");
        resolve(m_api.wipe, "API_WipePrintHead", "_Z17API_WipePrintHeadi");
        resolve(m_api.clean, "API_AutoCleanHead", "_Z17API_AutoCleanHeadi");
        resolve(m_api.startPump, "API_StartPumpInk", "_Z16API_StartPumpInki");
        resolve(m_api.stopPump, "API_StopPumpInk", "_Z15API_StopPumpInkv");
        resolve(m_api.startSpit, "API_StartSpitInk", "_Z16API_StartSpitInki");
        resolve(m_api.stopSpit, "API_StopSpitInk", "_Z15API_StopSpitInkv");
        resolve(m_api.cap, "API_CapPrintHead", "_Z16API_CapPrintHeadv");
        resolve(m_api.moveAxis, "API_MoveAxis", "_Z12API_MoveAxisii");
        resolve(m_api.stopAxis, "API_StopAxis", "_Z12API_StopAxisiPi");
        resolve(m_api.saveAxis, "API_SaveAxisPos", "_Z15API_SaveAxisPosiPi");
        resolve(m_api.setHeight, "API_SetPrintHeight", "_Z18API_SetPrintHeightt");
        resolve(m_api.getHeight, "API_GetPrintHeight", "_Z18API_GetPrintHeightPt");
        resolve(m_api.setJob, "API_SetJobSettings",
                "_Z18API_SetJobSettingsP13stJobSettingsi");
        resolve(m_api.getJob, "API_GetJobSettings",
                "_Z18API_GetJobSettingsP13stJobSettingsi");
        resolve(m_api.setAlign, "API_SetAlignmentValues",
                "_Z22API_SetAlignmentValuesP17stAlignmentValues20eAlignmentValueTypesi");
        resolve(m_api.getAlign, "API_GetAlignmentValues",
                "_Z22API_GetAlignmentValuesP17stAlignmentValuesi");
        resolve(m_api.exportConfig, "API_ExportConfigFile", "_Z20API_ExportConfigFilePc");
        resolve(m_api.importConfig, "API_ImportConfigFile", "_Z20API_ImportConfigFilePc");
        resolve(m_api.pattern, "API_PrintAlignmentPattern",
                "_Z25API_PrintAlignmentPattern22eAlignmentPatternTypes");
        resolve(m_api.status, "API_GetPrinterStatus",
                "_Z20API_GetPrinterStatusP15stPrinterStatusi");
        resolve(m_api.info, "API_GetPrinterInfo",
                "_Z18API_GetPrinterInfoP13stPrinterInfoi");
        resolve(m_api.setXY, "API_SetPrintXYValue", "_Z19API_SetPrintXYValuejj");
        resolve(m_api.getXY, "API_GetPrintXYValue", "_Z19API_GetPrintXYValuePjS_");
        resolve(m_api.setUV, "API_SetUVParamValues",
                "_Z20API_SetUVParamValuesP15stUVParamValues18eUVParamValueTypesi");
        resolve(m_api.getUV, "API_GetUVParamValues",
                "_Z20API_GetUVParamValuesP15stUVParamValuesi");
        resolve(m_api.supportsNewUV, "API_GetSupportNewUVParam",
                "_Z24API_GetSupportNewUVParamv");
        resolve(m_api.newUVAction, "API_SetNewUVParamFunction",
                "_Z25API_SetNewUVParamFunction24eNewUVParamFunctionTypes");
        resolve(m_api.setNewUV, "API_SetNewUVParamValues",
                "_Z23API_SetNewUVParamValuesP18stNewUVParamValues21eNewUVParamValueTypesi");
        resolve(m_api.getNewUV, "API_GetNewUVParamValues",
                "_Z23API_GetNewUVParamValuesP18stNewUVParamValuesi");
        resolve(m_api.netOrder, "API_NetSendOrder",
                "_Z16API_NetSendOrderPcjS_Pjc");
        resolve(m_api.netData, "API_NetSendData", "_Z15API_NetSendDataPcjS_Pjc");

        if (!required) {
            m_logger.event("one or more required print symbols are missing");
            return false;
        }
        g_callbackStartPrint.store(m_api.startPrint, std::memory_order_release);
        g_callbackPrintALine.store(m_api.printALine, std::memory_order_release);
        g_callbackEndPrint.store(m_api.endPrint, std::memory_order_release);
        g_callbackClosePrint.store(m_api.closePrint, std::memory_order_release);
        m_logger.event("SDK loaded; ELF base=" + hexValue(m_libraryBase) +
                       ", parsed symbols=" +
                       std::to_string(m_symbols.entries().size()));
        takeSnapshot("after-load", true);
        return true;
    }

    void repl()
    {
        printHelp();
        std::string line;
        while (true) {
            reapWorker();
            std::cout << "x33> " << std::flush;
            if (!std::getline(std::cin, line))
                break;
            auto words = tokenize(line);
            if (words.empty())
                continue;
            if (words[0] == "quit" || words[0] == "exit")
                break;
            dispatch(std::move(words));
        }
        stopWorker();
    }

private:
    template<typename Function>
    bool resolve(Function& target, const char* display,
                 const char* mangled, const char* plain = nullptr)
    {
        void* address = dlsym(m_library, mangled);
        if (!address)
            address = dlsym(m_library, display);
        if (!address && plain)
            address = dlsym(m_library, plain);
        target = reinterpret_cast<Function>(address);
        if (!target)
            m_logger.event(std::string("symbol unavailable: ") + display);
        return target != nullptr;
    }

    template<typename Function, typename... Args>
    int call(const std::string& name, Function function, Args... args)
    {
        if (!function) {
            m_logger.event(name + " unavailable in this SDK");
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_sdkCallMutex);
        m_logger.event(name + " begin");
        const int result = function(args...);
        m_logger.event(name + " result=" + hexValue(static_cast<std::uint32_t>(result)) +
                       (result == kSdkSucceeded ? " (success)" : ""));
        return result;
    }

    static bool mutationAllowed(bool execute, const std::string& command)
    {
        if (execute)
            return true;
        std::cout << command << " changes printer state; append --execute.\n";
        return false;
    }

    void printHelp() const
    {
        std::cout << R"HELP(
Standalone X-33 SDK commands
  help | quit
  inspect [prn]                       validate and compare both 48-byte mappings
  abi [vendor|printflow]              show/change the default StartPrint mapping
  search | select INDEX | connect
  status | info | snapshot [LABEL]
  symbols [FILTER] | globals interesting | globals all [FILTER]
  height get | height set MM --execute    decimal mm; SDK raw unit is 0.1 mm
  xy get                              show raw hundredths and decoded mm
  xy set-mm X_MM Y_MM --execute       1 raw unit = 0.01 mm
  xy set-raw X Y --execute            low-level uint32 values
  job get | job set KEY=VALUE... --execute
  align get | align set TYPE VALUE [INDEX] --execute
  uv get | uv set TYPE VALUE --execute
  newuv support | newuv get | newuv set TYPE VALUE --execute
  newuv action TYPE --execute
  wipe MASK --execute | clean MASK --execute
  pump MASK --execute | stop-pump
  spit MASK --execute | stop-spit
  cap --execute
  move AXIS DIR --execute | stop-axis AXIS | save-axis AXIS --execute
  config export PATH | config import PATH --execute
  pattern TYPE --execute              0 is nozzle check; valid SDK range 0..22
  print [PRN] [vendor|printflow] [xy-mm X Y] --execute
                                       apply XY after Init, before StartPrint
  pause | continue --execute | abort
  raw order CHANNEL HEX --unsafe | raw data CHANNEL HEX --unsafe
  trace interval ROWS | trace window FIRST LAST | trace window off
  core on | core off | core status

Emergency pause, abort, stop-pump, stop-spit, and stop-axis do not require a
safety flag. Head masks, axes, alignment types, UV types, pattern types, and
raw channels accept decimal or 0x-prefixed integers.
)HELP";
    }

    void dispatch(std::vector<std::string> words)
    {
        const bool execute = consumeFlag(words, "--execute");
        const bool unsafe = consumeFlag(words, "--unsafe");
        const std::string command = words[0];
        if (command == "help") {
            printHelp();
        } else if (command == "inspect") {
            showInspection(words.size() > 1 ? fs::path(words[1]) : m_defaultPrn);
        } else if (command == "abi") {
            commandAbi(words);
        } else if (command == "search") {
            search();
        } else if (command == "select") {
            select(words);
        } else if (command == "connect") {
            connect();
        } else if (command == "status") {
            showStatus();
        } else if (command == "info") {
            showInfo();
        } else if (command == "snapshot") {
            takeSnapshot(words.size() > 1 ? words[1] : "manual", true);
        } else if (command == "symbols") {
            listSymbols(words.size() > 1 ? words[1] : "", false);
        } else if (command == "globals") {
            globals(words);
        } else if (command == "height") {
            height(words, execute);
        } else if (command == "xy") {
            xy(words, execute);
        } else if (command == "job") {
            job(words, execute);
        } else if (command == "align") {
            alignment(words, execute);
        } else if (command == "uv") {
            uv(words, execute);
        } else if (command == "newuv") {
            newUv(words, execute);
        } else if (command == "wipe") {
            headCommand(words, execute, "WipePrintHead", m_api.wipe, false);
        } else if (command == "clean") {
            headCommand(words, execute, "AutoCleanHead", m_api.clean, false);
        } else if (command == "pump") {
            headCommand(words, execute, "StartPumpInk", m_api.startPump, false);
        } else if (command == "spit") {
            headCommand(words, execute, "StartSpitInk", m_api.startSpit, false);
        } else if (command == "stop-pump") {
            call("StopPumpInk", m_api.stopPump);
        } else if (command == "stop-spit") {
            call("StopSpitInk", m_api.stopSpit);
        } else if (command == "cap") {
            if (mutationAllowed(execute, "cap"))
                call("CapPrintHead", m_api.cap);
        } else if (command == "move") {
            move(words, execute);
        } else if (command == "stop-axis") {
            axisPosition(words, false, execute);
        } else if (command == "save-axis") {
            axisPosition(words, true, execute);
        } else if (command == "config") {
            config(words, execute);
        } else if (command == "pattern") {
            pattern(words, execute);
        } else if (command == "print") {
            startWorker(words, execute);
        } else if (command == "pause") {
            call("PausePrint", m_api.pausePrint);
        } else if (command == "continue") {
            if (mutationAllowed(execute, "continue"))
                call("ContinuePrint", m_api.continuePrint);
        } else if (command == "abort") {
            abortNow();
        } else if (command == "raw") {
            raw(words, unsafe);
        } else if (command == "trace") {
            trace(words);
        } else if (command == "core") {
            core(words);
        } else {
            std::cout << "Unknown command. Run help.\n";
        }
    }

    void showInspection(const fs::path& path)
    {
        const PrnInspection inspection = inspectPrn(path);
        std::cout << "PRN: " << inspection.path << '\n'
                  << "file size: " << inspection.actualSize << '\n';
        if (inspection.disk.Signature || inspection.actualSize >= sizeof(X33DiskHeader)) {
            std::cout << "disk: sig=" << hexValue(inspection.disk.Signature)
                      << ", " << inspection.disk.Width << 'x'
                      << inspection.disk.Height << ", dpi="
                      << inspection.disk.XDPI << 'x' << inspection.disk.YDPI
                      << ", BPL=" << inspection.disk.BytesPerLine
                      << ", Colors(u16)=" << inspection.disk.Colors
                      << ", Bits(u16)=" << inspection.disk.Bits
                      << ", Pass=" << inspection.disk.Pass
                      << ", VsdMode=" << inspection.disk.VsdMode
                      << ", Reserved={" << inspection.disk.Reserved[0] << ','
                      << inspection.disk.Reserved[1] << "}\n"
                      << "vendor SDK ABI:    " << headerSummary(inspection.vendor)
                      << "\nprintflow raw ABI: " << headerSummary(inspection.printflow)
                      << '\n';
        }
        if (!inspection.valid) {
            std::cout << "INVALID: " << inspection.error << '\n';
            return;
        }
        std::cout << "VALID: exact length " << inspection.expectedSize << " = 48 + "
                  << inspection.disk.Height << " x " << inspection.disk.Colors
                  << " x " << inspection.disk.BytesPerLine << '\n';
    }

    void commandAbi(const std::vector<std::string>& words)
    {
        if (words.size() == 1) {
            std::cout << "default ABI: " << modeName(m_mode) << '\n';
            return;
        }
        if (words[1] == "vendor")
            m_mode = HeaderMode::Vendor;
        else if (words[1] == "printflow")
            m_mode = HeaderMode::PrintFlow;
        else {
            std::cout << "usage: abi vendor|printflow\n";
            return;
        }
        m_logger.event("default header ABI changed to " + modeName(m_mode));
    }

    void search()
    {
        PrinterInfoList list{};
        const int result = call("SearchPrinter", m_api.search, &list,
                                static_cast<int>(sizeof(list)));
        m_selected = -1;
        m_connected = false;
        if (result == kSdkSucceeded) {
            const int total = std::clamp(list.totalNum, 0, kMaxPrinters);
            std::cout << "printers: " << total << '\n';
            for (int i = 0; i < total; ++i)
                std::cout << "  [" << i << "] " << list.infoList[i] << '\n';
        }
        takeSnapshot("after-search", true);
    }

    void select(const std::vector<std::string>& words)
    {
        if (words.size() != 2) {
            std::cout << "usage: select INDEX\n";
            return;
        }
        const auto index = parseInteger(words[1]);
        if (!index || *index < 0 || *index > INT_MAX) {
            std::cout << "invalid printer index\n";
            return;
        }
        if (call("SelectPrinter", m_api.select, static_cast<int>(*index)) ==
            kSdkSucceeded) {
            m_selected = static_cast<int>(*index);
            m_connected = false;
        }
        takeSnapshot("after-select", true);
    }

    void connect()
    {
        if (call("ConnectPrinter", m_api.connect) == kSdkSucceeded)
            m_connected = true;
        takeSnapshot("after-connect", true);
    }

    void showStatus()
    {
        PrinterStatus status{};
        if (call("GetPrinterStatus", m_api.status, &status,
                 static_cast<int>(sizeof(status))) == kSdkSucceeded) {
            std::cout << "PrintStatus=" << status.PrintStatus
                      << " (0 standby, 1 printing, 2 pause, 3 resume, 4 cancel, 5 error)"
                      << ", CleanStatus=" << status.CleanStatus << '\n';
        }
    }

    void showInfo()
    {
        PrinterInfo info{};
        if (call("GetPrinterInfo", m_api.info, &info,
                 static_cast<int>(sizeof(info))) != kSdkSucceeded)
            return;
        std::cout << "main FPGA=" << info.Mainboard_fpgaVer << '/'
                  << static_cast<char>(info.Mainboard_fpgaExVer) << '/'
                  << static_cast<unsigned int>(info.Mainboard_fpgaSubVer)
                  << ", car FPGA=" << info.Carboard_fpgaVer << '/'
                  << static_cast<char>(info.Carboard_fpgaExVer) << '/'
                  << static_cast<unsigned int>(info.Carboard_fpgaSubVer)
                  << "\nmain CPU=" << info.Mainboard_cpuVer << '/'
                  << static_cast<char>(info.Mainboard_cpuExVer) << '/'
                  << static_cast<unsigned int>(info.Mainboard_cpuSubVer)
                  << ", car CPU=" << info.Carboard_cpuVer << '/'
                  << static_cast<char>(info.Carboard_cpuExVer) << '/'
                  << static_cast<unsigned int>(info.Carboard_cpuSubVer)
                  << "\nCarParaCRC=" << hexValue(info.CarParaCRC)
                  << ", UI CRCs=" << hexValue(info.UI_CRC) << ','
                  << hexValue(info.UI_CRC2) << ", IDs="
                  << hexValue(info.ID1) << ',' << hexValue(info.ID2) << '\n';
    }

    void height(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() == 2 && words[1] == "get") {
            std::uint16_t raw = 0;
            if (call("GetPrintHeight", m_api.getHeight, &raw) == kSdkSucceeded)
                std::cout << "print height=" << std::fixed << std::setprecision(1)
                          << static_cast<double>(raw) / 10.0 << " mm (raw="
                          << raw << ")\n";
            return;
        }
        if (words.size() == 3 && words[1] == "set") {
            const auto mm = parseDouble(words[2]);
            if (!mm || *mm < 0.0 || *mm > 6553.5) {
                std::cout << "height must be 0.0..6553.5 mm\n";
                return;
            }
            if (!mutationAllowed(execute, "height set"))
                return;
            const auto raw = static_cast<std::uint16_t>(*mm * 10.0 + 0.5);
            call("SetPrintHeight(" + std::to_string(raw) + " raw)",
                 m_api.setHeight, raw);
            return;
        }
        std::cout << "usage: height get | height set MM --execute\n";
    }

    void xy(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() == 2 && words[1] == "get") {
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            if (call("GetPrintXYValue", m_api.getXY, &x, &y) == kSdkSucceeded) {
                std::cout << std::fixed << std::setprecision(2)
                          << "X=" << static_cast<double>(x) / 100.0 << " mm"
                          << ", Y=" << static_cast<double>(y) / 100.0 << " mm"
                          << " (raw X=" << x << ", Y=" << y << ")\n";
            }
            return;
        }
        if (words.size() == 4 && words[1] == "set-mm") {
            const auto xMm = parseDouble(words[2]);
            const auto yMm = parseDouble(words[3]);
            const auto xRaw = xMm ? millimetersToHundredths(*xMm) : std::nullopt;
            const auto yRaw = yMm ? millimetersToHundredths(*yMm) : std::nullopt;
            if (!xRaw || !yRaw) {
                std::cout << "X_MM and Y_MM must be nonnegative values representable as uint32 hundredths of a millimeter\n";
                return;
            }
            if (mutationAllowed(execute, "xy set-mm")) {
                call("SetPrintXYValue(" + std::to_string(*xRaw) + "," +
                         std::to_string(*yRaw) + " raw hundredths)",
                     m_api.setXY, *xRaw, *yRaw);
            }
            return;
        }
        if (words.size() == 4 && words[1] == "set-raw") {
            const auto x = parseInteger(words[2]);
            const auto y = parseInteger(words[3]);
            if (!x || !y || *x < 0 || *y < 0 ||
                static_cast<unsigned long long>(*x) > UINT32_MAX ||
                static_cast<unsigned long long>(*y) > UINT32_MAX) {
                std::cout << "X and Y must be uint32 values\n";
                return;
            }
            if (mutationAllowed(execute, "xy set-raw"))
                call("SetPrintXYValue", m_api.setXY,
                     static_cast<std::uint32_t>(*x),
                     static_cast<std::uint32_t>(*y));
            return;
        }
        std::cout << "usage: xy get | xy set-mm X_MM Y_MM --execute | xy set-raw X Y --execute\n";
    }

    void showJob(const JobSettings& job) const
    {
        std::cout << "direction=" << job.PrintDirection
                  << " speed=" << job.PrintSpeed << " wc=" << job.WCSequence
                  << " eclosion=" << job.EclosionGrade
                  << " head=" << job.HeadSelect
                  << " white_percent=" << job.WInkPercent
                  << " white_pass=" << job.WInkPassCount
                  << " varnish_percent=" << job.VInkPercent
                  << " varnish_pass=" << job.VInkPassCount
                  << " voltage=" << job.HeadVoltage
                  << " car_reset=" << job.CarReset
                  << " strip_blank=" << job.stripBlank
                  << " blank_distance=" << job.blankDistance << " uv_disable=";
        for (const auto value : job.DisableUVLights)
            std::cout << static_cast<unsigned int>(value);
        std::cout << '\n';
    }

    bool getJob(JobSettings& job)
    {
        return call("GetJobSettings", m_api.getJob, &job,
                    static_cast<int>(sizeof(job))) == kSdkSucceeded;
    }

    void job(const std::vector<std::string>& words, bool execute)
    {
        JobSettings settings{};
        if (words.size() == 2 && words[1] == "get") {
            if (getJob(settings))
                showJob(settings);
            return;
        }
        if (words.size() < 3 || words[1] != "set") {
            std::cout << "usage: job get | job set KEY=VALUE... --execute\n";
            return;
        }
        if (!mutationAllowed(execute, "job set") || !getJob(settings))
            return;
        bool valid = true;
        for (std::size_t i = 2; i < words.size(); ++i) {
            const auto equals = words[i].find('=');
            if (equals == std::string::npos) {
                valid = false;
                break;
            }
            const std::string key = words[i].substr(0, equals);
            const auto number = parseInteger(words[i].substr(equals + 1));
            if (!number || *number < 0 || *number > 65535) {
                valid = false;
                break;
            }
            const auto value = static_cast<std::uint16_t>(*number);
            if (key == "direction") settings.PrintDirection = value;
            else if (key == "speed") settings.PrintSpeed = value;
            else if (key == "wc") settings.WCSequence = value;
            else if (key == "eclosion") settings.EclosionGrade = value;
            else if (key == "head") settings.HeadSelect = value;
            else if (key == "white_percent") settings.WInkPercent = value;
            else if (key == "white_pass") settings.WInkPassCount = value;
            else if (key == "varnish_percent") settings.VInkPercent = value;
            else if (key == "varnish_pass") settings.VInkPassCount = value;
            else if (key == "voltage") settings.HeadVoltage = value;
            else if (key == "car_reset") settings.CarReset = value;
            else if (key == "strip_blank") settings.stripBlank = value;
            else if (key == "blank_distance") settings.blankDistance = value;
            else if (key.rfind("uv", 0) == 0 && key.size() == 3 &&
                     key[2] >= '0' && key[2] <= '5' && value <= 255)
                settings.DisableUVLights[key[2] - '0'] = static_cast<unsigned char>(value);
            else {
                std::cout << "unknown job key: " << key << '\n';
                valid = false;
                break;
            }
        }
        if (!valid) {
            std::cout << "invalid KEY=VALUE job update\n";
            return;
        }
        showJob(settings);
        call("SetJobSettings", m_api.setJob, &settings,
             static_cast<int>(sizeof(settings)));
    }

    void showAlignment(const AlignmentValues& value) const
    {
        std::cout << "step=" << value.StepValue
                  << " bidi=" << static_cast<unsigned int>(value.BidiValue)
                  << " href=" << static_cast<unsigned int>(value.HorizontalAlignReference)
                  << " vref=" << static_cast<unsigned int>(value.VerticalAlignReference)
                  << "\nhspace:";
        for (const auto item : value.HorizontalSpacing) std::cout << ' ' << item;
        std::cout << "\nvspace:";
        for (const auto item : value.VerticalSpacing) std::cout << ' ' << item;
        const std::array<const char*, 8> arrays = {
            value.LeftChannelAlign_H1, value.LeftChannelAlign_H2,
            value.LeftChannelAlign_H3, value.LeftChannelAlign_H4,
            value.RightChannelAlign_H1, value.RightChannelAlign_H2,
            value.RightChannelAlign_H3, value.RightChannelAlign_H4};
        const std::array<const char*, 8> names = {
            "left1", "left2", "left3", "left4",
            "right1", "right2", "right3", "right4"};
        for (std::size_t group = 0; group < arrays.size(); ++group) {
            std::cout << '\n' << names[group] << ':';
            for (int i = 0; i < 8; ++i)
                std::cout << ' ' << static_cast<int>(arrays[group][i]);
        }
        std::cout << '\n';
    }

    void alignment(const std::vector<std::string>& words, bool execute)
    {
        AlignmentValues values{};
        if (words.size() == 2 && words[1] == "get") {
            if (call("GetAlignmentValues", m_api.getAlign, &values,
                     static_cast<int>(sizeof(values))) == kSdkSucceeded)
                showAlignment(values);
            return;
        }
        if (words.size() < 4 || words[1] != "set") {
            std::cout << "usage: align set TYPE VALUE [INDEX] --execute\n";
            return;
        }
        if (!mutationAllowed(execute, "align set") ||
            call("GetAlignmentValues", m_api.getAlign, &values,
                 static_cast<int>(sizeof(values))) != kSdkSucceeded)
            return;
        const std::string typeName = words[2];
        const auto value = parseInteger(words[3]);
        const auto parsedIndex = words.size() > 4 ? parseInteger(words[4])
                                                  : std::optional<long long>{};
        const bool hasIndex = parsedIndex.has_value();
        const long long index = parsedIndex.value_or(-1);
        if (!value) {
            std::cout << "invalid alignment value\n";
            return;
        }
        int apiType = -1;
        if (typeName == "step" && *value >= 0 &&
            static_cast<unsigned long long>(*value) <= UINT32_MAX) {
            values.StepValue = static_cast<std::uint32_t>(*value);
            apiType = 0;
        } else if (typeName == "bidi" && *value >= 0 && *value <= 255) {
            values.BidiValue = static_cast<unsigned char>(*value);
            apiType = 1;
        } else if ((typeName == "hspace" || typeName == "vspace") && hasIndex &&
                   index >= 0 && index < 4 && *value >= INT16_MIN &&
                   *value <= INT16_MAX) {
            auto* array = typeName == "hspace" ? values.HorizontalSpacing
                                                : values.VerticalSpacing;
            array[index] = static_cast<std::int16_t>(*value);
            apiType = typeName == "hspace" ? 2 : 3;
        } else if ((typeName.rfind("left", 0) == 0 ||
                    typeName.rfind("right", 0) == 0) && hasIndex &&
                   index >= 0 && index < 8 && *value >= -128 && *value <= 127 &&
                   typeName.size() == (typeName[0] == 'l' ? 5u : 6u)) {
            const char groupChar = typeName.back();
            if (groupChar < '1' || groupChar > '4') {
                std::cout << "channel group must be left1..4 or right1..4\n";
                return;
            }
            char* groups[] = {values.LeftChannelAlign_H1, values.LeftChannelAlign_H2,
                              values.LeftChannelAlign_H3, values.LeftChannelAlign_H4,
                              values.RightChannelAlign_H1, values.RightChannelAlign_H2,
                              values.RightChannelAlign_H3, values.RightChannelAlign_H4};
            const int base = typeName[0] == 'l' ? 0 : 4;
            groups[base + groupChar - '1'][index] = static_cast<char>(*value);
            apiType = typeName[0] == 'l' ? 4 : 5;
        }
        if (apiType < 0) {
            std::cout << "invalid type/value/index; types: step, bidi, hspace, vspace, left1..4, right1..4\n";
            return;
        }
        call("SetAlignmentValues", m_api.setAlign, &values, apiType,
             static_cast<int>(sizeof(values)));
    }

    void uv(const std::vector<std::string>& words, bool execute)
    {
        UVParamValues values{};
        if (words.size() == 2 && words[1] == "get") {
            if (call("GetUVParamValues", m_api.getUV, &values,
                     static_cast<int>(sizeof(values))) == kSdkSucceeded) {
                std::cout << "right_r2l=" << values.RightR2LOffset
                          << " right_l2r=" << values.RightL2ROffset
                          << " left_r2l=" << values.LeftR2LOffset
                          << " left_l2r=" << values.LeftL2ROffset
                          << " lamp_l2r=" << values.LampL2ROffset << '\n';
            }
            return;
        }
        if (words.size() != 4 || words[1] != "set") {
            std::cout << "usage: uv set TYPE(0..4) VALUE --execute\n";
            return;
        }
        const auto type = parseInteger(words[2]);
        const auto value = parseInteger(words[3]);
        if (!type || !value || *type < 0 || *type > 4 ||
            *value < INT16_MIN || *value > INT16_MAX) {
            std::cout << "invalid UV type/value\n";
            return;
        }
        if (!mutationAllowed(execute, "uv set") ||
            call("GetUVParamValues", m_api.getUV, &values,
                 static_cast<int>(sizeof(values))) != kSdkSucceeded)
            return;
        std::array<std::int16_t, 5> updated = {
            values.RightR2LOffset, values.RightL2ROffset,
            values.LeftR2LOffset, values.LeftL2ROffset,
            values.LampL2ROffset};
        updated[static_cast<std::size_t>(*type)] =
            static_cast<std::int16_t>(*value);
        values = {updated[0], updated[1], updated[2], updated[3], updated[4]};
        call("SetUVParamValues", m_api.setUV, &values, static_cast<int>(*type),
             static_cast<int>(sizeof(values)));
    }

    void newUv(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() == 2 && words[1] == "support") {
            if (!m_api.supportsNewUV)
                std::cout << "new UV support query unavailable\n";
            else
                std::cout << "new UV supported="
                          << call("GetSupportNewUVParam", m_api.supportsNewUV)
                          << '\n';
            return;
        }
        NewUVParamValues values{};
        if (words.size() == 2 && words[1] == "get") {
            if (call("GetNewUVParamValues", m_api.getNewUV, &values,
                     static_cast<int>(sizeof(values))) == kSdkSucceeded) {
                const auto* first = &values.UVLampLeftStartOffset;
                std::cout << "new UV values:";
                for (int i = 0; i < 7; ++i) std::cout << ' ' << first[i];
                std::cout << '\n';
            }
            return;
        }
        if (words.size() == 3 && words[1] == "action") {
            const auto type = parseInteger(words[2]);
            if (!type || *type < 0 || *type > 8) {
                std::cout << "new UV action type must be 0..8\n";
                return;
            }
            if (mutationAllowed(execute, "newuv action"))
                call("SetNewUVParamFunction", m_api.newUVAction,
                     static_cast<int>(*type));
            return;
        }
        if (words.size() == 4 && words[1] == "set") {
            const auto type = parseInteger(words[2]);
            const auto value = parseInteger(words[3]);
            if (!type || !value || *type < 0 || *type > 6 ||
                *value < INT16_MIN || *value > INT16_MAX) {
                std::cout << "new UV type must be 0..6 and value must be int16\n";
                return;
            }
            if (!mutationAllowed(execute, "newuv set") ||
                call("GetNewUVParamValues", m_api.getNewUV, &values,
                     static_cast<int>(sizeof(values))) != kSdkSucceeded)
                return;
            std::array<std::int16_t, 7> updated = {
                values.UVLampLeftStartOffset, values.UVLampLeftEndOffset,
                values.UVLampLeftMinOffset, values.UVLampRightStartOffset,
                values.UVLampRightEndOffset, values.UVLampRightMinOffset,
                values.UVLampDelayDistance};
            updated[static_cast<std::size_t>(*type)] =
                static_cast<std::int16_t>(*value);
            values = {updated[0], updated[1], updated[2], updated[3],
                      updated[4], updated[5], updated[6]};
            call("SetNewUVParamValues", m_api.setNewUV, &values,
                 static_cast<int>(*type), static_cast<int>(sizeof(values)));
            return;
        }
        std::cout << "usage: newuv support|get|set TYPE VALUE --execute|action TYPE --execute\n";
    }

    void headCommand(const std::vector<std::string>& words, bool execute,
                     const std::string& name, Api::HeadMaskFn function, bool)
    {
        if (words.size() != 2) {
            std::cout << "usage: " << words[0] << " HEAD_MASK --execute\n";
            return;
        }
        const auto mask = parseInteger(words[1]);
        if (!mask || *mask < 0 || *mask > INT_MAX) {
            std::cout << "invalid head mask\n";
            return;
        }
        if (mutationAllowed(execute, words[0]))
            call(name, function, static_cast<int>(*mask));
    }

    void move(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() != 3) {
            std::cout << "usage: move AXIS(0..2) DIR(0..1) --execute\n";
            return;
        }
        const auto axis = parseInteger(words[1]);
        const auto direction = parseInteger(words[2]);
        if (!axis || !direction || *axis < 0 || *axis > 2 ||
            *direction < 0 || *direction > 1) {
            std::cout << "invalid axis or direction\n";
            return;
        }
        if (mutationAllowed(execute, "move"))
            call("MoveAxis", m_api.moveAxis, static_cast<int>(*axis),
                 static_cast<int>(*direction));
    }

    void axisPosition(const std::vector<std::string>& words, bool save,
                      bool execute)
    {
        if (words.size() != 2) {
            std::cout << "usage: " << words[0] << " AXIS\n";
            return;
        }
        const auto axis = parseInteger(words[1]);
        if (!axis || *axis < 0 || *axis > 2) {
            std::cout << "axis must be 0..2\n";
            return;
        }
        if (save && !mutationAllowed(execute, "save-axis"))
            return;
        int position = 0;
        const int result = call(save ? "SaveAxisPos" : "StopAxis",
                                save ? m_api.saveAxis : m_api.stopAxis,
                                static_cast<int>(*axis), &position);
        std::cout << "position=" << position << ", result=" << result << '\n';
    }

    void config(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() != 3 ||
            (words[1] != "export" && words[1] != "import")) {
            std::cout << "usage: config export PATH | config import PATH --execute\n";
            return;
        }
        std::vector<char> path(words[2].begin(), words[2].end());
        path.push_back('\0');
        if (words[1] == "export")
            call("ExportConfigFile", m_api.exportConfig, path.data());
        else if (mutationAllowed(execute, "config import"))
            call("ImportConfigFile", m_api.importConfig, path.data());
    }

    void pattern(const std::vector<std::string>& words, bool execute)
    {
        if (words.size() != 2) {
            std::cout << "usage: pattern TYPE(0..22) --execute\n";
            return;
        }
        const auto type = parseInteger(words[1]);
        if (!type || *type < 0 || *type > 22) {
            std::cout << "pattern type must be 0..22\n";
            return;
        }
        if (mutationAllowed(execute, "pattern"))
            call("PrintAlignmentPattern", m_api.pattern, static_cast<int>(*type));
    }

    void raw(const std::vector<std::string>& words, bool unsafe)
    {
        if (!unsafe) {
            std::cout << "raw network calls require --unsafe\n";
            return;
        }
        if (words.size() != 4 || (words[1] != "order" && words[1] != "data")) {
            std::cout << "usage: raw order|data CHANNEL HEX --unsafe\n";
            return;
        }
        const auto channel = parseInteger(words[2]);
        const auto payload = parseHex(words[3]);
        if (!channel || *channel < -128 || *channel > 255 || !payload) {
            std::cout << "invalid channel or hexadecimal payload\n";
            return;
        }
        std::array<char, kRawResponseCapacity> response{};
        std::uint32_t responseSize = static_cast<std::uint32_t>(response.size());
        std::vector<char> request(payload->begin(), payload->end());
        Api::NetFn function = words[1] == "order" ? m_api.netOrder : m_api.netData;
        const int result = call(words[1] == "order" ? "NetSendOrder" : "NetSendData",
                                function, request.data(),
                                static_cast<std::uint32_t>(request.size()),
                                response.data(), &responseSize,
                                static_cast<char>(*channel));
        const std::size_t safeSize = std::min<std::size_t>(responseSize, response.size());
        std::cout << "result=" << result << " responseSize=" << responseSize
                  << " response=" << hexBytes(
                         reinterpret_cast<unsigned char*>(response.data()), safeSize)
                  << '\n';
    }

    void trace(const std::vector<std::string>& words)
    {
        if (words.size() == 3 && words[1] == "interval") {
            const auto interval = parseInteger(words[2]);
            if (!interval || *interval < 1 || *interval > INT_MAX) {
                std::cout << "trace interval must be positive\n";
                return;
            }
            m_traceInterval.store(static_cast<int>(*interval));
            std::cout << "snapshot interval=" << *interval << " rows\n";
            return;
        }
        if (words.size() == 3 && words[1] == "window" && words[2] == "off") {
            m_traceFirst.store(0);
            m_traceLast.store(-1);
            std::cout << "detailed trace window disabled\n";
            return;
        }
        if (words.size() == 4 && words[1] == "window") {
            const auto first = parseInteger(words[2]);
            const auto last = parseInteger(words[3]);
            if (!first || !last || *first < 1 || *last < *first || *last > INT_MAX) {
                std::cout << "invalid one-based row window\n";
                return;
            }
            m_traceFirst.store(static_cast<int>(*first));
            m_traceLast.store(static_cast<int>(*last));
            std::cout << "detailed trace window=" << *first << ".." << *last << '\n';
            return;
        }
        std::cout << "interval=" << m_traceInterval.load() << ", window="
                  << m_traceFirst.load() << ".." << m_traceLast.load() << '\n';
    }

    void core(const std::vector<std::string>& words)
    {
        if (words.size() == 2 && words[1] == "on") {
            rlimit limit{RLIM_INFINITY, RLIM_INFINITY};
            const bool limitOk = setrlimit(RLIMIT_CORE, &limit) == 0;
            const bool dumpOk = prctl(PR_SET_DUMPABLE, 1) == 0;
            std::cout << "core dumps " << (limitOk && dumpOk ? "enabled" : "not fully enabled")
                      << "; no fatal handler is installed\n";
        } else if (words.size() == 2 && words[1] == "off") {
            rlimit limit{0, 0};
            const bool ok = setrlimit(RLIMIT_CORE, &limit) == 0;
            std::cout << "core dumps " << (ok ? "disabled" : "could not be disabled") << '\n';
        } else if (words.size() == 2 && words[1] == "status") {
            rlimit limit{};
            getrlimit(RLIMIT_CORE, &limit);
            std::ifstream pattern("/proc/sys/kernel/core_pattern");
            std::string value;
            std::getline(pattern, value);
            std::cout << "RLIMIT_CORE=";
            if (limit.rlim_cur == RLIM_INFINITY) std::cout << "unlimited";
            else std::cout << limit.rlim_cur;
            std::cout << ", dumpable=" << prctl(PR_GET_DUMPABLE)
                      << ", core_pattern=" << value << '\n';
        } else {
            std::cout << "usage: core on|off|status\n";
        }
    }

    void startWorker(std::vector<std::string> words, bool execute)
    {
        if (!mutationAllowed(execute, "print"))
            return;
        if (m_printing.load()) {
            std::cout << "a print upload is already active\n";
            return;
        }
        reapWorker();
        fs::path path = m_defaultPrn;
        HeaderMode mode = m_mode;
        std::optional<PrintOrigin> origin;
        for (std::size_t i = 1; i < words.size(); ++i) {
            if (words[i] == "vendor") mode = HeaderMode::Vendor;
            else if (words[i] == "printflow") mode = HeaderMode::PrintFlow;
            else if (words[i] == "xy-mm") {
                if (i + 2 >= words.size()) {
                    std::cout << "xy-mm requires X_MM and Y_MM\n";
                    return;
                }
                const auto xMm = parseDouble(words[++i]);
                const auto yMm = parseDouble(words[++i]);
                const auto xRaw = xMm ? millimetersToHundredths(*xMm) : std::nullopt;
                const auto yRaw = yMm ? millimetersToHundredths(*yMm) : std::nullopt;
                if (!xMm || !yMm || !xRaw || !yRaw) {
                    std::cout << "xy-mm values must be nonnegative millimeters representable as uint32 hundredths\n";
                    return;
                }
                origin = PrintOrigin{*xMm, *yMm, *xRaw, *yRaw};
            } else {
                path = words[i];
            }
        }
        const PrnInspection inspection = inspectPrn(path);
        if (!inspection.valid) {
            std::cout << "cannot print invalid PRN: " << inspection.error << '\n';
            return;
        }
        m_abortRequested.store(false);
        m_abortIssued.store(false);
        m_printing.store(true);
        m_worker = std::thread([this, inspection, mode, origin]() {
            printFile(inspection, mode, origin);
            m_printing.store(false);
        });
    }

    void printFile(const PrnInspection& inspection, HeaderMode mode,
                   const std::optional<PrintOrigin>& origin)
    {
        const SdkPrintJobProperty header =
            mode == HeaderMode::Vendor ? inspection.vendor : inspection.printflow;
        m_logger.event("print worker started: mode=" + modeName(mode) +
                       ", path=" + inspection.path.string());
        m_logger.event("StartPrint header: " + headerSummary(header));
        takeSnapshot("before-init", true);
        if (call("InitPrinter", m_api.init) != kSdkSucceeded) {
            m_logger.event("print stopped: InitPrinter failed");
            return;
        }
        takeSnapshot("after-init", true);

        if (origin) {
            // The iQueue SetPrinterXYValue contract uses uint32 hundredths of
            // a millimeter. The Linux SDK's corresponding exported entrypoint
            // is API_SetPrintXYValue and stores both uint32 values unchanged.
            if (call("SetPrintXYValue pre-StartPrint (X=" +
                         std::to_string(origin->xHundredthsMm) + ", Y=" +
                         std::to_string(origin->yHundredthsMm) + " raw)",
                     m_api.setXY, origin->xHundredthsMm,
                     origin->yHundredthsMm) != kSdkSucceeded) {
                m_logger.event("print stopped: pre-StartPrint XY setter failed");
                return;
            }
            std::uint32_t actualX = 0;
            std::uint32_t actualY = 0;
            if (call("GetPrintXYValue pre-StartPrint", m_api.getXY,
                     &actualX, &actualY) != kSdkSucceeded ||
                actualX != origin->xHundredthsMm ||
                actualY != origin->yHundredthsMm) {
                m_logger.event("print stopped: pre-StartPrint XY verification failed; expected " +
                               std::to_string(origin->xHundredthsMm) + "," +
                               std::to_string(origin->yHundredthsMm) + " raw; got " +
                               std::to_string(actualX) + "," +
                               std::to_string(actualY));
                return;
            }
            std::ostringstream originMessage;
            originMessage << std::fixed << std::setprecision(2)
                          << "verified pre-StartPrint origin " << origin->xMm
                          << " x " << origin->yMm << " mm (raw "
                          << actualX << " x " << actualY << ')';
            m_logger.event(originMessage.str());
            takeSnapshot("after-pre-start-origin", true);
        }

        SdkPrintJobProperty mutableHeader = header;
        if (call("StartPrint", m_api.startPrint, &mutableHeader) != kSdkSucceeded) {
            m_logger.event("print stopped: StartPrint failed");
            return;
        }
        takeSnapshot("after-start", true);

        std::ifstream input(inspection.path, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(sizeof(X33DiskHeader)));
        const std::size_t rowBytes = static_cast<std::size_t>(inspection.disk.Colors) *
                                     inspection.disk.BytesPerLine;
        std::vector<char> rowData(rowBytes);
        bool failed = false;
        for (std::uint32_t row = 0; row < inspection.disk.Height && !failed; ++row) {
            if (m_abortRequested.load()) {
                m_logger.event("abort requested before raster row " +
                               std::to_string(row + 1));
                failed = true;
                break;
            }
            if (!input.read(rowData.data(), static_cast<std::streamsize>(rowData.size()))) {
                m_logger.event("short PRN read at raster row " +
                               std::to_string(row + 1));
                failed = true;
                break;
            }
            for (std::uint16_t plane = 0; plane < inspection.disk.Colors; ++plane) {
                const int oneBasedRow = static_cast<int>(row + 1);
                const bool detailed = oneBasedRow >= m_traceFirst.load() &&
                                      oneBasedRow <= m_traceLast.load();
                const std::string callLabel = "row=" + std::to_string(row + 1) +
                                              " plane=" + std::to_string(plane + 1);
                if (detailed)
                    takeSnapshot("before-PrintALine " + callLabel, false);
                int written = 0;
                {
                    std::lock_guard<std::mutex> lock(m_sdkCallMutex);
                    written = m_api.printALine(
                        rowData.data() + static_cast<std::size_t>(plane) *
                                             inspection.disk.BytesPerLine,
                        inspection.disk.BytesPerLine);
                }
                if (detailed)
                    takeSnapshot("after-PrintALine " + callLabel +
                                     " result=" + std::to_string(written), false);
                if (written != static_cast<int>(inspection.disk.BytesPerLine)) {
                    m_logger.event("short SDK write at " + callLabel + ": " +
                                   std::to_string(written) + " of " +
                                   std::to_string(inspection.disk.BytesPerLine));
                    failed = true;
                    break;
                }
            }
            if ((row + 1) == 1 || (row + 1) == inspection.disk.Height ||
                (row + 1) % static_cast<std::uint32_t>(m_traceInterval.load()) == 0) {
                takeSnapshot("periodic-row-" + std::to_string(row + 1), false);
                m_logger.event("streamed raster row " + std::to_string(row + 1) +
                               " of " + std::to_string(inspection.disk.Height));
            } else {
                takeChangedSnapshot("changed-row-" + std::to_string(row + 1));
            }
        }

        if (failed || m_abortRequested.load()) {
            takeSnapshot("before-abort", true);
            if (!m_abortIssued.exchange(true))
                call("AbortPrint", m_api.abortPrint);
            takeSnapshot("after-abort", true);
            call("ClosePrint", m_api.closePrint);
            takeSnapshot("after-close-aborted", true);
            m_logger.event("print worker ended after abort/failure");
            return;
        }

        takeSnapshot("before-end", true);
        if (call("EndPrint", m_api.endPrint) != kSdkSucceeded)
            m_logger.event("EndPrint returned failure after complete PRN upload");
        takeSnapshot("after-end", true);
        if (call("ClosePrint", m_api.closePrint) != kSdkSucceeded)
            m_logger.event("ClosePrint returned failure after complete PRN upload");
        takeSnapshot("after-close", true);
        m_logger.event("print worker completed the entire PRN lifecycle");
    }

    void abortNow()
    {
        m_abortRequested.store(true);
        if (!m_abortIssued.exchange(true))
            call("AbortPrint", m_api.abortPrint);
    }

    void reapWorker()
    {
        if (m_worker.joinable() && !m_printing.load())
            m_worker.join();
    }

    void stopWorker()
    {
        if (m_worker.joinable()) {
            if (m_printing.load())
                abortNow();
            m_worker.join();
        }
    }

    std::uintptr_t symbolAddress(const ElfSymbol& symbol) const
    {
        return m_libraryBase + static_cast<std::uintptr_t>(symbol.value);
    }

    std::string scalarValue(const ElfSymbol& symbol) const
    {
        const auto address = symbolAddress(symbol);
        if (symbol.size == 1) {
            std::uint8_t value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(address), 1);
            return std::to_string(value);
        }
        if (symbol.size == 2) {
            std::uint16_t value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(address), 2);
            return std::to_string(value);
        }
        if (symbol.size == 4) {
            std::uint32_t value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(address), 4);
            return std::to_string(value) + " (" + hexValue(value) + ')';
        }
        if (symbol.size == 8) {
            std::uint64_t value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(address), 8);
            return std::to_string(value) + " (" + hexValue(value) + ')';
        }
        const std::size_t count = std::min<std::size_t>(symbol.size, 32);
        std::array<unsigned char, 32> bytes{};
        std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), count);
        return hexBytes(bytes.data(), count) + (symbol.size > count ? " ..." : "");
    }

    std::string u32Array(const ElfSymbol& symbol,
                         const std::vector<std::string>& labels = {}) const
    {
        const std::size_t count = static_cast<std::size_t>(symbol.size / 4);
        std::vector<std::uint32_t> values(count);
        std::memcpy(values.data(), reinterpret_cast<const void*>(symbolAddress(symbol)),
                    values.size() * sizeof(values[0]));
        std::ostringstream output;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) output << ", ";
            output << (i < labels.size() ? labels[i] : "u32[" + std::to_string(i) + "]")
                   << '=' << values[i];
        }
        return output.str();
    }

    std::map<std::string, std::string> snapshotValues() const
    {
        static const std::vector<std::string> scalarNames = {
            "gBytesPerLine", "gRealBytesPerLine", "gPrintSliceNum",
            "MaxSwathSize", "MaxFormatBufSize", "MAX_SWATHBUF_SIZE",
            "gTotalLines", "gLineInPtr", "gLineOutPtr", "TotalSwathNum",
            "total_swath", "act_slcie", "total_datasize", "gLinesPerPass",
            "gLinesPerStrip", "error_slice", "error_swath", "gfPrinterError",
            "net_error", "uart_error", "NewSwathIndex"};
        static const std::vector<std::string> pointerNames = {
            "FormatOutBuf", "FormatOutBuf2", "pCurSwath", "SwathOutBuf",
            "SrcImageBufC", "SrcImageBufM", "SrcImageBufY", "SrcImageBufK"};
        std::map<std::string, std::string> fields;
        for (const auto& name : scalarNames) {
            if (const auto* symbol = m_symbols.find(name))
                fields[name] = scalarValue(*symbol);
        }
        for (const auto& name : pointerNames) {
            if (const auto* symbol = m_symbols.find(name)) {
                std::uintptr_t pointer = 0;
                std::memcpy(&pointer,
                            reinterpret_cast<const void*>(symbolAddress(*symbol)),
                            std::min(sizeof(pointer), static_cast<std::size_t>(symbol->size)));
                fields[name] = hexValue(pointer);
            }
        }
        if (const auto* symbol = m_symbols.find("gJobProp")) {
            fields["gJobProp"] = u32Array(*symbol,
                {"Signature", "XDPI", "YDPI", "BytesPerLine", "Height", "Width",
                 "PaperWidth", "Colors", "Bits", "Pass", "VsdMode", "Reserved"});
        }
        if (const auto* symbol = m_symbols.find("gFmtInfo"))
            fields["gFmtInfo"] = u32Array(*symbol);
        if (const auto* symbol = m_symbols.find("gPassInfo"))
            fields["gPassInfo"] = u32Array(*symbol);
        const auto allocationValue = [&](const char* name) -> std::uint32_t {
            const auto* symbol = m_symbols.find(name);
            if (!symbol || symbol->size < 4)
                return 0;
            std::uint32_t value = 0;
            std::memcpy(&value,
                        reinterpret_cast<const void*>(symbolAddress(*symbol)), 4);
            return value;
        };
        fields["derived.format_allocation_bytes"] =
            std::to_string(allocationValue("MaxFormatBufSize"));
        const std::uint32_t maxSwath = allocationValue("MaxSwathSize");
        const std::uint32_t maxSwathBuffer = allocationValue("MAX_SWATHBUF_SIZE");
        fields["derived.swath_allocation_bytes"] =
            std::to_string(maxSwathBuffer != 0 ? maxSwathBuffer : maxSwath);
        return fields;
    }

    void takeSnapshot(const std::string& label, bool verbose)
    {
        const auto fields = snapshotValues();
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            m_lastSnapshot = fields;
        }
        m_logger.snapshot(label, fields, verbose);
    }

    void takeChangedSnapshot(const std::string& label)
    {
        const auto fields = snapshotValues();
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            changed = fields != m_lastSnapshot;
            if (changed)
                m_lastSnapshot = fields;
        }
        if (changed)
            m_logger.snapshot(label, fields, false);
    }

    void listSymbols(const std::string& filter, bool objectsOnly) const
    {
        for (const auto& symbol : m_symbols.entries()) {
            if (objectsOnly && symbol.type != STT_OBJECT)
                continue;
            if (!filter.empty() && symbol.name.find(filter) == std::string::npos)
                continue;
            std::cout << (symbol.type == STT_OBJECT ? "OBJECT " : "FUNC   ")
                      << (symbol.binding == STB_LOCAL ? "local  " : "global ")
                      << (symbol.dynamic ? "dyn " : "sym ")
                      << hexValue(symbolAddress(symbol), 16) << " size="
                      << symbol.size << ' ' << symbol.name << '\n';
        }
    }

    void globals(const std::vector<std::string>& words)
    {
        if (words.size() >= 2 && words[1] == "interesting") {
            takeSnapshot("globals-interesting", true);
            return;
        }
        if (words.size() >= 2 && words[1] == "all") {
            const std::string filter = words.size() > 2 ? words[2] : "";
            for (const auto& symbol : m_symbols.entries()) {
                if (symbol.type != STT_OBJECT ||
                    (!filter.empty() && symbol.name.find(filter) == std::string::npos))
                    continue;
                std::cout << (symbol.binding == STB_LOCAL ? "local  " : "global ")
                          << hexValue(symbolAddress(symbol), 16) << " size="
                          << symbol.size << ' ' << symbol.name << " = "
                          << scalarValue(symbol) << '\n';
            }
            return;
        }
        std::cout << "usage: globals interesting | globals all [FILTER]\n";
    }

    fs::path m_sdkRoot;
    fs::path m_defaultPrn;
    Logger m_logger;
    void* m_library = nullptr;
    std::uintptr_t m_libraryBase = 0;
    ElfSymbols m_symbols;
    Api m_api;
    std::mutex m_sdkCallMutex;
    std::mutex m_snapshotMutex;
    std::map<std::string, std::string> m_lastSnapshot;
    int m_selected = -1;
    bool m_connected = false;
    HeaderMode m_mode = HeaderMode::Vendor;
    std::thread m_worker;
    std::atomic<bool> m_printing{false};
    std::atomic<bool> m_abortRequested{false};
    std::atomic<bool> m_abortIssued{false};
    std::atomic<int> m_traceInterval{64};
    std::atomic<int> m_traceFirst{1900};
    std::atomic<int> m_traceLast{2600};
};

fs::path executableDirectory(const char* argv0)
{
    std::error_code error;
    const fs::path executable = fs::canonical("/proc/self/exe", error);
    if (!error)
        return executable.parent_path();
    const fs::path fallback = fs::absolute(argv0, error);
    return error ? fs::current_path() : fallback.parent_path();
}

void printUsage(const char* argv0)
{
    std::cout << "usage: " << argv0
              << " [--sdk-root DIR] [--prn FILE] [--log-dir DIR]\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path sdkRoot = executableDirectory(argv[0]);
    fs::path prn = "/home/xante-admin/Downloads/TestPrint.prn";
    fs::path logDirectory = sdkRoot / "logs";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--sdk-root" || argument == "--prn" ||
             argument == "--log-dir") && i + 1 < argc) {
            const fs::path value = argv[++i];
            if (argument == "--sdk-root") sdkRoot = value;
            else if (argument == "--prn") prn = value;
            else logDirectory = value;
        } else if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown/incomplete argument: " << argument << '\n';
            printUsage(argv[0]);
            return 2;
        }
    }

    Harness harness(fs::absolute(sdkRoot), fs::absolute(prn),
                    fs::absolute(logDirectory));
    if (!harness.load())
        return 1;
    harness.repl();
    return 0;
}
