#include "NocaiDirectPrintClient.h"
#include "NocaiDirectPrintCompatibility.h"
#include "NocaiPrnWriter.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QDataStream>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace {
static constexpr int kSySucceeded = 0x01;
static constexpr int kMaxPrinters = 100;
static constexpr uint32_t kPrintSignature = 0x00005555u;
static constexpr quint32 kWorkerJobMagic = 0x50464e57u; // "PFNW"
static constexpr quint32 kWorkerJobVersion = 3;

std::recursive_mutex g_sdkStdoutMutex;

static bool isRoutineSdkOutput(const QString& line)
{
    if (line.startsWith(QStringLiteral("netdevice ")) ||
        line.startsWith(QStringLiteral("IPTable:")) ||
        line.startsWith(QStringLiteral("gvcp"), Qt::CaseInsensitive)) {
        return true;
    }

    // Known localized success/progress output from the supplied SDK. These
    // messages add no actionable information because every API result is
    // already checked and logged by the adapter in English.
    return line == QStringLiteral("成功加载所有函数") ||
        line.startsWith(QStringLiteral("成功申请到端口号")) ||
        line == QStringLiteral("gvcp第一阶段完成") ||
        line == QStringLiteral("GVCP搜索完成") ||
        line == QStringLiteral("arp发送完成");
}

static void emitNormalizedSdkOutput(const QByteArray& captured)
{
    const QList<QByteArray> rawLines = captured.split('\n');
    for (QByteArray rawLine : rawLines) {
        if (rawLine.endsWith('\r'))
            rawLine.chop(1);
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || isRoutineSdkOutput(line))
            continue;

        // Do not leak untranslated vendor text into the operator log. Known
        // routine messages are handled above; unknown localized output is
        // suppressed while the adapter's checked English API result remains.
        const bool containsNonAscii = std::any_of(
            rawLine.cbegin(), rawLine.cend(),
            [](char ch) { return static_cast<unsigned char>(ch) >= 0x80; });
        if (!containsNonAscii)
            qInfo().noquote() << "Nocai SDK:" << line;
    }
}

class ScopedSdkStdoutCapture
{
public:
    ScopedSdkStdoutCapture()
        : m_lock(g_sdkStdoutMutex)
    {
#if defined(Q_OS_UNIX)
        std::cout.flush();
        std::fflush(nullptr);
        m_originalFd = ::dup(STDOUT_FILENO);
        m_capture = std::tmpfile();
        if (m_originalFd >= 0 && m_capture &&
            ::dup2(::fileno(m_capture), STDOUT_FILENO) >= 0) {
            m_active = true;
        } else {
            restore();
        }
#endif
    }

    ~ScopedSdkStdoutCapture()
    {
        emitNormalizedSdkOutput(finish());
    }

private:
    QByteArray finish()
    {
        QByteArray captured;
#if defined(Q_OS_UNIX)
        if (!m_active)
            return captured;

        std::cout.flush();
        std::fflush(nullptr);
        ::dup2(m_originalFd, STDOUT_FILENO);
        ::close(m_originalFd);
        m_originalFd = -1;
        m_active = false;

        std::rewind(m_capture);
        char buffer[4096];
        while (const size_t count = std::fread(buffer, 1, sizeof(buffer), m_capture))
            captured.append(buffer, static_cast<qsizetype>(count));
        std::fclose(m_capture);
        m_capture = nullptr;
#endif
        return captured;
    }

    void restore()
    {
#if defined(Q_OS_UNIX)
        if (m_originalFd >= 0) {
            ::dup2(m_originalFd, STDOUT_FILENO);
            ::close(m_originalFd);
            m_originalFd = -1;
        }
        if (m_capture) {
            std::fclose(m_capture);
            m_capture = nullptr;
        }
        m_active = false;
#endif
    }

    std::unique_lock<std::recursive_mutex> m_lock;
#if defined(Q_OS_UNIX)
    int m_originalFd = -1;
    FILE* m_capture = nullptr;
    bool m_active = false;
#endif
};

struct ScopedCurrentDir
{
    explicit ScopedCurrentDir(const QString& next)
        : previous(QDir::currentPath()),
          changed(QDir::setCurrent(next))
    {
    }

    ~ScopedCurrentDir()
    {
        if (changed)
            QDir::setCurrent(previous);
    }

    QString previous;
    bool changed = false;
};

static int clampInt(int value, int lo, int hi)
{
    return std::max(lo, std::min(hi, value));
}

static uint32_t millimetersToPrintXYUnits(int millimeters)
{
    // The X-33 SetPrinterXYValue contract used by iQueue, and the equivalent
    // Linux API_SetPrintXYValue export, represent 0.01 mm per uint32 unit.
    const uint64_t nonnegativeMm = static_cast<uint64_t>(std::max(0, millimeters));
    return static_cast<uint32_t>(std::min<uint64_t>(
        nonnegativeMm * 100u, std::numeric_limits<uint32_t>::max()));
}

template<typename T>
static T* sdkDataAddress(QFunctionPointer symbol)
{
    return symbol
        ? reinterpret_cast<T*>(reinterpret_cast<quintptr>(symbol))
        : nullptr;
}
}

struct NocaiDirectPrintClient::PrinterInfoList
{
    int totalNum = 0;
    char infoList[kMaxPrinters][256] = {};
};

struct NocaiDirectPrintClient::PrintJobProperty
{
    uint32_t Signature = 0;
    uint32_t XDPI = 0;
    uint32_t YDPI = 0;
    uint32_t BytesPerLine = 0;
    uint32_t Height = 0;
    uint32_t Width = 0;
    uint32_t PaperWidth = 0;
    // The x64 demo converts the packed PRN header's WORD fields to DWORDs
    // before calling API_StartPrint. This is the SDK-facing ABI, not the
    // packed on-disk PRN header layout documented by the ARM package.
    uint32_t Colors = 0;
    uint32_t Bits = 0;
    uint32_t Pass = 0;
    uint32_t VsdMode = 0;
    uint32_t Reserved = 0;
};

struct NocaiDirectPrintClient::JobSettings
{
    uint16_t PrintDirection = 0;
    uint16_t PrintSpeed = 1;
    uint16_t WCSequence = 0;
    uint16_t EclosionGrade = 0;
    uint16_t HeadSelect = 0;
    uint16_t WInkPercent = 0;
    uint16_t WInkPassCount = 0;
    uint16_t VInkPercent = 0;
    uint16_t VInkPassCount = 0;
    uint16_t HeadVoltage = 512;
    unsigned char DisableUVLights[6] = {0, 0, 0, 0, 0, 0};
    uint16_t CarReset = 1;
    uint16_t stripBlank = 0;
    uint16_t blankDistance = 0;
};

struct NocaiDirectPrintClient::AlignmentValues
{
    uint32_t StepValue = 0;
    unsigned char BidiValue = 0;
    int16_t HorizontalSpacing[4] = {0, 0, 0, 0};
    int16_t VerticalSpacing[4] = {0, 0, 0, 0};
    unsigned char HorizontalAlignReference = 0;
    unsigned char VerticalAlignReference = 0;
    char LeftChannelAlign_H1[8] = {};
    char LeftChannelAlign_H2[8] = {};
    char LeftChannelAlign_H3[8] = {};
    char LeftChannelAlign_H4[8] = {};
    char RightChannelAlign_H1[8] = {};
    char RightChannelAlign_H2[8] = {};
    char RightChannelAlign_H3[8] = {};
    char RightChannelAlign_H4[8] = {};
};

struct NocaiDirectPrintClient::PrinterStatus
{
    uint16_t PrintStatus = 0;
    uint16_t CleanStatus = 0;
};

struct NocaiDirectPrintClient::PrinterInfo
{
    uint16_t Mainboard_fpgaVer = 0;
    unsigned char Mainboard_fpgaExVer = 0;
    unsigned char Mainboard_fpgaSubVer = 0;
    uint16_t Carboard_fpgaVer = 0;
    unsigned char Carboard_fpgaExVer = 0;
    unsigned char Carboard_fpgaSubVer = 0;
    uint16_t Mainboard_cpuVer = 0;
    unsigned char Mainboard_cpuExVer = 0;
    unsigned char Mainboard_cpuSubVer = 0;
    uint16_t Carboard_cpuVer = 0;
    unsigned char Carboard_cpuExVer = 0;
    unsigned char Carboard_cpuSubVer = 0;
    uint32_t CarParaCRC = 0;
    uint16_t UI_CRC = 0;
    uint16_t UI_CRC2 = 0;
    unsigned char ID1 = 0;
    unsigned char ID2 = 0;
};

struct NocaiDirectPrintClient::UVParamValues
{
    int16_t RightR2LOffset = 0;
    int16_t RightL2ROffset = 0;
    int16_t LeftR2LOffset = 0;
    int16_t LeftL2ROffset = 0;
    int16_t LampL2ROffset = 0;
};

struct NocaiDirectPrintClient::NewUVParamValues
{
    int16_t UVLampLeftStartOffset = 0;
    int16_t UVLampLeftEndOffset = 0;
    int16_t UVLampLeftMinOffset = 0;
    int16_t UVLampRightStartOffset = 0;
    int16_t UVLampRightEndOffset = 0;
    int16_t UVLampRightMinOffset = 0;
    int16_t UVLampDelayDistance = 0;
};

