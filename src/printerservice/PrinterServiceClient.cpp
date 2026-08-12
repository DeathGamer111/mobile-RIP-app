#include "PrinterServiceClient.h"

#include "PrinterServiceProtocol.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <csignal>
#endif

namespace {

constexpr int kServiceStartupTimeoutMs = 10000;
constexpr int kJobTimeoutMs = 30 * 60 * 1000;
constexpr int kMaintenanceTimeoutMs = 10 * 60 * 1000;
constexpr int kServiceShutdownTimeoutMs = 10000;
constexpr qint64 kDiagnosticLogReadLimit = 512 * 1024;

QString serviceExecutablePath()
{
    const QString configured = qEnvironmentVariable(
        "PRINTFLOW_PRINTER_SERVICE_EXECUTABLE").trimmed();
    if (!configured.isEmpty())
        return QFileInfo(configured).absoluteFilePath();

    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("PrintFlowPrinterService")),
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("../libexec/printflow/PrintFlowPrinterService")),
        QStringLiteral("/usr/local/libexec/printflow/PrintFlowPrinterService"),
        QStringLiteral("/usr/libexec/printflow/PrintFlowPrinterService"),
        QStandardPaths::findExecutable(
            QStringLiteral("PrintFlowPrinterService")),
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (!candidate.isEmpty() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return candidates.constFirst();
}

QString serviceLogPath()
{
    const QString logDirectory = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                     .absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(logDirectory);
    return QDir(logDirectory).absoluteFilePath(
        QStringLiteral("printer-service.log"));
}

#if defined(Q_OS_LINUX)
bool serviceProcessIsRunning(qint64 pid)
{
    if (pid <= 0)
        return false;
    errno = 0;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

bool isExpectedServiceProcess(qint64 pid)
{
    if (!serviceProcessIsRunning(pid))
        return false;
    QString runningExecutable = QFileInfo(
        QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    const QString deletedSuffix = QStringLiteral(" (deleted)");
    if (runningExecutable.endsWith(deletedSuffix))
        runningExecutable.chop(deletedSuffix.size());
    return QFileInfo(runningExecutable).absoluteFilePath() ==
           QFileInfo(serviceExecutablePath()).absoluteFilePath();
}

bool terminateExpectedService(qint64 pid, QString* errorMessage)
{
    if (!isExpectedServiceProcess(pid)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Refused to terminate PID %1 because it is not the configured PrintFlow printer service.")
                                .arg(pid);
        }
        return false;
    }

    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0 && errno != ESRCH) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not terminate printer service PID %1.")
                                .arg(pid);
        }
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000 && serviceProcessIsRunning(pid))
        QThread::msleep(100);
    if (!serviceProcessIsRunning(pid))
        return true;

    if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0 && errno != ESRCH) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Printer service PID %1 ignored its stop request.")
                                .arg(pid);
        }
        return false;
    }
    timer.restart();
    while (timer.elapsed() < 2000 && serviceProcessIsRunning(pid))
        QThread::msleep(100);
    if (serviceProcessIsRunning(pid)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Printer service PID %1 could not be stopped.")
                                .arg(pid);
        }
        return false;
    }
    return true;
}
#endif

} // namespace

PrinterServiceClient::PrinterServiceClient(QObject* parent)
    : QObject(parent)
{
    connect(&m_maintenanceWatcher, &QFutureWatcher<QVariantMap>::finished,
            this, [this]() {
        const QVariantMap response = m_maintenanceWatcher.result();
        applyResponse(response);
        const QString action = m_currentMaintenanceAction;
        const bool ok = response.value(QStringLiteral("ok")).toBool();
        const QVariant result = response.value(QStringLiteral("result"));
        const QString error = response.value(QStringLiteral("error")).toString();
        m_currentMaintenanceAction.clear();
        m_maintenanceBusy = false;
        emit maintenanceBusyChanged();
        emit maintenanceActionFinished(action, ok, result, error);
        emit statusChanged();
    });

    request(QStringLiteral("ping"));
}

