#include "PrinterServiceServer.h"

#include "PrinterServiceProtocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QScopeGuard>
#include <QUuid>

namespace {
constexpr qsizetype kUploadChunkBytes = 1024 * 1024;
constexpr qint64 kUploadExpiryMs = 5 * 60 * 1000;

QString uploadDirectory()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (root.isEmpty())
        root = QDir::tempPath();
    const QString path = QDir(root).filePath(QStringLiteral("PrintFlow-printer-uploads"));
    QDir().mkpath(path);
    return path;
}
}

PrinterServiceServer::PrinterServiceServer(QObject* parent)
    : QObject(parent)
{
    QDir uploadRoot(uploadDirectory());
    for (const QFileInfo& partial : uploadRoot.entryInfoList(
             {QStringLiteral("*.upload.partial")}, QDir::Files))
        QFile::remove(partial.absoluteFilePath());
    m_backend.setAutoDiscoverSdk(true);
    connect(&m_server, &QLocalServer::newConnection,
            this, &PrinterServiceServer::acceptConnections);
    connect(&m_tcpServer, &QTcpServer::newConnection,
            this, &PrinterServiceServer::acceptTcpConnections);
    auto* uploadCleanupTimer = new QTimer(this);
    uploadCleanupTimer->setInterval(60 * 1000);
    connect(uploadCleanupTimer, &QTimer::timeout,
            this, &PrinterServiceServer::removeExpiredUploads);
    uploadCleanupTimer->start();
}

PrinterServiceServer::~PrinterServiceServer()
{
    for (const UploadState& upload : std::as_const(m_uploads))
        QFile::remove(upload.partialPath);
}

bool PrinterServiceServer::listenTcp(const QHostAddress& address, quint16 port,
                                     QString* errorMessage)
{
    // The development bridge deliberately accepts loopback only. ADB reverse
    // makes this endpoint reachable from one attached Android target without
    // exposing printer-control commands to the local network.
    if (!PrintFlowPrinterServiceProtocol::isAllowedBridgeAddress(address)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The Android printer bridge may listen on loopback only.");
        }
        return false;
    }
    if (!m_tcpServer.listen(address, port)) {
        if (errorMessage)
            *errorMessage = m_tcpServer.errorString();
        return false;
    }
    return true;
}

bool PrinterServiceServer::listen(const QString& socketName,
                                  QString* errorMessage)
{
    const QFileInfo socketInfo(socketName);
    if (!QDir().mkpath(socketInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create printer-service socket directory: %1")
                                .arg(socketInfo.absolutePath());
        }
        return false;
    }

    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(socketName)) {
        // A process lock protects live services. At this point an existing
        // filesystem socket is stale and can be removed safely.
        QLocalServer::removeServer(socketName);
        if (!m_server.listen(socketName)) {
            if (errorMessage)
                *errorMessage = m_server.errorString();
            return false;
        }
    }
    return true;
}

QString PrinterServiceServer::socketName() const
{
    return m_server.serverName();
}

quint16 PrinterServiceServer::tcpPort() const
{
    return m_tcpServer.serverPort();
}

void PrinterServiceServer::acceptConnections()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead,
                this, &PrinterServiceServer::readClient);
        connect(socket, &QLocalSocket::disconnected,
                this, &PrinterServiceServer::removeClient);
        connect(socket, &QObject::destroyed, this, [this, socket]() {
            m_buffers.remove(socket);
        });
    }
}

void PrinterServiceServer::acceptTcpConnections()
{
    while (QTcpSocket* socket = m_tcpServer.nextPendingConnection()) {
        m_tcpBuffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead,
                this, &PrinterServiceServer::readTcpClient);
        connect(socket, &QTcpSocket::disconnected,
                this, &PrinterServiceServer::removeTcpClient);
        connect(socket, &QObject::destroyed, this, [this, socket]() {
            m_tcpBuffers.remove(socket);
        });
    }
}