NocaiDirectPrintClient::NocaiDirectPrintClient(QObject* parent)
    : QObject(parent)
{
    static_assert(sizeof(PrintJobProperty) == 48, "Vendor PrintJobProperty ABI changed.");
    static_assert(offsetof(PrintJobProperty, Colors) == 28, "Vendor Colors ABI offset changed.");
    static_assert(offsetof(PrintJobProperty, Bits) == 32, "Vendor Bits ABI offset changed.");
    static_assert(offsetof(PrintJobProperty, Pass) == 36, "Vendor Pass ABI offset changed.");
    static_assert(sizeof(JobSettings) == 32, "Vendor JobSettings ABI changed.");
    static_assert(sizeof(AlignmentValues) == 88, "Vendor AlignmentValues ABI changed.");
    static_assert(sizeof(PrinterStatus) == 4, "Vendor PrinterStatus ABI changed.");
    static_assert(sizeof(PrinterInfo) == 28, "Vendor PrinterInfo ABI changed.");
    static_assert(sizeof(UVParamValues) == 10, "Vendor UVParamValues ABI changed.");
    static_assert(sizeof(NewUVParamValues) == 14, "Vendor NewUVParamValues ABI changed.");
}

NocaiDirectPrintClient::~NocaiDirectPrintClient()
{
    QMutexLocker locker(&m_mutex);
    NocaiDirectPrintCompatibility::uninstall(this);
    m_library.unload();
}

bool NocaiDirectPrintClient::isAvailable()
{
    QMutexLocker locker(&m_mutex);
    return ensureLoaded();
}

bool NocaiDirectPrintClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected;
}

QString NocaiDirectPrintClient::vendorName() const
{
    return QStringLiteral("Vendor direct print");
}

QString NocaiDirectPrintClient::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

QString NocaiDirectPrintClient::sdkRootPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_sdkRootPath;
}

void NocaiDirectPrintClient::setSdkRootPath(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    const QString clean = path.trimmed();
    if (m_sdkRootPath == clean)
        return;

    NocaiDirectPrintCompatibility::uninstall(this);
    m_sdkRootPath = clean;
    m_resolvedSdkRoot.clear();
    m_symbolsResolved = false;
    m_selectedPrinterIndex = -1;
    m_connected = false;
    m_printers.clear();
    m_library.unload();
    m_searchPrinter = nullptr;
    m_choosePrinter = nullptr;
    m_initPrinter = nullptr;
    m_startPrint = nullptr;
    m_printALine = nullptr;
    m_abortPrint = nullptr;
    m_pausePrint = nullptr;
    m_continuePrint = nullptr;
    m_endPrint = nullptr;
    m_closePrint = nullptr;
    m_setJobSettings = nullptr;
    m_getJobSettings = nullptr;
    m_connectPrinter = nullptr;
    m_wipePrintHead = nullptr;
    m_startCleanOperation = nullptr;
    m_startPump = nullptr;
    m_stopPumpOperation = nullptr;
    m_spitPrintHead = nullptr;
    m_stopSpitOperation = nullptr;
    m_capPrintHead = nullptr;
    m_moveAxis = nullptr;
    m_stopAxis = nullptr;
    m_saveAxisPos = nullptr;
    m_setPrintHeight = nullptr;
    m_getPrintHeight = nullptr;
    m_setAlignmentValues = nullptr;
    m_getAlignmentValues = nullptr;
    m_exportConfigFile = nullptr;
    m_importConfigFile = nullptr;
    m_printAlignmentPattern = nullptr;
    m_getPrinterStatus = nullptr;
    m_getPrinterInfo = nullptr;
    m_setPrintXYValue = nullptr;
    m_getPrintXYValue = nullptr;
    m_setUVParamValues = nullptr;
    m_getUVParamValues = nullptr;
    m_getSupportNewUVParamFunction = nullptr;
    m_setNewUVParamFunction = nullptr;
    m_setNewUVParamValues = nullptr;
    m_getNewUVParamValues = nullptr;
    emit statusChanged();
}

bool NocaiDirectPrintClient::autoDiscoverSdk() const
{
    QMutexLocker locker(&m_mutex);
    return m_autoDiscoverSdk;
}

void NocaiDirectPrintClient::setAutoDiscoverSdk(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    if (m_autoDiscoverSdk == enabled)
        return;

    m_autoDiscoverSdk = enabled;
    m_resolvedSdkRoot.clear();
    m_symbolsResolved = false;
    m_selectedPrinterIndex = -1;
    m_connected = false;
    m_printers.clear();
    NocaiDirectPrintCompatibility::uninstall(this);
    m_library.unload();
    emit statusChanged();
}

QVariantList NocaiDirectPrintClient::printers() const
{
    QMutexLocker locker(&m_mutex);
    return m_printers;
}

QStringList NocaiDirectPrintClient::maintenanceSupportedPrinters() const
{
    return {
        QStringLiteral("X-33")
    };
}

bool NocaiDirectPrintClient::supportsMaintenance(const QString& printerName) const
{
    return maintenanceSupportedPrinters().contains(printerName.trimmed(), Qt::CaseInsensitive);
}

QVariantList NocaiDirectPrintClient::searchPrinters()
{
    refreshPrinters();
    return printers();
}

bool NocaiDirectPrintClient::refreshPrinters()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;

    // The vendor demo always treats a new search as the start of a fresh
    // Search -> Select -> Connect sequence.
    m_selectedPrinterIndex = -1;
    m_connected = false;
    bool ok = false;
    PrinterInfoList list;
    withSdkWorkingDirectory([&]() {
        const int result = m_searchPrinter(&list, sizeof(PrinterInfoList));
        ok = callSucceeded(result, "SearchPrinter");
        return ok;
    });

    QVariantList found;
    if (ok) {
        const int total = clampInt(list.totalNum, 0, kMaxPrinters);
        for (int i = 0; i < total; ++i) {
            QVariantMap entry;
            entry["index"] = i;
            entry["name"] = QString::fromLocal8Bit(list.infoList[i]).trimmed();
            found.append(entry);
        }
    }

    m_printers = found;
    if (m_selectedPrinterIndex < 0 || m_selectedPrinterIndex >= m_printers.size())
        m_selectedPrinterIndex = -1;
    emit printersChanged();
    emit statusChanged();
    return ok;
}

bool NocaiDirectPrintClient::choosePrinter(int index)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;
    if (index == m_selectedPrinterIndex)
        return true;

    bool ok = false;
    withSdkWorkingDirectory([&]() {
        ok = callSucceeded(m_choosePrinter(index), "ChoosePrinter");
        if (ok)
            m_selectedPrinterIndex = index;
        if (ok)
            m_connected = false;
        return ok;
    });
    emit statusChanged();
    return ok;
}

bool NocaiDirectPrintClient::abortPrint()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;
    return callSucceeded(m_abortPrint(), "AbortPrint");
}

bool NocaiDirectPrintClient::pausePrint()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;
    return callSucceeded(m_pausePrint(), "PausePrint");
}

bool NocaiDirectPrintClient::continuePrint()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;
    return callSucceeded(m_continuePrint(), "ContinuePrint");
}

bool NocaiDirectPrintClient::connectPrinter()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_connectPrinter), "ConnectPrinter"))
        return false;

    if (m_selectedPrinterIndex < 0) {
        if (m_printers.size() != 1) {
            setError(m_printers.isEmpty()
                         ? QStringLiteral("Search for an SDK printer before connecting.")
                         : QStringLiteral("Select an SDK printer before connecting."));
            emit statusChanged();
            return false;
        }

        const int onlyIndex = m_printers.first().toMap().value(QStringLiteral("index"), 0).toInt();
        if (!choosePrinter(onlyIndex))
            return false;
    }

    bool ok = false;
    withSdkWorkingDirectory([&]() {
        ok = callSucceeded(m_connectPrinter(), "ConnectPrinter");
        return ok;
    });
    m_connected = ok;
    if (!ok)
        m_selectedPrinterIndex = -1;
    emit statusChanged();
    return ok;
}

bool NocaiDirectPrintClient::wipePrintHead(int printHeadMask)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_wipePrintHead), "WipePrintHead"))
        return false;
    return callSucceeded(m_wipePrintHead(printHeadMask), "WipePrintHead");
}

bool NocaiDirectPrintClient::startCleanOperation(int printHeadMask)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_startCleanOperation), "StartCleanOperation"))
        return false;
    return callSucceeded(m_startCleanOperation(printHeadMask), "StartCleanOperation");
}

bool NocaiDirectPrintClient::startPump(int printHeadMask)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_startPump), "StartPump"))
        return false;
    return callSucceeded(m_startPump(printHeadMask), "StartPump");
}

bool NocaiDirectPrintClient::stopPumpOperation()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_stopPumpOperation), "StopPumpOperation"))
        return false;
    return callSucceeded(m_stopPumpOperation(), "StopPumpOperation");
}

bool NocaiDirectPrintClient::spitPrintHead(int printHeadMask)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_spitPrintHead), "SpitPrintHead"))
        return false;
    return callSucceeded(m_spitPrintHead(printHeadMask), "SpitPrintHead");
}

bool NocaiDirectPrintClient::stopSpitOperation()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_stopSpitOperation), "StopSpitOperation"))
        return false;
    return callSucceeded(m_stopSpitOperation(), "StopSpitOperation");
}

bool NocaiDirectPrintClient::capPrintHead()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_capPrintHead), "CapPrintHead"))
        return false;
    return callSucceeded(m_capPrintHead(), "CapPrintHead");
}

bool NocaiDirectPrintClient::moveAxis(int axis, int direction)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_moveAxis), "MoveAxis"))
        return false;
    return callSucceeded(m_moveAxis(clampInt(axis, 0, 2), clampInt(direction, 0, 1)), "MoveAxis");
}