PrinterServiceClient::~PrinterServiceClient()
{
    if (m_maintenanceWatcher.isRunning())
        m_maintenanceWatcher.waitForFinished();
    // The service deliberately outlives the GUI and remains the sole SDK owner.
}

bool PrinterServiceClient::isAvailable()
{
    QMutexLocker locker(&m_mutex);
    return m_available;
}

bool PrinterServiceClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected;
}

QString PrinterServiceClient::vendorName() const
{
    return QStringLiteral("PrintFlow unified printer service");
}

QString PrinterServiceClient::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

QString PrinterServiceClient::sdkRootPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_sdkRootPath;
}

void PrinterServiceClient::setSdkRootPath(const QString& path)
{
    const QString clean = path.trimmed();
    {
        QMutexLocker locker(&m_mutex);
        if (m_sdkRootPath == clean)
            return;
        m_sdkRootPath = clean;
    }
    request(QStringLiteral("configure"), configureArguments());
}

bool PrinterServiceClient::autoDiscoverSdk() const
{
    QMutexLocker locker(&m_mutex);
    return m_autoDiscoverSdk;
}

void PrinterServiceClient::setAutoDiscoverSdk(bool enabled)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_autoDiscoverSdk == enabled)
            return;
        m_autoDiscoverSdk = enabled;
    }
    request(QStringLiteral("configure"), configureArguments());
}

QVariantList PrinterServiceClient::printers() const
{
    QMutexLocker locker(&m_mutex);
    return m_printers;
}

QStringList PrinterServiceClient::maintenanceSupportedPrinters() const
{
    return {QStringLiteral("X-33")};
}

bool PrinterServiceClient::maintenanceBusy() const
{
    QMutexLocker locker(&m_mutex);
    return m_maintenanceBusy;
}

qint64 PrinterServiceClient::servicePid() const
{
    QMutexLocker locker(&m_mutex);
    return m_servicePid;
}

QVariantList PrinterServiceClient::searchPrinters()
{
    refreshPrinters();
    return printers();
}

bool PrinterServiceClient::supportsMaintenance(const QString& printerName) const
{
    return maintenanceSupportedPrinters().contains(
        printerName.trimmed(), Qt::CaseInsensitive);
}

bool PrinterServiceClient::refreshPrinters()
{
    return responseOk(request(QStringLiteral("refreshPrinters")));
}

bool PrinterServiceClient::choosePrinter(int index)
{
    return responseOk(request(QStringLiteral("choosePrinter"),
                              {{QStringLiteral("index"), index}}));
}

bool PrinterServiceClient::abortPrint()
{
    return responseOk(request(QStringLiteral("abortPrint")));
}

bool PrinterServiceClient::pausePrint()
{
    return responseOk(request(QStringLiteral("pausePrint")));
}

bool PrinterServiceClient::continuePrint()
{
    return responseOk(request(QStringLiteral("continuePrint")));
}

QString PrinterServiceClient::statusText()
{
    const QVariantMap value = request(QStringLiteral("statusText"));
    return value.value(QStringLiteral("ok")).toBool()
        ? value.value(QStringLiteral("result")).toString()
        : lastError();
}

bool PrinterServiceClient::connectPrinter()
{
    return responseOk(request(QStringLiteral("connectPrinter"), {}, {},
                              kMaintenanceTimeoutMs));
}

bool PrinterServiceClient::startReconnectPrinter(int printerIndex)
{
    return startAsyncAction(QStringLiteral("ReconnectPrinter"),
                            [this, printerIndex]() {
        return exchange(QStringLiteral("reconnectPrinter"),
                        {{QStringLiteral("index"), printerIndex}}, {},
                        kMaintenanceTimeoutMs, true);
    });
}

bool PrinterServiceClient::startRestartService(int printerIndex)
{
    return startAsyncAction(QStringLiteral("RestartPrinterService"),
                            [this, printerIndex]() {
        return restartService(printerIndex);
    });
}