void PrinterServiceServer::readClient()
{
    auto* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket || !m_buffers.contains(socket))
        return;
    QByteArray& buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    QVariantMap requestMessage;
    QString decodeError;
    const auto status = PrintFlowPrinterServiceProtocol::takeFrame(
        buffer, &requestMessage, &decodeError);
    if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Incomplete)
        return;
    if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid) {
        finishRequest(socket, response(false, {}, decodeError));
        return;
    }
    finishRequest(socket, handleRequest(requestMessage));
}

void PrinterServiceServer::readTcpClient()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_tcpBuffers.contains(socket))
        return;
    QByteArray& buffer = m_tcpBuffers[socket];
    buffer.append(socket->readAll());

    QVariantMap requestMessage;
    QString decodeError;
    const auto status = PrintFlowPrinterServiceProtocol::takeFrame(
        buffer, &requestMessage, &decodeError);
    if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Incomplete)
        return;
    if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid) {
        finishTcpRequest(socket, response(false, {}, decodeError));
        return;
    }
    qInfo("Android bridge request: %s",
          qPrintable(requestMessage.value(
              QStringLiteral("command")).toString()));
    finishTcpRequest(socket, handleRequest(requestMessage));
}

void PrinterServiceServer::removeClient()
{
    auto* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket)
        return;
    m_buffers.remove(socket);
    socket->deleteLater();
}

void PrinterServiceServer::removeTcpClient()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;
    m_tcpBuffers.remove(socket);
    socket->deleteLater();
}

QVariantMap PrinterServiceServer::serviceState()
{
    return {
        // Calling isAvailable() may reload the vendor library. Keep the SDK
        // isolated in its worker while a physical print is in progress.
        {QStringLiteral("available"),
         m_printInProgress ? true : m_backend.isAvailable()},
        {QStringLiteral("connected"), m_backend.isConnected()},
        {QStringLiteral("lastError"), m_backend.lastError()},
        {QStringLiteral("sdkRootPath"), m_backend.sdkRootPath()},
        {QStringLiteral("autoDiscoverSdk"), m_backend.autoDiscoverSdk()},
        {QStringLiteral("printers"), m_backend.printers()},
        {QStringLiteral("maintenanceSupportedPrinters"),
         m_backend.maintenanceSupportedPrinters()},
        {QStringLiteral("servicePid"), QCoreApplication::applicationPid()},
        {QStringLiteral("protocolVersion"),
         PrintFlowPrinterServiceProtocol::Version},
        {QStringLiteral("streamingRasterUpload"), true},
        {QStringLiteral("rasterUploadChunkBytes"), kUploadChunkBytes},
    };
}

QVariantMap PrinterServiceServer::response(bool ok, const QVariant& result,
                                           const QString& errorMessage)
{
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("result"), result},
        {QStringLiteral("error"), ok ? QString() : errorMessage},
        {QStringLiteral("state"), serviceState()},
    };
}