QVariantMap NocaiDirectPrintClient::stopAxis(int axis)
{
    QMutexLocker locker(&m_mutex);
    QVariantMap out;
    int stopPos = 0;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_stopAxis), "StopAxis") &&
        callSucceeded(m_stopAxis(clampInt(axis, 0, 2), &stopPos), "StopAxis");
    out["ok"] = ok;
    out["position"] = stopPos;
    return out;
}

QVariantMap NocaiDirectPrintClient::saveAxisPos(int axis)
{
    QMutexLocker locker(&m_mutex);
    QVariantMap out;
    int savePos = 0;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_saveAxisPos), "SaveAxisPos") &&
        callSucceeded(m_saveAxisPos(clampInt(axis, 0, 2), &savePos), "SaveAxisPos");
    out["ok"] = ok;
    out["position"] = savePos;
    return out;
}

bool NocaiDirectPrintClient::setPrintHeight(double heightMm)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setPrintHeight), "SetPrintHeight"))
        return false;
    // X-33 firmware represents print height as fixed-point hundredths of a mm
    // in this WORD API (for example, 5.5 mm is sent as 550).
    const uint16_t rawHundredthsMm = static_cast<uint16_t>(
        std::clamp(qRound(heightMm * 100.0), 0, 15200));
    return callSucceeded(m_setPrintHeight(rawHundredthsMm), "SetPrintHeight");
}

QVariantMap NocaiDirectPrintClient::getPrintHeight()
{
    QMutexLocker locker(&m_mutex);
    QVariantMap out;
    uint16_t height = 0;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getPrintHeight), "GetPrintHeight") &&
        callSucceeded(m_getPrintHeight(&height), "GetPrintHeight");
    out["ok"] = ok;
    out["heightMm"] = static_cast<double>(height) / 100.0;
    return out;
}

QVariantMap NocaiDirectPrintClient::getJobSettings()
{
    QMutexLocker locker(&m_mutex);
    JobSettings settings;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getJobSettings), "GetJobSettings") &&
        callSucceeded(m_getJobSettings(&settings, sizeof(JobSettings)), "GetJobSettings");
    QVariantMap out = jobSettingsToMap(settings);
    out["ok"] = ok;
    return out;
}

bool NocaiDirectPrintClient::setJobSettingsFromMap(const QVariantMap& settings)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setJobSettings), "SetJobSettings"))
        return false;
    JobSettings jobSettings = jobSettingsFromMap(settings);
    return callSucceeded(m_setJobSettings(&jobSettings, sizeof(JobSettings)), "SetJobSettings");
}

bool NocaiDirectPrintClient::exportConfigFile(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_exportConfigFile), "ExportConfigFile"))
        return false;
    const QString localPath = path.startsWith("file:", Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
    QByteArray bytes = QFile::encodeName(localPath);
    return callSucceeded(m_exportConfigFile(bytes.data()), "ExportConfigFile");
}

bool NocaiDirectPrintClient::importConfigFile(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_importConfigFile), "ImportConfigFile"))
        return false;
    const QString localPath = path.startsWith("file:", Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
    QByteArray bytes = QFile::encodeName(localPath);
    return callSucceeded(m_importConfigFile(bytes.data()), "ImportConfigFile");
}

QVariantMap NocaiDirectPrintClient::getAlignmentValues()
{
    QMutexLocker locker(&m_mutex);
    AlignmentValues values;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getAlignmentValues), "GetAlignmentValues") &&
        callSucceeded(m_getAlignmentValues(&values, sizeof(AlignmentValues)), "GetAlignmentValues");
    QVariantMap out = alignmentValuesToMap(values);
    out["ok"] = ok;
    return out;
}

bool NocaiDirectPrintClient::setAlignmentValues(const QVariantMap& settings, int type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setAlignmentValues), "SetAlignmentValues"))
        return false;
    AlignmentValues values = alignmentValuesFromMap(settings);
    return callSucceeded(m_setAlignmentValues(&values, clampInt(type, 0, 5), sizeof(AlignmentValues)), "SetAlignmentValues");
}

bool NocaiDirectPrintClient::printAlignmentPattern(int type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_printAlignmentPattern), "PrintAlignmentPattern"))
        return false;
    bool ok = false;
    withSdkWorkingDirectory([&]() {
        ok = callSucceeded(m_printAlignmentPattern(clampInt(type, 0, 22)),
                           "PrintAlignmentPattern");
        return ok;
    });
    return ok;
}

bool NocaiDirectPrintClient::printNozzleCheck()
{
    // eAlignmentPatternTypes::E_NOZZLE_CHECK is the first documented value.
    return printAlignmentPattern(0);
}

QVariantMap NocaiDirectPrintClient::getPrinterStatus()
{
    QMutexLocker locker(&m_mutex);
    PrinterStatus status;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getPrinterStatus), "GetPrinterStatus") &&
        callSucceeded(m_getPrinterStatus(&status, sizeof(PrinterStatus)), "GetPrinterStatus");
    QVariantMap out;
    out["ok"] = ok;
    out["printStatus"] = static_cast<int>(status.PrintStatus);
    out["cleanStatus"] = static_cast<int>(status.CleanStatus);
    return out;
}

QVariantMap NocaiDirectPrintClient::getPrinterInfo()
{
    QMutexLocker locker(&m_mutex);
    PrinterInfo info;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getPrinterInfo), "GetPrinterInfo") &&
        callSucceeded(m_getPrinterInfo(&info, sizeof(PrinterInfo)), "GetPrinterInfo");
    QVariantMap out;
    out["ok"] = ok;
    out["mainboardFpga"] = QString("%1%2%3").arg(info.Mainboard_fpgaVer).arg(QChar(info.Mainboard_fpgaExVer)).arg(QChar(info.Mainboard_fpgaSubVer));
    out["carboardFpga"] = QString("%1%2%3").arg(info.Carboard_fpgaVer).arg(QChar(info.Carboard_fpgaExVer)).arg(QChar(info.Carboard_fpgaSubVer));
    out["mainboardCpu"] = QString("%1%2%3").arg(info.Mainboard_cpuVer).arg(QChar(info.Mainboard_cpuExVer)).arg(info.Mainboard_cpuSubVer);
    out["carboardCpu"] = QString("%1%2%3").arg(info.Carboard_cpuVer).arg(QChar(info.Carboard_cpuExVer)).arg(info.Carboard_cpuSubVer);
    out["carParaCrc"] = QString::number(info.CarParaCRC, 16).toUpper();
    out["uiCrc"] = QString::number(info.UI_CRC, 16).toUpper();
    out["uiCrc2"] = QString::number(info.UI_CRC2, 16).toUpper();
    out["id1"] = QString::number(info.ID1, 16).toUpper();
    out["id2"] = QString::number(info.ID2, 16).toUpper();
    return out;
}

bool NocaiDirectPrintClient::setPrintXYValue(int xMm, int yMm)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setPrintXYValue), "SetPrintXYValue"))
        return false;
    return callSucceeded(m_setPrintXYValue(millimetersToPrintXYUnits(xMm),
                                           millimetersToPrintXYUnits(yMm)),
                         "SetPrintXYValue");
}

QVariantMap NocaiDirectPrintClient::getPrintXYValue()
{
    QMutexLocker locker(&m_mutex);
    QVariantMap out;
    uint32_t x = 0, y = 0;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getPrintXYValue), "GetPrintXYValue") &&
        callSucceeded(m_getPrintXYValue(&x, &y), "GetPrintXYValue");
    out["ok"] = ok;
    out["xMm"] = static_cast<double>(x) / 100.0;
    out["yMm"] = static_cast<double>(y) / 100.0;
    out["xRawHundredthsMm"] = static_cast<qulonglong>(x);
    out["yRawHundredthsMm"] = static_cast<qulonglong>(y);
    return out;
}

QVariantMap NocaiDirectPrintClient::getUVParamValues()
{
    QMutexLocker locker(&m_mutex);
    UVParamValues values;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getUVParamValues), "GetUVParamValues") &&
        callSucceeded(m_getUVParamValues(&values, sizeof(UVParamValues)), "GetUVParamValues");
    QVariantMap out = uvParamValuesToMap(values);
    out["ok"] = ok;
    return out;
}

bool NocaiDirectPrintClient::setUVParamValues(const QVariantMap& settings, int type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setUVParamValues), "SetUVParamValues"))
        return false;
    UVParamValues values = uvParamValuesFromMap(settings);
    return callSucceeded(m_setUVParamValues(&values, clampInt(type, 0, 4), sizeof(UVParamValues)), "SetUVParamValues");
}

int NocaiDirectPrintClient::getSupportNewUVParamFunction()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_getSupportNewUVParamFunction), "GetSupportNewUVParamFunction"))
        return 0;
    return m_getSupportNewUVParamFunction();
}

bool NocaiDirectPrintClient::setNewUVParamFunction(int type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setNewUVParamFunction), "SetNewUVParamFunction"))
        return false;
    return callSucceeded(m_setNewUVParamFunction(clampInt(type, 0, 8)), "SetNewUVParamFunction");
}

QVariantMap NocaiDirectPrintClient::getNewUVParamValues()
{
    QMutexLocker locker(&m_mutex);
    NewUVParamValues values;
    const bool ok = ensureLoaded() &&
        requireFunction(reinterpret_cast<const void*>(m_getNewUVParamValues), "GetNewUVParamValues") &&
        callSucceeded(m_getNewUVParamValues(&values, sizeof(NewUVParamValues)), "GetNewUVParamValues");
    QVariantMap out = newUvParamValuesToMap(values);
    out["ok"] = ok;
    return out;
}