QString PrinterServiceClient::diagnosticLogPath() const
{
    return serviceLogPath();
}

QString PrinterServiceClient::diagnosticLog() const
{
    QFile log(serviceLogPath());
    if (!log.open(QIODevice::ReadOnly))
        return {};
    if (log.size() > kDiagnosticLogReadLimit)
        log.seek(log.size() - kDiagnosticLogReadLimit);
    return QString::fromUtf8(log.readAll());
}

bool PrinterServiceClient::wipePrintHead(int printHeadMask)
{
    return maintenanceBool(QStringLiteral("WipePrintHead"),
                           {{QStringLiteral("headMask"), printHeadMask}});
}

bool PrinterServiceClient::startCleanOperation(int printHeadMask)
{
    return maintenanceBool(QStringLiteral("StartCleanOperation"),
                           {{QStringLiteral("headMask"), printHeadMask}});
}

bool PrinterServiceClient::startPump(int printHeadMask)
{
    return maintenanceBool(QStringLiteral("StartPump"),
                           {{QStringLiteral("headMask"), printHeadMask}});
}

bool PrinterServiceClient::stopPumpOperation()
{
    return maintenanceBool(QStringLiteral("StopPumpOperation"));
}

bool PrinterServiceClient::spitPrintHead(int printHeadMask)
{
    return maintenanceBool(QStringLiteral("StartFlashSpray"),
                           {{QStringLiteral("headMask"), printHeadMask}});
}

bool PrinterServiceClient::stopSpitOperation()
{
    return maintenanceBool(QStringLiteral("StopFlashSpray"));
}

bool PrinterServiceClient::capPrintHead()
{
    return maintenanceBool(QStringLiteral("CapPrintHead"));
}

bool PrinterServiceClient::moveAxis(int axis, int direction)
{
    return maintenanceBool(QStringLiteral("MoveAxis"),
                           {{QStringLiteral("axis"), axis},
                            {QStringLiteral("direction"), direction}});
}

QVariantMap PrinterServiceClient::stopAxis(int axis)
{
    return maintenanceMap(QStringLiteral("StopAxis"),
                          {{QStringLiteral("axis"), axis}});
}

QVariantMap PrinterServiceClient::saveAxisPos(int axis)
{
    return maintenanceMap(QStringLiteral("SaveAxisPos"),
                          {{QStringLiteral("axis"), axis}});
}

bool PrinterServiceClient::setPrintHeight(double heightMm)
{
    return maintenanceBool(QStringLiteral("SetPrintHeight"),
                           {{QStringLiteral("heightMm"), heightMm}});
}

QVariantMap PrinterServiceClient::getPrintHeight()
{
    return maintenanceMap(QStringLiteral("GetPrintHeight"));
}

QVariantMap PrinterServiceClient::getJobSettings()
{
    return maintenanceMap(QStringLiteral("GetJobSettings"));
}

bool PrinterServiceClient::setJobSettingsFromMap(const QVariantMap& settings)
{
    return maintenanceBool(QStringLiteral("SetJobSettings"),
                           {{QStringLiteral("settings"), settings}});
}

bool PrinterServiceClient::exportConfigFile(const QString& path)
{
    return maintenanceBool(QStringLiteral("ExportConfigFile"),
                           {{QStringLiteral("path"), path}});
}

bool PrinterServiceClient::importConfigFile(const QString& path)
{
    return maintenanceBool(QStringLiteral("ImportConfigFile"),
                           {{QStringLiteral("path"), path}});
}

QVariantMap PrinterServiceClient::getAlignmentValues()
{
    return maintenanceMap(QStringLiteral("GetAlignmentValues"));
}

bool PrinterServiceClient::setAlignmentValues(const QVariantMap& settings,
                                              int type)
{
    return maintenanceBool(QStringLiteral("SetAlignmentValues"),
                           {{QStringLiteral("settings"), settings},
                            {QStringLiteral("type"), type}});
}

