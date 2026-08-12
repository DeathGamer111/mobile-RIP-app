#include "PrinterServiceProtocol.h"
#include "PrinterServiceServer.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QLockFile>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <csignal>

namespace {

volatile std::sig_atomic_t g_terminationRequested = 0;

void terminationHandler(int)
{
    g_terminationRequested = 1;
}

void installTerminationHandlers()
{
    struct sigaction action = {};
    action.sa_handler = terminationHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

int sendControlCommand(const QString& socketName, const QString& command)
{
    QLocalSocket socket;
    socket.connectToServer(socketName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(1000))
        return command == QLatin1String("shutdown") ? 0 : 3;

    const QByteArray frame = PrintFlowPrinterServiceProtocol::encodeFrame(
        {{QStringLiteral("command"), command},
         {QStringLiteral("arguments"), QVariantMap{}}});
    if (frame.isEmpty())
        return 4;
    socket.write(frame);
    if (!socket.waitForBytesWritten(1000))
        return 5;

    QByteArray responseBuffer;
    QDeadlineTimer deadline(3000);
    while (!deadline.hasExpired()) {
        responseBuffer.append(socket.readAll());
        QVariantMap response;
        QString error;
        const auto status = PrintFlowPrinterServiceProtocol::takeFrame(
            responseBuffer, &response, &error);
        if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Complete) {
            if (command == QLatin1String("ping")) {
                std::printf("%s\n", qPrintable(
                    response.value(QStringLiteral("result")).toString()));
            }
            return response.value(QStringLiteral("ok")).toBool() ? 0 : 6;
        }
        if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid)
            return 7;
        socket.waitForReadyRead(250);
    }
    return 8;
}

} // namespace

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("PrintFlowPrinterService"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1"));

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() == 3 &&
        arguments.at(1) == QStringLiteral("--nocai-print-worker")) {
        return NocaiDirectPrintClient::runSerializedPrintWorker(arguments.at(2));
    }

    QString socketName = PrintFlowPrinterServiceProtocol::defaultSocketName();
    QString controlCommand;
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments.at(index) == QStringLiteral("--socket") &&
            index + 1 < arguments.size()) {
            socketName = arguments.at(++index);
        } else if (arguments.at(index) == QStringLiteral("--shutdown")) {
            controlCommand = QStringLiteral("shutdown");
        } else if (arguments.at(index) == QStringLiteral("--ping")) {
            controlCommand = QStringLiteral("ping");
        }
    }
    if (!controlCommand.isEmpty())
        return sendControlCommand(socketName, controlCommand);

    QString runtimeDirectory = QStandardPaths::writableLocation(
        QStandardPaths::RuntimeLocation);
    if (runtimeDirectory.isEmpty()) {
        runtimeDirectory = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
    }
    QDir().mkpath(runtimeDirectory);
    QLockFile serviceLock(QDir(runtimeDirectory).absoluteFilePath(
        QStringLiteral("printflow-printer-service.lock")));
    serviceLock.setStaleLockTime(0);
    if (!serviceLock.tryLock())
        return 0;

    installTerminationHandlers();
    QTimer terminationPoll;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app]() {
        if (g_terminationRequested != 0)
            app.quit();
    });
    terminationPoll.start(100);

    PrinterServiceServer server;
    QString error;
    if (!server.listen(socketName, &error)) {
        qCritical("PrintFlow printer service could not listen: %s",
                  qPrintable(error));
        return 2;
    }
    qInfo("PrintFlow printer service listening on %s",
          qPrintable(server.socketName()));
    return app.exec();
}