bool NocaiDirectPrintClient::setNewUVParamValues(const QVariantMap& settings, int type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded() || !requireFunction(reinterpret_cast<const void*>(m_setNewUVParamValues), "SetNewUVParamValues"))
        return false;
    NewUVParamValues values = newUvParamValuesFromMap(settings);
    return callSucceeded(m_setNewUVParamValues(&values, clampInt(type, 0, 6), sizeof(NewUVParamValues)), "SetNewUVParamValues");
}

QString NocaiDirectPrintClient::statusText()
{
    return isAvailable()
        ? QStringLiteral("Direct print SDK ready: %1").arg(m_resolvedSdkRoot)
        : m_lastError;
}

QString NocaiDirectPrintClient::controllerErrorDetails() const
{
    QStringList flags;
    const auto appendFlag = [&flags](const char* name, const int* value) {
        if (value && *value != 0)
            flags.append(QStringLiteral("%1=%2").arg(QString::fromLatin1(name)).arg(*value));
    };
    appendFlag("printer", m_sdkPrinterError);
    appendFlag("network", m_sdkNetError);
    appendFlag("uart", m_sdkUartError);
    appendFlag("slice", m_sdkErrorSlice);
    appendFlag("swath", m_sdkErrorSwath);

    QStringList nonzeroWords;
    QByteArray rawPacket;
    if (m_currentErrorInfo) {
        rawPacket = QByteArray(reinterpret_cast<const char*>(m_currentErrorInfo), 42);
        for (int offset = 0; offset + 1 < rawPacket.size(); offset += 2) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(rawPacket.constData() + offset);
            const uint16_t value = static_cast<uint16_t>(bytes[0]) |
                (static_cast<uint16_t>(bytes[1]) << 8);
            if (value != 0) {
                nonzeroWords.append(QStringLiteral("w%1=0x%2")
                                        .arg(offset / 2)
                                        .arg(value, 4, 16, QLatin1Char('0')));
            }
        }
    }

    if (m_sdkJobProperty) {
        QStringList values;
        for (int index = 0; index < 12; ++index)
            values.append(QString::number(m_sdkJobProperty[index]));
        qWarning().noquote()
            << "NocaiDirectPrintClient: SDK-normalized job property DWORDs:"
            << values.join(QLatin1Char(','));
    }
    if (!rawPacket.isEmpty()) {
        qWarning().noquote()
            << "NocaiDirectPrintClient: controller error packet (42 bytes):"
            << QString::fromLatin1(rawPacket.toHex(' '));
    }
    qWarning().noquote()
        << "NocaiDirectPrintClient: controller error fields:"
        << (flags.isEmpty() ? QStringLiteral("none") : flags.join(QStringLiteral(", ")))
        << "; nonzero packet words:"
        << (nonzeroWords.isEmpty() ? QStringLiteral("none")
                                   : nonzeroWords.join(QStringLiteral(", ")));

    QStringList details = flags;
    details.append(nonzeroWords);
    return details.isEmpty()
        ? QStringLiteral(" The SDK exposed no nonzero controller error fields.")
        : QStringLiteral(" SDK details: %1.").arg(details.join(QStringLiteral(", ")));
}

bool NocaiDirectPrintClient::submitPreparedJob(const DirectPrintRaster& raster,
                                               const DirectPrintSettings& settings)
{
    // The supplied x64 SDK performs swath formatting in native code and has
    // been observed faulting instead of returning an error. Keep that failure
    // outside the GUI process. Tests and the worker itself use the direct path.
    if (QCoreApplication::applicationName() == QStringLiteral("PrintFlow") &&
        !qEnvironmentVariableIsSet("PRINTFLOW_NOCAI_WORKER")) {
        return submitPreparedJobIsolated(raster, settings);
    }
    return printPackedJob(raster, settings);
}