bool PrinterServiceClient::printNozzleCheck()
{
    return maintenanceBool(QStringLiteral("PrintNozzleCheck"));
}

bool PrinterServiceClient::printAlignmentPattern(int type)
{
    return maintenanceBool(QStringLiteral("PrintAlignmentPattern"),
                           {{QStringLiteral("type"), type}});
}

QVariantMap PrinterServiceClient::getPrinterStatus()
{
    const QVariantMap response = request(QStringLiteral("getPrinterStatus"));
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    if (!result.contains(QStringLiteral("ok")))
        result.insert(QStringLiteral("ok"), false);
    return result;
}

QVariantMap PrinterServiceClient::getPrinterInfo()
{
    const QVariantMap response = request(QStringLiteral("getPrinterInfo"));
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    if (!result.contains(QStringLiteral("ok")))
        result.insert(QStringLiteral("ok"), false);
    return result;
}

bool PrinterServiceClient::setPrintXYValue(int xMm, int yMm)
{
    return maintenanceBool(QStringLiteral("SetPrintXYValue"),
                           {{QStringLiteral("xMm"), xMm},
                            {QStringLiteral("yMm"), yMm}});
}

QVariantMap PrinterServiceClient::getPrintXYValue()
{
    return maintenanceMap(QStringLiteral("GetPrintXYValue"));
}

QVariantMap PrinterServiceClient::getUVParamValues()
{
    return maintenanceMap(QStringLiteral("GetUVParamValues"));
}

bool PrinterServiceClient::setUVParamValues(const QVariantMap& settings,
                                            int type)
{
    return maintenanceBool(QStringLiteral("SetUVParamValues"),
                           {{QStringLiteral("settings"), settings},
                            {QStringLiteral("type"), type}});
}

int PrinterServiceClient::getSupportNewUVParamFunction()
{
    const QVariantMap response = request(
        QStringLiteral("maintenance"),
        {{QStringLiteral("action"),
          QStringLiteral("GetSupportNewUVParamFunction")},
         {QStringLiteral("arguments"), QVariantMap()}});
    return response.value(QStringLiteral("ok")).toBool()
        ? response.value(QStringLiteral("result")).toInt() : -1;
}

bool PrinterServiceClient::setNewUVParamFunction(int type)
{
    return maintenanceBool(QStringLiteral("SetNewUVParamFunction"),
                           {{QStringLiteral("type"), type}});
}

QVariantMap PrinterServiceClient::getNewUVParamValues()
{
    return maintenanceMap(QStringLiteral("GetNewUVParamValues"));
}

bool PrinterServiceClient::setNewUVParamValues(const QVariantMap& settings,
                                               int type)
{
    return maintenanceBool(QStringLiteral("SetNewUVParamValues"),
                           {{QStringLiteral("settings"), settings},
                            {QStringLiteral("type"), type}});
}

bool PrinterServiceClient::startMaintenanceAction(
    const QString& action, const QVariantMap& arguments)
{
    const QString normalized = action.trimmed();
    if (normalized.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        m_lastError = QStringLiteral("Printer service action is empty.");
        emit statusChanged();
        return false;
    }
    if (normalized == QLatin1String("GetPrinterStatus")) {
        return startAsyncAction(normalized, [this]() {
            return exchange(QStringLiteral("getPrinterStatus"), {}, {},
                            kMaintenanceTimeoutMs, true);
        });
    }
    if (normalized == QLatin1String("GetPrinterInfo")) {
        return startAsyncAction(normalized, [this]() {
            return exchange(QStringLiteral("getPrinterInfo"), {}, {},
                            kMaintenanceTimeoutMs, true);
        });
    }
    return startAsyncAction(normalized, [this, normalized, arguments]() {
        return exchange(
            QStringLiteral("maintenance"),
            {{QStringLiteral("action"), normalized},
             {QStringLiteral("arguments"), arguments}},
            {}, kMaintenanceTimeoutMs, true);
    });
}

