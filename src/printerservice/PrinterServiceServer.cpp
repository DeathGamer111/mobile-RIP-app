#include "PrinterServiceServer.h"

#include "PrinterServiceProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTimer>

PrinterServiceServer::PrinterServiceServer(QObject* parent)
    : QObject(parent)
{
    m_backend.setAutoDiscoverSdk(true);
    connect(&m_server, &QLocalServer::newConnection,
            this, &PrinterServiceServer::acceptConnections);
    connect(&m_tcpServer, &QTcpServer::newConnection,
            this, &PrinterServiceServer::acceptTcpConnections);
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
        {QStringLiteral("available"), m_backend.isAvailable()},
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