bool NocaiDirectPrintClient::submitPreparedJobIsolated(
    const DirectPrintRaster& raster,
    const DirectPrintSettings& settings)
{
    QMutexLocker locker(&m_mutex);
    if (!raster.packedLines || raster.packedLines->empty() ||
        raster.channelOrder.empty() || raster.height <= 0) {
        setError("Direct print raster is empty.");
        return false;
    }

    const QString sdkRoot = m_resolvedSdkRoot.isEmpty() ? resolveSdkRoot() : m_resolvedSdkRoot;
    if (sdkRoot.isEmpty()) {
        setError("No architecture-compatible direct print SDK folder was found.");
        return false;
    }

    QTemporaryDir temporaryDir(QStringLiteral("/tmp/PrintFlow-NocaiWorker-XXXXXX"));
    if (!temporaryDir.isValid()) {
        setError("Could not create the isolated direct-print job folder.");
        return false;
    }

    const QString jobPath = temporaryDir.filePath(QStringLiteral("job.pfnw"));
    QFile file(jobPath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(QStringLiteral("Could not create isolated direct-print job: %1")
                     .arg(file.errorString()));
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << kWorkerJobMagic << kWorkerJobVersion << sdkRoot;
    out << qint32(raster.width) << qint32(raster.height)
        << qint32(raster.xdpi) << qint32(raster.ydpi)
        << qint32(raster.bytesPerLine);
    out << quint8(raster.format);
    for (const uint32_t word : raster.canonicalHeader)
        out << quint32(word);
    out << qint32(raster.channelOrder.size());
    for (const int channel : raster.channelOrder)
        out << qint32(channel);

    out << qint32(settings.printerIndex)
        << qint32(settings.printDirection) << qint32(settings.printSpeed)
        << qint32(settings.wcSequence) << qint32(settings.eclosionGrade)
        << qint32(settings.headSelect) << qint32(settings.whiteInkPercent)
        << qint32(settings.whiteInkPassCount) << qint32(settings.varnishInkPercent)
        << qint32(settings.varnishInkPassCount) << qint32(settings.headVoltage)
        << qint32(settings.disableUv0) << qint32(settings.disableUv1)
        << qint32(settings.disableUv2) << qint32(settings.disableUv3)
        << qint32(settings.disableUv4) << qint32(settings.disableUv5)
        << qint32(settings.carReset) << qint32(settings.stripBlank)
        << qint32(settings.blankDistance) << settings.mediaHeightMm
        << qint32(settings.printOffsetXmm) << qint32(settings.printOffsetYmm)
        << qint32(settings.pass) << qint32(settings.vsdMode);

    for (int row = 0; row < raster.height; ++row) {
        for (const int channel : raster.channelOrder) {
            if (channel < 0 || channel >= static_cast<int>(raster.packedLines->size()) ||
                row >= static_cast<int>((*raster.packedLines)[channel].size())) {
                setError("Direct print raster channel/row index is invalid.");
                return false;
            }
            const auto& line = (*raster.packedLines)[channel][row];
            out << QByteArray(reinterpret_cast<const char*>(line.data()),
                              static_cast<qsizetype>(line.size()));
        }
    }
    file.close();
    if (out.status() != QDataStream::Ok) {
        setError("Could not serialize the isolated direct-print job.");
        return false;
    }

    QProcess worker;
    worker.setProgram(QCoreApplication::applicationFilePath());
    worker.setArguments({QStringLiteral("--nocai-print-worker"), jobPath});
    worker.setWorkingDirectory(sdkRoot);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PRINTFLOW_NOCAI_WORKER"), QStringLiteral("1"));
    worker.setProcessEnvironment(environment);
    worker.setProcessChannelMode(QProcess::ForwardedChannels);
    worker.start();
    if (!worker.waitForStarted(10000)) {
        setError(QStringLiteral("Could not start isolated direct-print worker: %1")
                     .arg(worker.errorString()));
        return false;
    }
    worker.waitForFinished(-1);

    if (worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0) {
        m_connected = false;
        setError(worker.exitStatus() == QProcess::CrashExit
                     ? QStringLiteral("The vendor print SDK crashed in the isolated worker; PrintFlow remained open.")
                     : QStringLiteral("The isolated direct-print worker failed with exit code %1.")
                           .arg(worker.exitCode()));
        return false;
    }

    qDebug() << "NocaiDirectPrintClient: isolated raster upload completed.";
    return true;
}

int NocaiDirectPrintClient::runSerializedPrintWorker(const QString& jobPath)
{
    QFile file(jobPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Nocai print worker: cannot open job:" << file.errorString();
        return 2;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 version = 0;
    QString sdkRoot;
    qint32 width = 0, height = 0, xdpi = 0, ydpi = 0, bytesPerLine = 0;
    quint8 rasterFormat = 0;
    std::array<uint32_t, 12> canonicalHeader{};
    qint32 channelCount = 0;
    in >> magic >> version >> sdkRoot;
    in >> width >> height >> xdpi >> ydpi >> bytesPerLine >> rasterFormat;
    for (uint32_t& word : canonicalHeader) {
        quint32 serializedWord = 0;
        in >> serializedWord;
        word = serializedWord;
    }
    in >> channelCount;
    if (magic != kWorkerJobMagic || version != kWorkerJobVersion ||
        width <= 0 || height <= 0 || bytesPerLine <= 0 ||
        rasterFormat > quint8(DirectPrintRasterFormat::NocaiX33Standard) ||
        channelCount <= 0 || channelCount > 16) {
        qCritical() << "Nocai print worker: invalid serialized job header.";
        return 2;
    }

    std::vector<int> channelOrder;
    channelOrder.reserve(channelCount);
    int maximumChannel = -1;
    for (qint32 index = 0; index < channelCount; ++index) {
        qint32 channel = -1;
        in >> channel;
        if (channel < 0 || channel > 15)
            return 2;
        channelOrder.push_back(channel);
        maximumChannel = std::max(maximumChannel, int(channel));
    }

    DirectPrintSettings settings;
    qint32 values[20] = {};
    for (qint32& value : values)
        in >> value;
    settings.printerIndex = values[0];
    settings.printDirection = values[1];
    settings.printSpeed = values[2];
    settings.wcSequence = values[3];
    settings.eclosionGrade = values[4];
    settings.headSelect = values[5];
    settings.whiteInkPercent = values[6];
    settings.whiteInkPassCount = values[7];
    settings.varnishInkPercent = values[8];
    settings.varnishInkPassCount = values[9];
    settings.headVoltage = values[10];
    settings.disableUv0 = values[11];
    settings.disableUv1 = values[12];
    settings.disableUv2 = values[13];
    settings.disableUv3 = values[14];
    settings.disableUv4 = values[15];
    settings.disableUv5 = values[16];
    settings.carReset = values[17];
    settings.stripBlank = values[18];
    settings.blankDistance = values[19];
    in >> settings.mediaHeightMm;
    qint32 printOffsetXmm = 0;
    qint32 printOffsetYmm = 0;
    in >> printOffsetXmm >> printOffsetYmm;
    settings.printOffsetXmm = printOffsetXmm;
    settings.printOffsetYmm = printOffsetYmm;
    qint32 pass = 0;
    qint32 vsdMode = 0;
    in >> pass >> vsdMode;
    settings.pass = pass;
    settings.vsdMode = vsdMode;

    std::vector<std::vector<std::vector<uint8_t>>> packedLines(
        static_cast<size_t>(maximumChannel + 1),
        std::vector<std::vector<uint8_t>>(static_cast<size_t>(height)));
    for (int row = 0; row < height; ++row) {
        for (const int channel : channelOrder) {
            QByteArray line;
            in >> line;
            if (line.size() != bytesPerLine) {
                qCritical() << "Nocai print worker: invalid plane-line size at row"
                            << row + 1;
                return 2;
            }
            packedLines[channel][row] = std::vector<uint8_t>(line.begin(), line.end());
        }
    }
    if (in.status() != QDataStream::Ok) {
        qCritical() << "Nocai print worker: truncated serialized job.";
        return 2;
    }

    DirectPrintRaster raster;
    raster.packedLines = &packedLines;
    raster.channelOrder = channelOrder;
    raster.width = width;
    raster.height = height;
    raster.xdpi = xdpi;
    raster.ydpi = ydpi;
    raster.bytesPerLine = bytesPerLine;
    raster.format = static_cast<DirectPrintRasterFormat>(rasterFormat);
    raster.canonicalHeader = canonicalHeader;

    NocaiDirectPrintClient client;
    client.setSdkRootPath(sdkRoot);
    client.setAutoDiscoverSdk(false);
    if (!client.refreshPrinters()) {
        qCritical() << "Nocai print worker:" << client.lastError();
        return 3;
    }
    if (!client.printPackedJob(raster, settings)) {
        qCritical() << "Nocai print worker:" << client.lastError();
        return 4;
    }
    return 0;
}

bool NocaiDirectPrintClient::printPackedJob(const DirectPrintRaster& raster,
                                            const DirectPrintSettings& settings)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureLoaded())
        return false;

    if (!raster.packedLines || raster.packedLines->empty() || raster.channelOrder.empty()) {
        setError("Direct print raster is empty.");
        return false;
    }

    const int bytesPerLine = raster.bytesPerLine > 0
        ? raster.bytesPerLine
        : static_cast<int>((*raster.packedLines)[0][0].size());

    PrintJobProperty prop;
    if (raster.format == DirectPrintRasterFormat::NocaiX33Standard) {
        const bool hasCanonicalCmykOrder =
            raster.channelOrder == std::vector<int>({2, 1, 0, 3});
        const bool hasCanonicalWhiteOrder =
            raster.channelOrder == std::vector<int>({2, 1, 0, 3, 4, 4});
        const auto expectedHeader = NocaiPrnWriter::makeStandardX33Header(
            raster.width,
            raster.height,
            raster.xdpi,
            raster.ydpi,
            bytesPerLine,
            static_cast<int>(raster.channelOrder.size()));
        if (raster.canonicalHeader != expectedHeader ||
            (!hasCanonicalCmykOrder && !hasCanonicalWhiteOrder)) {
            setError("The X-33 direct-print raster does not match the canonical PRN contract.");
            return false;
        }
        static_assert(sizeof(prop) == sizeof(raster.canonicalHeader),
                      "X-33 SDK header must remain exactly 48 bytes.");
        std::memcpy(&prop, raster.canonicalHeader.data(), sizeof(prop));
    } else {
        prop.Signature = kPrintSignature;
        prop.XDPI = static_cast<uint32_t>(std::max(1, raster.xdpi));
        prop.YDPI = static_cast<uint32_t>(std::max(1, raster.ydpi));
        prop.BytesPerLine = static_cast<uint32_t>(std::max(0, bytesPerLine));
        prop.Height = static_cast<uint32_t>(std::max(0, raster.height));
        prop.Width = static_cast<uint32_t>(std::max(0, raster.width));
        prop.PaperWidth = 0;
        prop.Colors = static_cast<uint32_t>(raster.channelOrder.size());
        prop.Bits = 1;
        prop.Pass = static_cast<uint32_t>(std::max(0, settings.pass));
        prop.VsdMode = static_cast<uint32_t>(std::max(0, settings.vsdMode));
    }

    JobSettings jobSettings = makeJobSettings(settings);
    if (raster.format == DirectPrintRasterFormat::NocaiX33Standard) {
        // The legacy x64 PROII SDK corrupts its reverse-direction swath length
        // for this controller in bidirectional mode (PrintDirection=0).  The
        // resulting unsigned underflow makes API_PrintALine write beyond the
        // SDK's allocation near the sixth swath.  Both supported X-33 header
        // interpretations complete normally in left-to-right mode, so keep
        // this workaround local to the legacy X-33 path.
        jobSettings.PrintDirection = 1;
    }
    qDebug().nospace()
        << "NocaiDirectPrintClient: X-33 job header: "
        << prop.Width << 'x' << prop.Height
        << ", dpi=" << prop.XDPI << 'x' << prop.YDPI
        << ", bytesPerLine=" << prop.BytesPerLine
        << ", colors=" << prop.Colors
        << ", bits=" << prop.Bits
        << ", pass=" << prop.Pass
        << ", headSelect=" << jobSettings.HeadSelect
        << ", direction=" << jobSettings.PrintDirection;
    bool ok = false;

    withSdkWorkingDirectory([&]() {
        if (settings.printerIndex < 0 && m_selectedPrinterIndex < 0) {
            if (m_printers.size() != 1) {
                setError("No SDK printer is selected for direct printing.");
                return false;
            }
            const int onlyIndex = m_printers.first().toMap()
                                      .value(QStringLiteral("index"), 0).toInt();
            if (!callSucceeded(m_choosePrinter(onlyIndex), "ChoosePrinter"))
                return false;
            m_selectedPrinterIndex = onlyIndex;
            m_connected = false;
        }

        if (settings.printerIndex >= 0 && settings.printerIndex != m_selectedPrinterIndex) {
            if (!callSucceeded(m_choosePrinter(settings.printerIndex), "ChoosePrinter"))
                return false;
            m_selectedPrinterIndex = settings.printerIndex;
            m_connected = false;
        }

        if (!m_connected && m_connectPrinter) {
            if (!callSucceeded(m_connectPrinter(), "ConnectPrinter"))
                return false;
            m_connected = true;
        }

        if (settings.mediaHeightMm >= 0.0) {
            if (!requireFunction(reinterpret_cast<const void*>(m_setPrintHeight),
                                 "SetPrintHeight") ||
                !requireFunction(reinterpret_cast<const void*>(m_getPrintHeight),
                                 "GetPrintHeight"))
                return false;
            const uint16_t rawHundredthsMm = static_cast<uint16_t>(
                std::clamp(qRound(settings.mediaHeightMm * 100.0), 0, 15200));

            uint16_t currentHeight = 0;
            bool heightReached = m_getPrintHeight(&currentHeight) == kSySucceeded
                && currentHeight == rawHundredthsMm;
            if (!heightReached) {
                if (!callSucceeded(m_setPrintHeight(rawHundredthsMm), "SetPrintHeight"))
                    return false;

                // SetPrintHeight starts an asynchronous Z-axis move. Starting
                // the job while that move is active makes StartPrint fail and
                // leaves the next height command reporting the controller busy.
                for (int attempt = 0; attempt < 240 && !heightReached; ++attempt) {
                    QThread::msleep(250);
                    currentHeight = 0;
                    heightReached = m_getPrintHeight(&currentHeight) == kSySucceeded
                        && currentHeight == rawHundredthsMm;
                }
                if (!heightReached) {
                    setError(QStringLiteral(
                        "Timed out waiting for print height %1 mm; controller reported %2 mm.")
                                 .arg(settings.mediaHeightMm, 0, 'f', 1)
                                 .arg(static_cast<double>(currentHeight) / 100.0, 0, 'f', 2));
                    return false;
                }
            }
            qDebug() << "NocaiDirectPrintClient: per-job media height ready at"
                     << settings.mediaHeightMm << "mm.";
        }

        if (!callSucceeded(m_setJobSettings(&jobSettings, sizeof(JobSettings)), "SetJobSettings"))
            return false;

        if (!callSucceeded(m_initPrinter(), "InitPrinter"))
            return false;

        if (raster.format == DirectPrintRasterFormat::NocaiX33Standard) {
            // The working X-33 sequence is InitPrinter -> SetPrintXYValue ->
            // StartPrint. A post-StartPrint setter interrupts the active data
            // connection, while a pre-Init setter can be superseded. The SDK
            // consumes uint32 hundredths of a millimeter without scaling.
            if (!requireFunction(reinterpret_cast<const void*>(m_setPrintXYValue),
                                 "SetPrintXYValue") ||
                !requireFunction(reinterpret_cast<const void*>(m_getPrintXYValue),
                                 "GetPrintXYValue"))
                return false;

            const uint32_t expectedX = millimetersToPrintXYUnits(
                settings.printOffsetXmm);
            const uint32_t expectedY = millimetersToPrintXYUnits(
                settings.printOffsetYmm);
            if (!callSucceeded(m_setPrintXYValue(expectedX, expectedY),
                               "SetPrintXYValue"))
                return false;

            uint32_t actualX = 0;
            uint32_t actualY = 0;
            if (!callSucceeded(m_getPrintXYValue(&actualX, &actualY),
                               "GetPrintXYValue"))
                return false;
            if (actualX != expectedX || actualY != expectedY) {
                setError(QStringLiteral(
                    "Printer rejected the per-job origin %1 x %2 mm (raw %3 x %4); it reported raw %5 x %6.")
                             .arg(std::max(0, settings.printOffsetXmm))
                             .arg(std::max(0, settings.printOffsetYmm))
                             .arg(expectedX)
                             .arg(expectedY)
                             .arg(actualX)
                             .arg(actualY));
                return false;
            }
            qDebug() << "NocaiDirectPrintClient: active print origin ready at"
                     << std::max(0, settings.printOffsetXmm) << "x"
                     << std::max(0, settings.printOffsetYmm) << "mm (raw"
                     << expectedX << "x" << expectedY << ").";
        }

        if (!callSucceeded(m_startPrint(&prop), "StartPrint")) {
            m_connected = false;
            m_closePrint();
            return false;
        }
        if (m_sdkJobProperty) {
            QStringList normalized;
            for (int index = 0; index < 12; ++index)
                normalized.append(QString::number(m_sdkJobProperty[index]));
            qDebug().noquote()
                << "NocaiDirectPrintClient: SDK-normalized header DWORDs:"
                << normalized.join(QLatin1Char(','));
        }
        const uint32_t totalPlaneLines = prop.Height * prop.Colors;
        qDebug() << "NocaiDirectPrintClient: StartPrint accepted; sending"
                 << totalPlaneLines << "single-color plane lines of"
                 << prop.BytesPerLine << "bytes in"
                 << (prop.Colors == 6 ? "YMCKWW" : "YMCK")
                 << "row order.";

        for (int row = 0; row < raster.height; ++row) {
            for (size_t plane = 0; plane < raster.channelOrder.size(); ++plane) {
                const int ch = raster.channelOrder[plane];
                if (ch < 0 || ch >= static_cast<int>(raster.packedLines->size()) ||
                    row < 0 || row >= static_cast<int>((*raster.packedLines)[ch].size())) {
                    setError("Direct print raster channel/row index is invalid.");
                    m_abortPrint();
                    m_closePrint();
                    return false;
                }

                const std::vector<uint8_t>& line = (*raster.packedLines)[ch][row];
                if (line.size() != prop.BytesPerLine) {
                    setError(QStringLiteral(
                        "Direct print plane-line size mismatch: got %1, expected %2.")
                                 .arg(line.size())
                                 .arg(prop.BytesPerLine));
                    m_abortPrint();
                    m_closePrint();
                    return false;
                }

                // The X-33 old interface accepts one color plane per call.
                // Its internal line sequence groups Colors calls into one
                // raster row, so the required order is Y, M, C, K for every
                // image row (Height * Colors calls in total). X-33 white jobs
                // append the same screened W line twice: YMCKWW.
                const int bytesWritten = m_printALine(
                    reinterpret_cast<char*>(const_cast<uint8_t*>(line.data())),
                    prop.BytesPerLine);
                if (bytesWritten != static_cast<int>(prop.BytesPerLine)) {
                    setError(QStringLiteral(
                        "PrintALine failed at row %1 plane %2: SDK consumed %3 of %4 bytes.")
                                 .arg(row + 1)
                                 .arg(plane + 1)
                                 .arg(bytesWritten)
                                 .arg(prop.BytesPerLine));
                    m_abortPrint();
                    m_closePrint();
                    return false;
                }
            }

            if (row == 0 || (row + 1) % 512 == 0 || row + 1 == raster.height)
                qDebug() << "NocaiDirectPrintClient: streamed raster row"
                         << (row + 1) << "of" << raster.height;

        }

        // Nocai's X-33 process is explicitly StartPrint -> plane-line loop ->
        // EndPrint -> ClosePrint. ClosePrint corresponds to StartPrint and is
        // part of committing the transmitted job; physical printing continues
        // asynchronously after this API session has been closed.
        if (!callSucceeded(m_endPrint(), "EndPrint")) {
            m_abortPrint();
            m_closePrint();
            return false;
        }

        const bool closed = callSucceeded(m_closePrint(), "ClosePrint");
        if (closed)
            qDebug() << "NocaiDirectPrintClient: raster upload finalized and print session closed.";
        ok = closed;
        return ok;
    });

    emit statusChanged();
    return ok;
}