bool PrinterServiceClient::startAsyncAction(
    const QString& action, std::function<QVariantMap()> operation)
{
    QMutexLocker locker(&m_mutex);
    if (m_maintenanceBusy) {
        m_lastError = QStringLiteral(
            "A printer service operation is already running.");
        emit statusChanged();
        return false;
    }
    m_currentMaintenanceAction = action;
    m_maintenanceBusy = true;
    emit maintenanceBusyChanged();
    m_maintenanceWatcher.setFuture(QtConcurrent::run(std::move(operation)));
    return true;
}

QVariantMap PrinterServiceClient::restartService(int printerIndex)
{
    qint64 previousPid = 0;
    {
        QMutexLocker locker(&m_mutex);
        previousPid = m_servicePid;
    }

    // The service destructor releases the SDK and its raw socket before exit.
    // A failed shutdown response is tolerated only when the old endpoint is
    // already gone; the readiness checks below remain authoritative.
    exchange(QStringLiteral("shutdown"), {}, {}, 3000, false);

    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    bool stopped = false;
    while (shutdownTimer.elapsed() < kServiceShutdownTimeoutMs) {
#if defined(Q_OS_LINUX)
        if (!serviceProcessIsRunning(previousPid)) {
            stopped = true;
            break;
        }
#else
        const QVariantMap ping = exchange(QStringLiteral("ping"), {}, {},
                                          500, false);
        if (!ping.value(QStringLiteral("ok")).toBool()) {
            stopped = true;
            break;
        }
#endif
        QThread::msleep(100);
    }
    if (!stopped) {
#if defined(Q_OS_LINUX)
        QString terminationError;
        if (terminateExpectedService(previousPid, &terminationError)) {
            stopped = true;
        } else {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), terminationError}};
        }
#else
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("Printer service PID %1 did not stop cleanly.")
                     .arg(previousPid)}};
#endif
    }

    QString startupError;
    if (!ensureService(&startupError)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), startupError}};
    }

    QVariantMap response = exchange(QStringLiteral("configure"),
                                    configureArguments(), {}, 5000, false);
    if (!response.value(QStringLiteral("ok")).toBool())
        return response;
    response = exchange(QStringLiteral("refreshPrinters"), {}, {},
                        kMaintenanceTimeoutMs, false);
    if (!response.value(QStringLiteral("ok")).toBool())
        return response;

    const QVariantList printers = response.value(QStringLiteral("state"))
                                      .toMap()
                                      .value(QStringLiteral("printers"))
                                      .toList();
    bool indexExists = false;
    for (const QVariant& printer : printers) {
        if (printer.toMap().value(QStringLiteral("index"), -1).toInt() ==
            printerIndex) {
            indexExists = true;
            break;
        }
    }
    if (!indexExists && printers.size() == 1) {
        printerIndex = printers.constFirst().toMap().value(
            QStringLiteral("index"), 0).toInt();
        indexExists = true;
    }
    if (!indexExists) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 printers.isEmpty()
                     ? QStringLiteral("The restarted service did not discover a printer.")
                     : QStringLiteral("Select a printer before restarting its service.")}};
    }

    response = exchange(QStringLiteral("choosePrinter"),
                        {{QStringLiteral("index"), printerIndex}}, {},
                        kMaintenanceTimeoutMs, false);
    if (!response.value(QStringLiteral("ok")).toBool())
        return response;
    return exchange(QStringLiteral("connectPrinter"), {}, {},
                    kMaintenanceTimeoutMs, false);
}

