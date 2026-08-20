#include "PrinterServiceProtocol.h"
#include "PrinterServiceServer.h"
#include "RasterSpool.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QtTest>

#include <array>

class PrinterServiceUploadTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsBadOffsetsTruncationAndChecksums();

private:
    static QVariantMap exchange(quint16 port, const QVariantMap& request);
};

QVariantMap PrinterServiceUploadTest::exchange(
    quint16 port, const QVariantMap& request)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    QElapsedTimer timeout;
    timeout.start();
    while (socket.state() != QAbstractSocket::ConnectedState &&
           timeout.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.state() != QAbstractSocket::ConnectedState)
        return {};

    const QByteArray frame = PrintFlowPrinterServiceProtocol::encodeFrame(request);
    if (socket.write(frame) != frame.size())
        return {};
    socket.flush();

    QByteArray responseBytes;
    while (timeout.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        responseBytes.append(socket.readAll());
        QVariantMap response;
        QString error;
        const auto status = PrintFlowPrinterServiceProtocol::takeFrame(
            responseBytes, &response, &error);
        if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Complete)
            return response;
        if (status == PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid)
            return {};
    }
    return {};
}

void PrinterServiceUploadTest::rejectsBadOffsetsTruncationAndChecksums()
{
    PrinterServiceServer server;
    QString error;
    QVERIFY2(server.listenTcp(QHostAddress::LocalHost, 0, &error), qPrintable(error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DirectPrintSpool metadata;
    metadata.logicalChannelCount = 1;
    metadata.channelOrder = {0};
    metadata.width = 16;
    metadata.height = 2;
    metadata.xdpi = 720;
    metadata.ydpi = 1440;
    metadata.bytesPerLine = 4;
    metadata.format = DirectPrintRasterFormat::NocaiX33Standard;

    PrintFlowRasterSpool::Writer writer;
    QVERIFY2(writer.create(directory.path(), metadata, &error), qPrintable(error));
    const std::array<uint8_t, 4> first{{1, 2, 3, 4}};
    const std::array<uint8_t, 4> second{{5, 6, 7, 8}};
    QVERIFY(writer.writeLine(0, 0, first.data(), first.size(), &error));
    QVERIFY(writer.writeLine(0, 1, second.data(), second.size(), &error));
    DirectPrintSpool spool;
    QVERIFY2(writer.finalize(&spool, &error), qPrintable(error));

    QFile spoolFile(spool.path);
    QVERIFY(spoolFile.open(QIODevice::ReadOnly));
    const QByteArray completeFile = spoolFile.readAll();
    spoolFile.close();
    const QVariantMap beginArguments{
        {QStringLiteral("spool"),
         PrintFlowPrinterServiceProtocol::spoolMetadata(spool)},
        {QStringLiteral("settings"),
         PrintFlowPrinterServiceProtocol::settingsToMap({})},
        {QStringLiteral("fileBytes"), qulonglong(completeFile.size())},
    };
    const auto beginUpload = [&]() {
        return exchange(server.tcpPort(), {
            {QStringLiteral("command"), QStringLiteral("beginRasterUpload")},
            {QStringLiteral("arguments"), beginArguments},
        });
    };
    const auto append = [&](const QString& uploadId, quint64 offset,
                            const QByteArray& bytes) {
        return exchange(server.tcpPort(), {
            {QStringLiteral("command"), QStringLiteral("appendRasterChunk")},
            {QStringLiteral("arguments"), QVariantMap{
                {QStringLiteral("uploadId"), uploadId},
                {QStringLiteral("offset"), qulonglong(offset)}}},
            {QStringLiteral("payload"), bytes},
        });
    };
    const auto commit = [&](const QString& uploadId) {
        return exchange(server.tcpPort(), {
            {QStringLiteral("command"), QStringLiteral("commitRasterUpload")},
            {QStringLiteral("arguments"), QVariantMap{
                {QStringLiteral("uploadId"), uploadId}}},
        });
    };

    QVariantMap response = beginUpload();
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QString uploadId = response.value(QStringLiteral("result")).toMap()
                           .value(QStringLiteral("uploadId")).toString();
    QVERIFY(!uploadId.isEmpty());
    response = append(uploadId, 1, completeFile.left(16));
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QVERIFY(response.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("offset")));
    response = exchange(server.tcpPort(), {
        {QStringLiteral("command"), QStringLiteral("cancelRasterUpload")},
        {QStringLiteral("arguments"), QVariantMap{
            {QStringLiteral("uploadId"), uploadId}}},
    });
    QVERIFY(response.value(QStringLiteral("ok")).toBool());

    response = beginUpload();
    uploadId = response.value(QStringLiteral("result")).toMap()
                   .value(QStringLiteral("uploadId")).toString();
    QVERIFY(append(uploadId, 0, completeFile.left(100))
                .value(QStringLiteral("ok")).toBool());
    response = commit(uploadId);
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QVERIFY(response.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("incomplete")));

    QByteArray corruptFile = completeFile;
    corruptFile[qsizetype(spool.bodyOffset)] =
        char(corruptFile[qsizetype(spool.bodyOffset)] ^ 0x40);
    response = beginUpload();
    uploadId = response.value(QStringLiteral("result")).toMap()
                   .value(QStringLiteral("uploadId")).toString();
    QVERIFY(append(uploadId, 0, corruptFile)
                .value(QStringLiteral("ok")).toBool());
    response = commit(uploadId);
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QVERIFY(response.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("checksum")));

    PrintFlowRasterSpool::remove(spool);
}

QTEST_GUILESS_MAIN(PrinterServiceUploadTest)
#include "PrinterServiceUploadTest.moc"