bool NocaiDirectPrintClient::ensureLoaded()
{
    if (m_library.isLoaded())
        return m_symbolsResolved;

    const QString root = resolveSdkRoot();
    if (root.isEmpty()) {
        setError("Direct print SDK folder was not found.");
        return false;
    }

    const QString libraryPath = QDir(root).filePath("libSYPrintAPIforPROII.so");
    if (!QFileInfo::exists(libraryPath)) {
        setError(QString("Direct print SDK library is missing: %1").arg(libraryPath));
        return false;
    }

    m_library.setFileName(libraryPath);
    {
        ScopedCurrentDir sdkDirectory(root);
        if (!sdkDirectory.changed) {
            setError(QString("Failed to enter direct print SDK folder: %1").arg(root));
            return false;
        }
        ScopedSdkStdoutCapture capture;
        if (!m_library.load()) {
            setError(QString("Failed to load direct print SDK %1: %2")
                         .arg(libraryPath, m_library.errorString()));
            return false;
        }
    }

    m_resolvedSdkRoot = root;
    m_symbolsResolved = resolveSymbols();
    if (!m_symbolsResolved) {
        NocaiDirectPrintCompatibility::uninstall(this);
        m_library.unload();
        m_resolvedSdkRoot.clear();
    }
    return m_symbolsResolved;
}