bool PrinterServiceClient::submitPreparedJob(
    const DirectPrintRaster& raster, const DirectPrintSettings& settings)
{
    QByteArray payload;
    QString serializationError;
    if (!PrintFlowPrinterServiceProtocol::serializeRaster(
            raster, &payload, &serializationError)) {
        QMutexLocker locker(&m_mutex);
        m_lastError = serializationError;
        emit statusChanged();
        return false;
    }
    const QVariantMap arguments{
        {QStringLiteral("raster"),
         PrintFlowPrinterServiceProtocol::rasterMetadata(raster)},
        {QStringLiteral("settings"),
         PrintFlowPrinterServiceProtocol::settingsToMap(settings)},
    };
    return responseOk(request(QStringLiteral("submitJob"), arguments,
                              payload, kJobTimeoutMs));
}

QVariantMap PrinterServiceClient::request(const QString& command,
                                          const QVariantMap& arguments,
                                          const QByteArray& payload,
                                          int timeoutMs)
{
    QVariantMap value = exchange(command, arguments, payload,
                                 timeoutMs, true);
    applyResponse(value);
    return value;
}

QVariantMap PrinterServiceClient::exchange(const QString& command,
                                           const QVariantMap& arguments,
                                           const QByteArray& payload,
                                           int timeoutMs,
                                           bool startService)
{
    QString startupError;
    if (startService && !ensureService(&startupError)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), startupError}};
    }

    QLocalSocket socket;
    socket.connectToServer(
        PrintFlowPrinterServiceProtocol::defaultSocketName(),
        QIODevice::ReadWrite);
    if (!socket.waitForConnected(std::min(timeoutMs, 3000))) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("Could not connect to PrintFlow printer service: %1")
                     .arg(socket.errorString())}};
    }

    QVariantMap message{
        {QStringLiteral("command"), command},
        {QStringLiteral("arguments"), arguments},
    };
    if (!payload.isEmpty())
        message.insert(QStringLiteral("payload"), payload);
    const QByteArray frame = PrintFlowPrinterServiceProtocol::encodeFrame(message);
    if (frame.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("Printer-service request is too large.")}};
    }
    socket.write(frame);

    QDeadlineTimer deadline(timeoutMs);
    while (socket.bytesToWrite() > 0 && !deadline.hasExpired()) {
        if (!socket.waitForBytesWritten(
                std::max(1, int(std::min<qint64>(deadline.remainingTime(), 1000))))) {
            if (socket.error() != QLocalSocket::UnknownSocketError)
                break;
        }
    }
    if (socket.bytesToWrite() > 0) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("Timed out sending request to printer service.")}};
    }

    QByteArray responseBuffer;
    while (!deadline.hasExpired()) {
        responseBuffer.append(socket.readAll());
        QVariantMap decoded;
        QString decodeError;
        const auto decodeStatus = PrintFlowPrinterServiceProtocol::takeFrame(
            responseBuffer, &decoded, &decodeError);
        if (decodeStatus == PrintFlowPrinterServiceProtocol::DecodeStatus::Complete)
            return decoded;
        if (decodeStatus == PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), decodeError}};
        }
        const int waitMs = std::max(
            1, int(std::min<qint64>(deadline.remainingTime(), 1000)));
        if (!socket.waitForReadyRead(waitMs) &&
            socket.state() == QLocalSocket::UnconnectedState) {
            responseBuffer.append(socket.readAll());
            const auto finalStatus = PrintFlowPrinterServiceProtocol::takeFrame(
                responseBuffer, &decoded, &decodeError);
            if (finalStatus == PrintFlowPrinterServiceProtocol::DecodeStatus::Complete)
                return decoded;
            break;
        }
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"),
             QStringLiteral("Printer service did not respond before the timeout.")}};
}

