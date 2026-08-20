#include "PrinterServiceProtocol.h"

#include <QDataStream>
#include <QtTest>

class PrinterServiceProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void frameRoundTrip();
    void incompleteFrameIsRetained();
    void invalidVersionIsRejected();
    void settingsRoundTrip();
    void rasterRoundTrip();
    void malformedRasterIsRejected();
    void spoolMetadataRoundTrip();
    void malformedSpoolMetadataIsRejected();
    void bridgeAddressIsLoopbackOnly();
};

void PrinterServiceProtocolTest::frameRoundTrip()
{
    const QVariantMap original{
        {QStringLiteral("command"), QStringLiteral("ping")},
        {QStringLiteral("sequence"), 42},
        {QStringLiteral("payload"), QByteArray("binary\0data", 11)},
    };
    QByteArray buffer = PrintFlowPrinterServiceProtocol::encodeFrame(original);
    QVERIFY(!buffer.isEmpty());

    QVariantMap decoded;
    QString error;
    QCOMPARE(PrintFlowPrinterServiceProtocol::takeFrame(
                 buffer, &decoded, &error),
             PrintFlowPrinterServiceProtocol::DecodeStatus::Complete);
    QCOMPARE(decoded, original);
    QVERIFY(buffer.isEmpty());
    QVERIFY(error.isEmpty());
}

void PrinterServiceProtocolTest::incompleteFrameIsRetained()
{
    const QByteArray complete = PrintFlowPrinterServiceProtocol::encodeFrame(
        {{QStringLiteral("command"), QStringLiteral("ping")}});
    QByteArray partial = complete.left(complete.size() - 1);
    const QByteArray before = partial;
    QVariantMap decoded;

    QCOMPARE(PrintFlowPrinterServiceProtocol::takeFrame(partial, &decoded),
             PrintFlowPrinterServiceProtocol::DecodeStatus::Incomplete);
    QCOMPARE(partial, before);
}

void PrinterServiceProtocolTest::invalidVersionIsRejected()
{
    QByteArray body;
    QDataStream bodyStream(&body, QIODevice::WriteOnly);
    bodyStream.setVersion(QDataStream::Qt_6_0);
    bodyStream << quint32(PrintFlowPrinterServiceProtocol::Magic)
               << quint16(PrintFlowPrinterServiceProtocol::Version + 1)
               << QVariantMap{{QStringLiteral("command"),
                               QStringLiteral("ping")}};

    QByteArray frame;
    QDataStream frameStream(&frame, QIODevice::WriteOnly);
    frameStream.setVersion(QDataStream::Qt_6_0);
    frameStream << quint32(body.size());
    frame.append(body);

    QVariantMap decoded;
    QString error;
    QCOMPARE(PrintFlowPrinterServiceProtocol::takeFrame(
                 frame, &decoded, &error),
             PrintFlowPrinterServiceProtocol::DecodeStatus::Invalid);
    QVERIFY(error.contains(QStringLiteral("unsupported")));
}

void PrinterServiceProtocolTest::settingsRoundTrip()
{
    DirectPrintSettings original;
    original.printerIndex = 3;
    original.printDirection = 1;
    original.printSpeed = 2;
    original.whiteInkPercent = 67;
    original.mediaHeightMm = 432.5;
    original.printOffsetXmm = 12;
    original.printOffsetYmm = 34;
    original.pass = 8;
    original.vsdMode = 2;

    DirectPrintSettings decoded;
    QString error;
    QVERIFY(PrintFlowPrinterServiceProtocol::settingsFromMap(
        PrintFlowPrinterServiceProtocol::settingsToMap(original),
        &decoded, &error));
    QCOMPARE(decoded.printerIndex, original.printerIndex);
    QCOMPARE(decoded.printDirection, original.printDirection);
    QCOMPARE(decoded.printSpeed, original.printSpeed);
    QCOMPARE(decoded.whiteInkPercent, original.whiteInkPercent);
    QCOMPARE(decoded.mediaHeightMm, original.mediaHeightMm);
    QCOMPARE(decoded.printOffsetXmm, original.printOffsetXmm);
    QCOMPARE(decoded.printOffsetYmm, original.printOffsetYmm);
    QCOMPARE(decoded.pass, original.pass);
    QCOMPARE(decoded.vsdMode, original.vsdMode);
    QVERIFY(error.isEmpty());
}

void PrinterServiceProtocolTest::rasterRoundTrip()
{
    std::vector<std::vector<std::vector<uint8_t>>> source(3);
    for (auto& channel : source)
        channel.resize(2);
    source[0][0] = {0x01, 0x02};
    source[2][0] = {0x21, 0x22};
    source[0][1] = {0x03, 0x04};
    source[2][1] = {0x23, 0x24};

    DirectPrintRaster original;
    original.packedLines = &source;
    original.channelOrder = {0, 2};
    original.width = 16;
    original.height = 2;
    original.xdpi = 720;
    original.ydpi = 1440;
    original.bytesPerLine = 2;
    original.format = DirectPrintRasterFormat::NocaiX33Standard;
    for (size_t index = 0; index < original.canonicalHeader.size(); ++index)
        original.canonicalHeader[index] = uint32_t(index * 11);

    QByteArray payload;
    QString error;
    QVERIFY(PrintFlowPrinterServiceProtocol::serializeRaster(
        original, &payload, &error));
    QCOMPARE(payload.toHex(), QByteArray("0102212203042324"));

    std::vector<std::vector<std::vector<uint8_t>>> decodedStorage;
    DirectPrintRaster decoded;
    QVERIFY(PrintFlowPrinterServiceProtocol::deserializeRaster(
        PrintFlowPrinterServiceProtocol::rasterMetadata(original), payload,
        &decodedStorage, &decoded, &error));
    QCOMPARE(decoded.width, original.width);
    QCOMPARE(decoded.height, original.height);
    QCOMPARE(decoded.xdpi, original.xdpi);
    QCOMPARE(decoded.ydpi, original.ydpi);
    QCOMPARE(decoded.bytesPerLine, original.bytesPerLine);
    QVERIFY(decoded.channelOrder == original.channelOrder);
    QVERIFY(decoded.canonicalHeader == original.canonicalHeader);
    QVERIFY(decodedStorage[0][0] == source[0][0]);
    QVERIFY(decodedStorage[2][1] == source[2][1]);
    QCOMPARE(decoded.packedLines, &decodedStorage);
    QVERIFY(error.isEmpty());
}