bool NocaiDirectPrintClient::resolveSymbols()
{
    bool usedCompatibilitySymbols = false;
    auto resolveAddress = [&](const char* name, const char* compatibilityName) -> QFunctionPointer {
        QFunctionPointer address = m_library.resolve(name);
        if (!address && compatibilityName) {
            address = m_library.resolve(compatibilityName);
            if (address) {
                usedCompatibilitySymbols = true;
            }
        }
        return address;
    };

    auto resolve = [&](auto& fn, const char* name, const char* compatibilityName) -> bool {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
            resolveAddress(name, compatibilityName));
        if (!fn) {
            const QString compatibilityText = compatibilityName
                ? QStringLiteral(" (compatibility alias %1 also missing)")
                      .arg(QString::fromLatin1(compatibilityName))
                : QString();
            setError(QStringLiteral("Failed to resolve direct print SDK function: %1%2")
                         .arg(QString::fromLatin1(name), compatibilityText));
            return false;
        }
        return true;
    };

    auto resolveOptional = [&](auto& fn, const char* name, const char* compatibilityName) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
            resolveAddress(name, compatibilityName));
        if (!fn)
            qWarning() << "NocaiDirectPrintClient: optional SDK function unavailable:" << name;
    };

    // These are data exports rather than callable API functions. They are
    // diagnostic-only and deliberately remain optional for test/other SDKs.
    m_currentErrorInfo = sdkDataAddress<unsigned char>(m_library.resolve("CurErrorInfo"));
    m_sdkJobProperty = sdkDataAddress<uint32_t>(m_library.resolve("gJobProp"));
    m_sdkPrinterError = sdkDataAddress<int>(m_library.resolve("gfPrinterError"));
    m_sdkNetError = sdkDataAddress<int>(m_library.resolve("net_error"));
    m_sdkUartError = sdkDataAddress<int>(m_library.resolve("uart_error"));
    m_sdkErrorSlice = sdkDataAddress<int>(m_library.resolve("error_slice"));
    m_sdkErrorSwath = sdkDataAddress<int>(m_library.resolve("error_swath"));

    resolveOptional(m_getJobSettings, "GetJobSettings", "_Z18API_GetJobSettingsP13stJobSettingsi");
    resolveOptional(m_connectPrinter, "ConnectPrinter", "_Z18API_ConnectPrinterv");
    resolveOptional(m_wipePrintHead, "WipePrintHead", "_Z17API_WipePrintHeadi");
    resolveOptional(m_startCleanOperation, "StartCleanOperation", "_Z17API_AutoCleanHeadi");
    resolveOptional(m_startPump, "StartPump", "_Z16API_StartPumpInki");
    resolveOptional(m_stopPumpOperation, "StopPumpOperation", "_Z15API_StopPumpInkv");
    resolveOptional(m_spitPrintHead, "SpitPrintHead", "_Z16API_StartSpitInki");
    resolveOptional(m_stopSpitOperation, "StopSpitOperation", "_Z15API_StopSpitInkv");
    resolveOptional(m_capPrintHead, "CapPrintHead", "_Z16API_CapPrintHeadv");
    resolveOptional(m_moveAxis, "MoveAxis", "_Z12API_MoveAxisii");
    resolveOptional(m_stopAxis, "StopAxis", "_Z12API_StopAxisiPi");
    resolveOptional(m_saveAxisPos, "SaveAxisPos", "_Z15API_SaveAxisPosiPi");
    resolveOptional(m_setPrintHeight, "SetPrintHeight", "_Z18API_SetPrintHeightt");
    resolveOptional(m_getPrintHeight, "GetPrintHeight", "_Z18API_GetPrintHeightPt");
    resolveOptional(m_setAlignmentValues, "SetAlignmentValues", "_Z22API_SetAlignmentValuesP17stAlignmentValues20eAlignmentValueTypesi");
    resolveOptional(m_getAlignmentValues, "GetAlignmentValues", "_Z22API_GetAlignmentValuesP17stAlignmentValuesi");
    resolveOptional(m_exportConfigFile, "ExportConfigFile", "_Z20API_ExportConfigFilePc");
    resolveOptional(m_importConfigFile, "ImportConfigFile", "_Z20API_ImportConfigFilePc");
    resolveOptional(m_printAlignmentPattern, "PrintAlignmentPattern", "_Z25API_PrintAlignmentPattern22eAlignmentPatternTypes");
    resolveOptional(m_getPrinterStatus, "GetPrinterStatus", "_Z20API_GetPrinterStatusP15stPrinterStatusi");
    resolveOptional(m_getPrinterInfo, "GetPrinterInfo", "_Z18API_GetPrinterInfoP13stPrinterInfoi");
    resolveOptional(m_setPrintXYValue, "SetPrintXYValue", "_Z19API_SetPrintXYValuejj");
    resolveOptional(m_getPrintXYValue, "GetPrintXYValue", "_Z19API_GetPrintXYValuePjS_");
    resolveOptional(m_setUVParamValues, "SetUVParamValues", "_Z20API_SetUVParamValuesP15stUVParamValues18eUVParamValueTypesi");
    resolveOptional(m_getUVParamValues, "GetUVParamValues", "_Z20API_GetUVParamValuesP15stUVParamValuesi");
    resolveOptional(m_getSupportNewUVParamFunction, "GetSupportNewUVParamFunction", "_Z24API_GetSupportNewUVParamv");
    resolveOptional(m_setNewUVParamFunction, "SetNewUVParamFunction", "_Z25API_SetNewUVParamFunction24eNewUVParamFunctionTypes");
    resolveOptional(m_setNewUVParamValues, "SetNewUVParamValues", "_Z23API_SetNewUVParamValuesP18stNewUVParamValues21eNewUVParamValueTypesi");
    resolveOptional(m_getNewUVParamValues, "GetNewUVParamValues", "_Z23API_GetNewUVParamValuesP18stNewUVParamValuesi");

    const bool requiredSymbolsResolved =
        resolve(m_searchPrinter, "SearchPrinter", "_Z17API_SearchPrinterP15PrinterInfoListi") &&
        resolve(m_choosePrinter, "ChoosePrinter", "_Z17API_SelectPrinteri") &&
        resolve(m_continuePrint, "ContinuePrint", "_Z17API_ContinuePrintv") &&
        resolve(m_initPrinter, "InitPrinter", "_Z15API_InitPrinterv") &&
        resolve(m_startPrint, "StartPrint", "_Z14API_StartPrintP19tagPrintJobProperty") &&
        resolve(m_printALine, "PrintALine", "_Z14API_PrintALinePcj") &&
        resolve(m_abortPrint, "AbortPrint", "_Z14API_AbortPrintv") &&
        resolve(m_pausePrint, "PausePrint", "_Z14API_PausePrintv") &&
        resolve(m_endPrint, "EndPrint", "_Z12API_EndPrintv") &&
        resolve(m_closePrint, "ClosePrint", "_Z14API_ClosePrintv") &&
        resolve(m_setJobSettings, "SetJobSettings", "_Z18API_SetJobSettingsP13stJobSettingsi");

    if (requiredSymbolsResolved && usedCompatibilitySymbols) {
        const NocaiDirectPrintCompatibility::Callbacks callbacks{
            reinterpret_cast<NocaiDirectPrintCompatibility::StartPrintFn>(m_startPrint),
            reinterpret_cast<NocaiDirectPrintCompatibility::PrintALineFn>(m_printALine),
            m_endPrint,
            m_closePrint
        };
        if (!NocaiDirectPrintCompatibility::install(this, callbacks)) {
            setError("Failed to install the Linux x86-64 direct-print compatibility callbacks.");
            return false;
        }
    }
    return requiredSymbolsResolved;
}

QString NocaiDirectPrintClient::resolveSdkRoot() const
{
    for (const QString& candidate : sdkRootCandidates()) {
        if (candidate.trimmed().isEmpty())
            continue;

        const QDir dir(candidate);
        if (dir.exists("libSYPrintAPIforPROII.so"))
            return dir.absolutePath();
    }

    return QString();
}

QStringList NocaiDirectPrintClient::sdkRootCandidates() const
{
    QStringList roots;
    roots << m_sdkRootPath;
    if (!m_autoDiscoverSdk)
        return roots;

    roots << qEnvironmentVariable("DIRECT_PRINT_SDK_ROOT");
    roots << QCoreApplication::applicationDirPath();
#if defined(Q_OS_ANDROID)
    roots << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("lib");
#endif
    roots << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("DemoForARM64Linux-260612/Demo260612");
    roots << QDir::current().absoluteFilePath("DemoForARM64Linux-260612/Demo260612");
    return roots;
}

void NocaiDirectPrintClient::setError(const QString& message)
{
    if (m_lastError == message)
        return;

    m_lastError = message;
    qWarning() << "NocaiDirectPrintClient:" << message;
}

bool NocaiDirectPrintClient::callSucceeded(int result, const QString& functionName)
{
    if (result == kSySucceeded) {
        m_lastError.clear();
        return true;
    }

    setError(QString("%1 failed with SDK result 0x%2.")
                 .arg(functionName)
                 .arg(result, 0, 16));
    return false;
}

bool NocaiDirectPrintClient::requireFunction(const void* fn, const QString& functionName)
{
    if (fn)
        return true;

    setError(QString("Direct print SDK function is unavailable: %1").arg(functionName));
    return false;
}

QVariantMap NocaiDirectPrintClient::jobSettingsToMap(const JobSettings& settings) const
{
    QVariantMap out;
    out["printDirection"] = static_cast<int>(settings.PrintDirection);
    out["printSpeed"] = static_cast<int>(settings.PrintSpeed);
    out["wcSequence"] = static_cast<int>(settings.WCSequence);
    out["eclosionGrade"] = static_cast<int>(settings.EclosionGrade);
    out["headSelect"] = static_cast<int>(settings.HeadSelect);
    out["whiteInkPercent"] = static_cast<int>(settings.WInkPercent);
    out["whiteInkPassCount"] = static_cast<int>(settings.WInkPassCount);
    out["varnishInkPercent"] = static_cast<int>(settings.VInkPercent);
    out["varnishInkPassCount"] = static_cast<int>(settings.VInkPassCount);
    out["headVoltage"] = static_cast<int>(settings.HeadVoltage);
    for (int i = 0; i < 6; ++i)
        out[QString("disableUv%1").arg(i)] = static_cast<int>(settings.DisableUVLights[i]);
    out["carReset"] = static_cast<int>(settings.CarReset);
    out["stripBlank"] = static_cast<int>(settings.stripBlank);
    out["blankDistance"] = static_cast<int>(settings.blankDistance);
    return out;
}

NocaiDirectPrintClient::JobSettings
NocaiDirectPrintClient::jobSettingsFromMap(const QVariantMap& settings) const
{
    DirectPrintSettings normalized;
    auto value = [&](const char* key, int def) {
        return settings.value(QString::fromUtf8(key), def).toInt();
    };

    normalized.printDirection = value("printDirection", 0);
    normalized.printSpeed = value("printSpeed", 1);
    normalized.wcSequence = value("wcSequence", 0);
    normalized.eclosionGrade = value("eclosionGrade", 0);
    normalized.headSelect = value("headSelect", 0);
    normalized.whiteInkPercent = value("whiteInkPercent", 0);
    normalized.whiteInkPassCount = value("whiteInkPassCount", 0);
    normalized.varnishInkPercent = value("varnishInkPercent", 0);
    normalized.varnishInkPassCount = value("varnishInkPassCount", 0);
    normalized.headVoltage = value("headVoltage", 512);
    normalized.disableUv0 = value("disableUv0", 0);
    normalized.disableUv1 = value("disableUv1", 0);
    normalized.disableUv2 = value("disableUv2", 0);
    normalized.disableUv3 = value("disableUv3", 0);
    normalized.disableUv4 = value("disableUv4", 0);
    normalized.disableUv5 = value("disableUv5", 0);
    normalized.carReset = value("carReset", 1);
    normalized.stripBlank = value("stripBlank", 1);
    normalized.blankDistance = value("blankDistance", 0);
    return makeJobSettings(normalized);
}