QVariantMap PrinterServiceServer::handleRequest(const QVariantMap& request)
{
    const QString command = request.value(QStringLiteral("command"))
                                .toString().trimmed();
    const QVariantMap arguments = request.value(
        QStringLiteral("arguments")).toMap();

    if (command == QLatin1String("ping"))
        return response(true, QStringLiteral("PrintFlowPrinterService"));

    if (m_printInProgress && command != QLatin1String("abortPrint")) {
        return response(false, {},
                        QStringLiteral("A print job is already in progress."));
    }

    if (command == QLatin1String("configure")) {
        m_backend.setSdkRootPath(arguments.value(
            QStringLiteral("sdkRootPath")).toString());
        m_backend.setAutoDiscoverSdk(arguments.value(
            QStringLiteral("autoDiscoverSdk"), true).toBool());
        return response(m_backend.isAvailable(), {}, m_backend.lastError());
    }

    if (command == QLatin1String("refreshPrinters")) {
        const bool ok = m_backend.refreshPrinters();
        return response(ok, m_backend.printers(), m_backend.lastError());
    }
    if (command == QLatin1String("choosePrinter")) {
        const bool ok = m_backend.choosePrinter(arguments.value(
            QStringLiteral("index"), -1).toInt());
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("connectPrinter")) {
        const bool ok = m_backend.connectPrinter();
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("reconnectPrinter")) {
        // A healthy ARM session must not be torn down merely because the user
        // asks to reconnect. Validate it first; only rebuild the SDK lifecycle
        // when the controller no longer answers status requests.
        if (m_backend.isConnected()) {
            const QVariantMap status = m_backend.getPrinterStatus();
            if (status.value(QStringLiteral("ok")).toBool()) {
                return response(true,
                                QStringLiteral("The existing printer session is healthy."));
            }
        }

        if (!m_backend.resetSdkSession())
            return response(false, {}, m_backend.lastError());
        if (!m_backend.refreshPrinters())
            return response(false, {}, m_backend.lastError());

        int selectedIndex = arguments.value(QStringLiteral("index"), -1).toInt();
        const QVariantList printers = m_backend.printers();
        bool indexExists = false;
        for (const QVariant& printer : printers) {
            if (printer.toMap().value(QStringLiteral("index"), -1).toInt() ==
                selectedIndex) {
                indexExists = true;
                break;
            }
        }
        if (!indexExists && printers.size() == 1) {
            selectedIndex = printers.constFirst().toMap().value(
                QStringLiteral("index"), 0).toInt();
            indexExists = true;
        }
        if (!indexExists) {
            return response(false, {},
                            printers.isEmpty()
                                ? QStringLiteral("No printer was found during reconnect.")
                                : QStringLiteral("Select a printer before reconnecting."));
        }
        if (!m_backend.choosePrinter(selectedIndex))
            return response(false, {}, m_backend.lastError());
        const bool ok = m_backend.connectPrinter();
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("statusText"))
        return response(true, m_backend.statusText());
    if (command == QLatin1String("getPrinterStatus")) {
        const QVariantMap value = m_backend.getPrinterStatus();
        return response(value.value(QStringLiteral("ok")).toBool(), value,
                        m_backend.lastError());
    }
    if (command == QLatin1String("getPrinterInfo")) {
        const QVariantMap value = m_backend.getPrinterInfo();
        return response(value.value(QStringLiteral("ok")).toBool(), value,
                        m_backend.lastError());
    }
    if (command == QLatin1String("abortPrint")) {
        const bool ok = m_backend.abortPrint();
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("pausePrint")) {
        const bool ok = m_backend.pausePrint();
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("continuePrint")) {
        const bool ok = m_backend.continuePrint();
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("maintenance")) {
        const QString action = arguments.value(QStringLiteral("action")).toString();
        const QVariantMap actionArguments = arguments.value(
            QStringLiteral("arguments")).toMap();
        const QVariantMap completion = m_backend.executeMaintenanceActionNow(
            action, actionArguments);
        return response(completion.value(QStringLiteral("ok")).toBool(),
                        completion.value(QStringLiteral("result")),
                        completion.value(QStringLiteral("error")).toString());
    }
    if (command == QLatin1String("beginRasterUpload")) {
        DirectPrintSpool expected;
        DirectPrintSettings settings;
        QString error;
        const quint64 expectedBytes = arguments.value(
            QStringLiteral("fileBytes")).toULongLong();
        if (!PrintFlowPrinterServiceProtocol::spoolMetadataFromMap(
                arguments.value(QStringLiteral("spool")).toMap(),
                &expected, &error) ||
            !PrintFlowPrinterServiceProtocol::settingsFromMap(
                arguments.value(QStringLiteral("settings")).toMap(),
                &settings, &error) ||
            !PrintFlowRasterSpool::metadataIsValid(expected, &error) ||
            expected.bodyOffset != PrintFlowRasterSpool::HeaderBytes ||
            expected.bodyBytes != PrintFlowRasterSpool::expectedBodyBytes(expected) ||
            expectedBytes != expected.bodyOffset + expected.bodyBytes) {
            if (error.isEmpty())
                error = QStringLiteral("Raster upload file length does not match its metadata.");
            return response(false, {}, error);
        }
        const QStorageInfo uploadStorage(uploadDirectory());
        const quint64 requiredUploadBytes = expectedBytes + expectedBytes / 5;
        if (!uploadStorage.isValid() || !uploadStorage.isReady() ||
            quint64(uploadStorage.bytesAvailable()) < requiredUploadBytes) {
            return response(
                false, {},
                QStringLiteral("Not enough temporary storage for the raster upload. Required: %1 MiB; available: %2 MiB.")
                    .arg((requiredUploadBytes + 1024 * 1024 - 1) / (1024 * 1024))
                    .arg(uploadStorage.isValid()
                             ? quint64(uploadStorage.bytesAvailable()) / (1024 * 1024)
                             : 0));
        }
        const QString uploadId = QUuid::createUuid().toString(QUuid::Id128);
        UploadState upload;
        upload.expectedBytes = expectedBytes;
        upload.expectedSpool = expected;
        upload.settings = settings;
        upload.lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        upload.partialPath = QDir(uploadDirectory()).filePath(
            uploadId + QStringLiteral(".upload.partial"));
        QFile file(upload.partialPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return response(false, {}, file.errorString());
        file.close();
        m_uploads.insert(uploadId, std::move(upload));
        return response(true, QVariantMap{
            {QStringLiteral("uploadId"), uploadId},
            {QStringLiteral("chunkBytes"), kUploadChunkBytes}});
    }
    if (command == QLatin1String("appendRasterChunk")) {
        const QString uploadId = arguments.value(
            QStringLiteral("uploadId")).toString();
        auto found = m_uploads.find(uploadId);
        if (found == m_uploads.end())
            return response(false, {}, QStringLiteral("Raster upload does not exist."));
        const quint64 offset = arguments.value(QStringLiteral("offset")).toULongLong();
        const QByteArray chunk = request.value(QStringLiteral("payload")).toByteArray();
        if (offset != found->receivedBytes || chunk.isEmpty() ||
            chunk.size() > kUploadChunkBytes ||
            found->receivedBytes + quint64(chunk.size()) > found->expectedBytes) {
            return response(false, {}, QStringLiteral("Raster upload chunk offset or size is invalid."));
        }
        QFile file(found->partialPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append) ||
            file.write(chunk) != chunk.size()) {
            const QString error = file.errorString();
            file.close();
            QFile::remove(found->partialPath);
            m_uploads.erase(found);
            return response(false, {}, error);
        }
        file.close();
        found->receivedBytes += quint64(chunk.size());
        found->lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        return response(true, qulonglong(found->receivedBytes));
    }
    if (command == QLatin1String("cancelRasterUpload")) {
        const QString uploadId = arguments.value(
            QStringLiteral("uploadId")).toString();
        const auto found = m_uploads.find(uploadId);
        if (found != m_uploads.end()) {
            QFile::remove(found->partialPath);
            m_uploads.erase(found);
        }
        return response(true);
    }
    if (command == QLatin1String("commitRasterUpload")) {
        const QString uploadId = arguments.value(
            QStringLiteral("uploadId")).toString();
        auto found = m_uploads.find(uploadId);
        if (found == m_uploads.end())
            return response(false, {}, QStringLiteral("Raster upload does not exist."));
        UploadState upload = *found;
        m_uploads.erase(found);
        if (upload.receivedBytes != upload.expectedBytes) {
            QFile::remove(upload.partialPath);
            return response(false, {}, QStringLiteral("Raster upload is incomplete."));
        }
        QString finalPath = upload.partialPath;
        finalPath.chop(QStringLiteral(".upload.partial").size());
        finalPath += QStringLiteral(".pfrs");
        if (!QFile::rename(upload.partialPath, finalPath)) {
            QFile::remove(upload.partialPath);
            return response(false, {}, QStringLiteral("Could not finalize the uploaded raster spool."));
        }
        DirectPrintSpool uploaded;
        QString error;
        bool valid = PrintFlowRasterSpool::readMetadata(finalPath, &uploaded, &error) &&
                     PrintFlowRasterSpool::verify(uploaded, &error);
        const DirectPrintSpool& expected = upload.expectedSpool;
        valid = valid && uploaded.width == expected.width &&
            uploaded.height == expected.height && uploaded.xdpi == expected.xdpi &&
            uploaded.ydpi == expected.ydpi &&
            uploaded.bytesPerLine == expected.bytesPerLine &&
            uploaded.logicalChannelCount == expected.logicalChannelCount &&
            uploaded.channelOrder == expected.channelOrder &&
            uploaded.format == expected.format &&
            uploaded.canonicalHeader == expected.canonicalHeader &&
            uploaded.bodyBytes == expected.bodyBytes &&
            uploaded.sha256 == expected.sha256;
        if (!valid) {
            QFile::remove(finalPath);
            return response(false, {}, error.isEmpty()
                ? QStringLiteral("Uploaded raster metadata does not match the submitted job.")
                : error);
        }
        m_printInProgress = true;
        const auto printGuard = qScopeGuard([this]() {
            m_printInProgress = false;
        });
        const bool ok = m_backend.submitSpooledJob(uploaded, upload.settings);
        QFile::remove(finalPath);
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("submitJob")) {
        std::vector<std::vector<std::vector<uint8_t>>> storage;
        DirectPrintRaster raster;
        DirectPrintSettings settings;
        QString error;
        const bool decoded =
            PrintFlowPrinterServiceProtocol::deserializeRaster(
                arguments.value(QStringLiteral("raster")).toMap(),
                request.value(QStringLiteral("payload")).toByteArray(),
                &storage, &raster, &error) &&
            PrintFlowPrinterServiceProtocol::settingsFromMap(
                arguments.value(QStringLiteral("settings")).toMap(),
                &settings, &error);
        if (!decoded)
            return response(false, {}, error);
        const bool ok = m_backend.submitPreparedJob(raster, settings);
        return response(ok, {}, m_backend.lastError());
    }
    if (command == QLatin1String("shutdown")) {
        QTimer::singleShot(0, QCoreApplication::instance(),
                           &QCoreApplication::quit);
        return response(true);
    }

    return response(false, {},
                    QStringLiteral("Unknown printer-service command: %1")
                        .arg(command));
}

void PrinterServiceServer::removeExpiredUploads()
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kUploadExpiryMs;
    for (auto upload = m_uploads.begin(); upload != m_uploads.end();) {
        if (upload->lastActivityMs > cutoff) {
            ++upload;
            continue;
        }
        QFile::remove(upload->partialPath);
        upload = m_uploads.erase(upload);
    }
}

void PrinterServiceServer::finishRequest(QLocalSocket* socket,
                                         const QVariantMap& result)
{
    if (!socket)
        return;
    const QByteArray frame = PrintFlowPrinterServiceProtocol::encodeFrame(result);
    if (!frame.isEmpty()) {
        socket->write(frame);
        socket->flush();
        socket->waitForBytesWritten(5000);
    }
    m_buffers.remove(socket);
    socket->disconnectFromServer();
}

void PrinterServiceServer::finishTcpRequest(QTcpSocket* socket,
                                            const QVariantMap& result)
{
    if (!socket)
        return;
    const QByteArray frame = PrintFlowPrinterServiceProtocol::encodeFrame(result);
    if (!frame.isEmpty()) {
        socket->write(frame);
        socket->flush();
        socket->waitForBytesWritten(5000);
    }
    m_tcpBuffers.remove(socket);
    socket->disconnectFromHost();
}