void PrinterServiceProtocolTest::malformedRasterIsRejected()
{
    QVariantMap metadata{
        {QStringLiteral("width"), 16},
        {QStringLiteral("height"), 2},
        {QStringLiteral("xdpi"), 720},
        {QStringLiteral("ydpi"), 1440},
        {QStringLiteral("bytesPerLine"), 2},
        {QStringLiteral("format"),
         int(DirectPrintRasterFormat::NocaiX33Standard)},
        {QStringLiteral("channels"), QVariantList{0, 1}},
        {QStringLiteral("canonicalHeader"), QVariantList(12, 0)},
    };
    std::vector<std::vector<std::vector<uint8_t>>> storage;
    DirectPrintRaster raster;
    QString error;
    QVERIFY(!PrintFlowPrinterServiceProtocol::deserializeRaster(
        metadata, QByteArray(7, '\0'), &storage, &raster, &error));
    QVERIFY(error.contains(QStringLiteral("length")));
}

void PrinterServiceProtocolTest::spoolMetadataRoundTrip()
{
    DirectPrintSpool original;
    original.channelOrder = {2, 1, 0, 3, 4, 4};
    original.logicalChannelCount = 5;
    original.width = 11854;
    original.height = 12274;
    original.xdpi = 720;
    original.ydpi = 1440;
    original.bytesPerLine = 2964;
    original.format = DirectPrintRasterFormat::NocaiX33Standard;
    original.bodyOffset = 4096;
    original.bodyBytes = quint64(original.logicalChannelCount) *
        quint64(original.height) * quint64(original.bytesPerLine);
    original.sha256 = QByteArray(32, char(0xa5));
    for (size_t index = 0; index < original.canonicalHeader.size(); ++index)
        original.canonicalHeader[index] = uint32_t(index * 101);

    DirectPrintSpool decoded;
    QString error;
    QVERIFY(PrintFlowPrinterServiceProtocol::spoolMetadataFromMap(
        PrintFlowPrinterServiceProtocol::spoolMetadata(original),
        &decoded, &error));
    QCOMPARE(decoded.channelOrder, original.channelOrder);
    QCOMPARE(decoded.logicalChannelCount, original.logicalChannelCount);
    QCOMPARE(decoded.width, original.width);
    QCOMPARE(decoded.height, original.height);
    QCOMPARE(decoded.xdpi, original.xdpi);
    QCOMPARE(decoded.ydpi, original.ydpi);
    QCOMPARE(decoded.bytesPerLine, original.bytesPerLine);
    QCOMPARE(decoded.format, original.format);
    QCOMPARE(decoded.canonicalHeader, original.canonicalHeader);
    QCOMPARE(decoded.bodyOffset, original.bodyOffset);
    QCOMPARE(decoded.bodyBytes, original.bodyBytes);
    QCOMPARE(decoded.sha256, original.sha256);
    QVERIFY(error.isEmpty());
}

void PrinterServiceProtocolTest::malformedSpoolMetadataIsRejected()
{
    DirectPrintSpool source;
    source.channelOrder = {0};
    source.logicalChannelCount = 1;
    source.width = 16;
    source.height = 2;
    source.xdpi = 720;
    source.ydpi = 1440;
    source.bytesPerLine = 4;
    source.bodyOffset = 4096;
    source.bodyBytes = 8;
    source.sha256 = QByteArray(32, char(0x11));

    DirectPrintSpool decoded;
    QString error;
    QVariantMap badChecksum = PrintFlowPrinterServiceProtocol::spoolMetadata(source);
    badChecksum.insert(QStringLiteral("sha256"), QByteArray(31, char(0x11)));
    QVERIFY(!PrintFlowPrinterServiceProtocol::spoolMetadataFromMap(
        badChecksum, &decoded, &error));
    QVERIFY(error.contains(QStringLiteral("incomplete")));

    error.clear();
    QVariantMap badChannel = PrintFlowPrinterServiceProtocol::spoolMetadata(source);
    badChannel.insert(QStringLiteral("channels"), QVariantList{3});
    QVERIFY(!PrintFlowPrinterServiceProtocol::spoolMetadataFromMap(
        badChannel, &decoded, &error));
    QVERIFY(error.contains(QStringLiteral("channel order")));
}

void PrinterServiceProtocolTest::bridgeAddressIsLoopbackOnly()
{
    QVERIFY(PrintFlowPrinterServiceProtocol::isAllowedBridgeAddress(
        QHostAddress::LocalHost));
    QVERIFY(PrintFlowPrinterServiceProtocol::isAllowedBridgeAddress(
        QHostAddress::LocalHostIPv6));
    QVERIFY(!PrintFlowPrinterServiceProtocol::isAllowedBridgeAddress(
        QHostAddress::AnyIPv4));
    QVERIFY(!PrintFlowPrinterServiceProtocol::isAllowedBridgeAddress(
        QHostAddress(QStringLiteral("192.0.2.10"))));
}

QTEST_MAIN(PrinterServiceProtocolTest)
#include "PrinterServiceProtocolTest.moc"
