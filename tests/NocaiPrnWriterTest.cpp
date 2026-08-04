#include "NocaiPrnWriter.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <QUrl>

#include <cstring>

namespace {
uint32_t readU32(const QByteArray& data, int offset)
{
    uint32_t value = 0;
    std::memcpy(&value, data.constData() + offset, sizeof(value));
    return value;
}

bool readFile(const QString& path, QByteArray& out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    out = file.readAll();
    return true;
}
}

class NocaiPrnWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void packs2BppRows();
    void writesStandardCmykPrn();
    void writesStandardX33WhitePrn();
    void writesMultiInkPrn();
};

void NocaiPrnWriterTest::packs2BppRows()
{
    const std::vector<std::vector<uint8_t>> dotMap = {
        {0, 1, 2, 3, 1}
    };
    const auto packed = NocaiPrnWriter::packTo2Bpp(dotMap, 5, 1);
    QCOMPARE(packed.size(), size_t(1));
    QCOMPARE(packed[0].size(), size_t(4));
    QCOMPARE(packed[0][0], uint8_t(0x1b));
    QCOMPARE(packed[0][1], uint8_t(0x40));
    QCOMPARE(packed[0][2], uint8_t(0x00));
    QCOMPARE(packed[0][3], uint8_t(0x00));
}

void NocaiPrnWriterTest::writesStandardCmykPrn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    std::vector<std::vector<std::vector<uint8_t>>> channels(4);
    for (int ch = 0; ch < 4; ++ch)
        channels[ch] = {std::vector<uint8_t>(4, static_cast<uint8_t>(ch + 1))};
    const std::vector<int> channelOrder = {2, 1, 0, 3};

    const QString standardPath = tempDir.filePath(QStringLiteral("standard.prn"));
    if (!NocaiPrnWriter::writeStandardCmykPrn(
            channels,
            channelOrder,
            4,
            1,
            720,
            720,
            QUrl::fromLocalFile(standardPath).toString()));

    QByteArray standardData;
    QVERIFY(readFile(standardPath, standardData));
    QCOMPARE(standardData.size(), 64);
    QCOMPARE(readU32(standardData, 0), 0x00005555u);
    QCOMPARE(readU32(standardData, 4), 720u);
    QCOMPARE(readU32(standardData, 8), 720u);
    QCOMPARE(readU32(standardData, 12), 4u);
    QCOMPARE(readU32(standardData, 16), 1u);
    QCOMPARE(readU32(standardData, 20), 4u);
    QCOMPARE(readU32(standardData, 24), 0u);
    QCOMPARE(readU32(standardData, 28), 4u);
    QCOMPARE(readU32(standardData, 32), 1u);
    QCOMPARE(readU32(standardData, 36), 1u);
    QCOMPARE(readU32(standardData, 40), 0u);
    QCOMPARE(readU32(standardData, 44), 0u);
    QCOMPARE(static_cast<unsigned char>(standardData[48]), 3);
    QCOMPARE(static_cast<unsigned char>(standardData[52]), 2);
    QCOMPARE(static_cast<unsigned char>(standardData[56]), 1);
    QCOMPARE(static_cast<unsigned char>(standardData[60]), 4);
}

void NocaiPrnWriterTest::writesStandardX33WhitePrn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Five logical rasters are supplied. The X-33 physical order references
    // logical W twice so the serialized row is YMCKWW.
    std::vector<std::vector<std::vector<uint8_t>>> channels(5);
    for (int channel = 0; channel < 5; ++channel) {
        channels[channel] = {
            std::vector<uint8_t>(4, static_cast<uint8_t>(channel + 1))
        };
    }
    const std::vector<int> channelOrder = {2, 1, 0, 3, 4, 4};

    const QString path = tempDir.filePath(QStringLiteral("x33-white.prn"));
    QVERIFY(NocaiPrnWriter::writeStandardX33Prn(
        channels,
        channelOrder,
        4,
        1,
        720,
        1440,
        QUrl::fromLocalFile(path).toString()));

    QByteArray data;
    QVERIFY(readFile(path, data));
    QCOMPARE(data.size(), 72);
    QCOMPARE(readU32(data, 0), 0x00005555u);
    QCOMPARE(readU32(data, 4), 720u);
    QCOMPARE(readU32(data, 8), 1440u);
    QCOMPARE(readU32(data, 12), 4u);
    QCOMPARE(readU32(data, 16), 1u);
    QCOMPARE(readU32(data, 20), 4u);
    QCOMPARE(readU32(data, 28), 6u);
    QCOMPARE(readU32(data, 32), 1u);
    QCOMPARE(readU32(data, 36), 1u);
    QCOMPARE(static_cast<unsigned char>(data[48]), 3); // Y
    QCOMPARE(static_cast<unsigned char>(data[52]), 2); // M
    QCOMPARE(static_cast<unsigned char>(data[56]), 1); // C
    QCOMPARE(static_cast<unsigned char>(data[60]), 4); // K
    QCOMPARE(static_cast<unsigned char>(data[64]), 5); // W
    QCOMPARE(static_cast<unsigned char>(data[68]), 5); // W
}

void NocaiPrnWriterTest::writesMultiInkPrn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    std::vector<std::vector<std::vector<uint8_t>>> channels(4);
    for (int ch = 0; ch < 4; ++ch)
        channels[ch] = {std::vector<uint8_t>(4, static_cast<uint8_t>(ch + 1))};
    const std::vector<int> channelOrder = {2, 1, 0, 3};

    const QString multiInkPath = tempDir.filePath(QStringLiteral("multiink.prn"));
    QVERIFY(NocaiPrnWriter::writeMultiInkPrn(
            channels,
            channelOrder,
            NocaiPrnWriter::MultiInkMode::FourColorYMCK,
            4,
            1,
            720,
            600,
            4,
            QUrl::fromLocalFile(multiInkPath).toString()));

    QByteArray multiInkData;
    QVERIFY(readFile(multiInkPath, multiInkData));
    QCOMPARE(multiInkData.size(), 96);
    QCOMPARE(QByteArray(multiInkData.constData(), 6), QByteArray("inkjet", 6));
    QCOMPARE(static_cast<unsigned char>(multiInkData[48]), 2);
    QCOMPARE(static_cast<unsigned char>(multiInkData[49]), 4);
    QCOMPARE(static_cast<unsigned char>(multiInkData[64]), static_cast<unsigned char>('Y'));
    QCOMPARE(static_cast<unsigned char>(multiInkData[65]), static_cast<unsigned char>('M'));
    QCOMPARE(static_cast<unsigned char>(multiInkData[66]), static_cast<unsigned char>('C'));
    QCOMPARE(static_cast<unsigned char>(multiInkData[67]), static_cast<unsigned char>('K'));
}

QTEST_GUILESS_MAIN(NocaiPrnWriterTest)
#include "NocaiPrnWriterTest.moc"