bool PrinterServiceClient::ensureService(QString* errorMessage)
{
    const QVariantMap existing = exchange(QStringLiteral("ping"), {}, {},
                                          1000, false);
    if (existing.value(QStringLiteral("ok")).toBool())
        return true;

    const QString executable = serviceExecutablePath();
    if (!QFileInfo::exists(executable)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "PrintFlow printer service executable is missing: %1")
                                .arg(executable);
        }
        return false;
    }
    const QString logPath = serviceLogPath();
    if (QFileInfo(logPath).size() > 10 * 1024 * 1024) {
        QFile::remove(logPath + QStringLiteral(".1"));
        QFile::rename(logPath, logPath + QStringLiteral(".1"));
    }
    QProcess serviceProcess;
    serviceProcess.setProgram(executable);
    serviceProcess.setArguments(
        {QStringLiteral("--socket"),
         PrintFlowPrinterServiceProtocol::defaultSocketName()});
    serviceProcess.setWorkingDirectory(QFileInfo(executable).absolutePath());
    serviceProcess.setStandardOutputFile(logPath, QIODevice::Append);
    serviceProcess.setStandardErrorFile(logPath, QIODevice::Append);
    qint64 pid = 0;
    if (!serviceProcess.startDetached(&pid)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not start PrintFlow printer service.");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kServiceStartupTimeoutMs) {
        QThread::msleep(100);
        const QVariantMap hello = exchange(QStringLiteral("ping"), {}, {},
                                           1000, false);
        if (hello.value(QStringLiteral("ok")).toBool())
            return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral(
            "PrintFlow printer service did not become ready within %1 seconds.")
                            .arg(kServiceStartupTimeoutMs / 1000);
    }
    return false;
}

void PrinterServiceClient::applyResponse(const QVariantMap& response)
{
    const QVariantMap state = response.value(QStringLiteral("state")).toMap();
    QMutexLocker locker(&m_mutex);
    const QVariantList previousPrinters = m_printers;
    if (!state.isEmpty()) {
        m_available = state.value(QStringLiteral("available"), m_available).toBool();
        m_connected = state.value(QStringLiteral("connected"), m_connected).toBool();
        m_servicePid = state.value(QStringLiteral("servicePid"), m_servicePid).toLongLong();
        m_printers = state.value(QStringLiteral("printers"), m_printers).toList();
        m_sdkRootPath = state.value(
            QStringLiteral("sdkRootPath"), m_sdkRootPath).toString();
        m_autoDiscoverSdk = state.value(
            QStringLiteral("autoDiscoverSdk"), m_autoDiscoverSdk).toBool();
        const QString backendError = state.value(
            QStringLiteral("lastError")).toString();
        if (!backendError.isEmpty())
            m_lastError = backendError;
    }
    if (!response.value(QStringLiteral("ok")).toBool()) {
        const QString requestError = response.value(QStringLiteral("error")).toString();
        if (!requestError.isEmpty())
            m_lastError = requestError;
    } else if (state.value(QStringLiteral("lastError")).toString().isEmpty()) {
        m_lastError.clear();
    }
    if (previousPrinters != m_printers)
        emit printersChanged();
    emit statusChanged();
}

bool PrinterServiceClient::responseOk(const QVariantMap& response)
{
    return response.value(QStringLiteral("ok")).toBool();
}

bool PrinterServiceClient::maintenanceBool(
    const QString& action, const QVariantMap& arguments)
{
    return responseOk(request(
        QStringLiteral("maintenance"),
        {{QStringLiteral("action"), action},
         {QStringLiteral("arguments"), arguments}},
        {}, kMaintenanceTimeoutMs));
}

QVariantMap PrinterServiceClient::maintenanceMap(
    const QString& action, const QVariantMap& arguments)
{
    const QVariantMap response = request(
        QStringLiteral("maintenance"),
        {{QStringLiteral("action"), action},
         {QStringLiteral("arguments"), arguments}},
        {}, kMaintenanceTimeoutMs);
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    if (!result.contains(QStringLiteral("ok")))
        result.insert(QStringLiteral("ok"), responseOk(response));
    return result;
}

QVariantMap PrinterServiceClient::configureArguments() const
{
    QMutexLocker locker(&m_mutex);
    return {
        {QStringLiteral("sdkRootPath"), m_sdkRootPath},
        {QStringLiteral("autoDiscoverSdk"), m_autoDiscoverSdk},
    };
}