static QVariantList byteArrayToVariantList(const char* values, int size)
{
    QVariantList out;
    for (int i = 0; i < size; ++i)
        out.append(static_cast<int>(values[i]));
    return out;
}

static void variantListToByteArray(const QVariant& value, char* out, int size)
{
    const QVariantList list = value.toList();
    for (int i = 0; i < size; ++i)
        out[i] = static_cast<char>(clampInt(i < list.size() ? list[i].toInt() : 0, -128, 127));
}

QVariantMap NocaiDirectPrintClient::alignmentValuesToMap(const AlignmentValues& values) const
{
    QVariantMap out;
    out["stepValue"] = static_cast<int>(values.StepValue);
    out["bidiValue"] = static_cast<int>(values.BidiValue);
    for (int i = 0; i < 4; ++i) {
        out[QString("horizontalSpacing%1").arg(i)] = static_cast<int>(values.HorizontalSpacing[i]);
        out[QString("verticalSpacing%1").arg(i)] = static_cast<int>(values.VerticalSpacing[i]);
    }
    out["horizontalAlignReference"] = static_cast<int>(values.HorizontalAlignReference);
    out["verticalAlignReference"] = static_cast<int>(values.VerticalAlignReference);
    out["leftChannelH1"] = byteArrayToVariantList(values.LeftChannelAlign_H1, 8);
    out["leftChannelH2"] = byteArrayToVariantList(values.LeftChannelAlign_H2, 8);
    out["leftChannelH3"] = byteArrayToVariantList(values.LeftChannelAlign_H3, 8);
    out["leftChannelH4"] = byteArrayToVariantList(values.LeftChannelAlign_H4, 8);
    out["rightChannelH1"] = byteArrayToVariantList(values.RightChannelAlign_H1, 8);
    out["rightChannelH2"] = byteArrayToVariantList(values.RightChannelAlign_H2, 8);
    out["rightChannelH3"] = byteArrayToVariantList(values.RightChannelAlign_H3, 8);
    out["rightChannelH4"] = byteArrayToVariantList(values.RightChannelAlign_H4, 8);
    return out;
}

NocaiDirectPrintClient::AlignmentValues
NocaiDirectPrintClient::alignmentValuesFromMap(const QVariantMap& settings) const
{
    AlignmentValues out;
    out.StepValue = static_cast<uint32_t>(std::max(0, settings.value("stepValue", 0).toInt()));
    out.BidiValue = static_cast<unsigned char>(clampInt(settings.value("bidiValue", 0).toInt(), 0, 255));
    for (int i = 0; i < 4; ++i) {
        out.HorizontalSpacing[i] = static_cast<int16_t>(clampInt(settings.value(QString("horizontalSpacing%1").arg(i), 0).toInt(), -32768, 32767));
        out.VerticalSpacing[i] = static_cast<int16_t>(clampInt(settings.value(QString("verticalSpacing%1").arg(i), 0).toInt(), -32768, 32767));
    }
    out.HorizontalAlignReference = static_cast<unsigned char>(clampInt(settings.value("horizontalAlignReference", 0).toInt(), 0, 255));
    out.VerticalAlignReference = static_cast<unsigned char>(clampInt(settings.value("verticalAlignReference", 0).toInt(), 0, 255));
    variantListToByteArray(settings.value("leftChannelH1"), out.LeftChannelAlign_H1, 8);
    variantListToByteArray(settings.value("leftChannelH2"), out.LeftChannelAlign_H2, 8);
    variantListToByteArray(settings.value("leftChannelH3"), out.LeftChannelAlign_H3, 8);
    variantListToByteArray(settings.value("leftChannelH4"), out.LeftChannelAlign_H4, 8);
    variantListToByteArray(settings.value("rightChannelH1"), out.RightChannelAlign_H1, 8);
    variantListToByteArray(settings.value("rightChannelH2"), out.RightChannelAlign_H2, 8);
    variantListToByteArray(settings.value("rightChannelH3"), out.RightChannelAlign_H3, 8);
    variantListToByteArray(settings.value("rightChannelH4"), out.RightChannelAlign_H4, 8);
    return out;
}

QVariantMap NocaiDirectPrintClient::uvParamValuesToMap(const UVParamValues& values) const
{
    QVariantMap out;
    out["rightR2LOffset"] = values.RightR2LOffset;
    out["rightL2ROffset"] = values.RightL2ROffset;
    out["leftR2LOffset"] = values.LeftR2LOffset;
    out["leftL2ROffset"] = values.LeftL2ROffset;
    out["lampL2ROffset"] = values.LampL2ROffset;
    return out;
}

NocaiDirectPrintClient::UVParamValues
NocaiDirectPrintClient::uvParamValuesFromMap(const QVariantMap& settings) const
{
    UVParamValues out;
    auto s16 = [&](const char* key) {
        return static_cast<int16_t>(clampInt(settings.value(QString::fromUtf8(key), 0).toInt(), -32768, 32767));
    };
    out.RightR2LOffset = s16("rightR2LOffset");
    out.RightL2ROffset = s16("rightL2ROffset");
    out.LeftR2LOffset = s16("leftR2LOffset");
    out.LeftL2ROffset = s16("leftL2ROffset");
    out.LampL2ROffset = s16("lampL2ROffset");
    return out;
}

QVariantMap NocaiDirectPrintClient::newUvParamValuesToMap(const NewUVParamValues& values) const
{
    QVariantMap out;
    out["leftStartOffset"] = values.UVLampLeftStartOffset;
    out["leftEndOffset"] = values.UVLampLeftEndOffset;
    out["leftMinOffset"] = values.UVLampLeftMinOffset;
    out["rightStartOffset"] = values.UVLampRightStartOffset;
    out["rightEndOffset"] = values.UVLampRightEndOffset;
    out["rightMinOffset"] = values.UVLampRightMinOffset;
    out["delayDistance"] = values.UVLampDelayDistance;
    return out;
}

NocaiDirectPrintClient::NewUVParamValues
NocaiDirectPrintClient::newUvParamValuesFromMap(const QVariantMap& settings) const
{
    NewUVParamValues out;
    auto s16 = [&](const char* key) {
        return static_cast<int16_t>(clampInt(settings.value(QString::fromUtf8(key), 0).toInt(), -32768, 32767));
    };
    out.UVLampLeftStartOffset = s16("leftStartOffset");
    out.UVLampLeftEndOffset = s16("leftEndOffset");
    out.UVLampLeftMinOffset = s16("leftMinOffset");
    out.UVLampRightStartOffset = s16("rightStartOffset");
    out.UVLampRightEndOffset = s16("rightEndOffset");
    out.UVLampRightMinOffset = s16("rightMinOffset");
    out.UVLampDelayDistance = s16("delayDistance");
    return out;
}

bool NocaiDirectPrintClient::withSdkWorkingDirectory(const std::function<bool()>& callback)
{
    if (m_resolvedSdkRoot.isEmpty()) {
        setError("Direct print SDK root is not resolved.");
        return false;
    }

    ScopedCurrentDir scoped(m_resolvedSdkRoot);
    if (!scoped.changed) {
        setError(QString("Failed to enter direct print SDK folder: %1").arg(m_resolvedSdkRoot));
        return false;
    }

    ScopedSdkStdoutCapture capture;
    return callback();
}

NocaiDirectPrintClient::JobSettings
NocaiDirectPrintClient::makeJobSettings(const DirectPrintSettings& settings) const
{
    JobSettings out;
    out.PrintDirection = static_cast<uint16_t>(clampInt(settings.printDirection, 0, 3));
    out.PrintSpeed = static_cast<uint16_t>(clampInt(settings.printSpeed, 0, 3));
    out.WCSequence = static_cast<uint16_t>(clampInt(settings.wcSequence, 0, 1));
    out.EclosionGrade = static_cast<uint16_t>(clampInt(settings.eclosionGrade, 0, 3));
    out.HeadSelect = static_cast<uint16_t>(clampInt(settings.headSelect, 0, 2));
    out.WInkPercent = static_cast<uint16_t>(clampInt(settings.whiteInkPercent, 0, 9));
    out.WInkPassCount = static_cast<uint16_t>(clampInt(settings.whiteInkPassCount, 0, 255));
    out.VInkPercent = static_cast<uint16_t>(clampInt(settings.varnishInkPercent, 0, 9));
    out.VInkPassCount = static_cast<uint16_t>(clampInt(settings.varnishInkPassCount, 0, 255));
    out.HeadVoltage = static_cast<uint16_t>(clampInt(settings.headVoltage, 400, 600));
    out.DisableUVLights[0] = static_cast<unsigned char>(clampInt(settings.disableUv0, 0, 1));
    out.DisableUVLights[1] = static_cast<unsigned char>(clampInt(settings.disableUv1, 0, 1));
    out.DisableUVLights[2] = static_cast<unsigned char>(clampInt(settings.disableUv2, 0, 1));
    out.DisableUVLights[3] = static_cast<unsigned char>(clampInt(settings.disableUv3, 0, 1));
    out.DisableUVLights[4] = static_cast<unsigned char>(clampInt(settings.disableUv4, 0, 1));
    out.DisableUVLights[5] = static_cast<unsigned char>(clampInt(settings.disableUv5, 0, 1));
    out.CarReset = static_cast<uint16_t>(clampInt(settings.carReset, 0, 1));
    out.stripBlank = static_cast<uint16_t>(clampInt(settings.stripBlank, 0, 2));
    out.blankDistance = static_cast<uint16_t>(clampInt(settings.blankDistance, 0, 65535));
    return out;
}
