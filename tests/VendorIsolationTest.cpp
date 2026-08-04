#include "NocaiDirectPrintClient.h"
#include "NocaiPrnWriter.h"

#include <QtTest/QtTest>
#include <QFileInfo>
#include <QTemporaryDir>

class VendorIsolationTest : public QObject
{
    Q_OBJECT

private slots:
    void directPrintUnavailablePathFailsCleanly();
    void missingSdkEnvironmentIsNotRequired();
    void mangledCompatibilitySymbolsAreSupported();
    void configuredVendorSdkLoadsWhenPresent();
};

void VendorIsolationTest::directPrintUnavailablePathFailsCleanly()
{
    QTemporaryDir emptySdkRoot;
    QVERIFY(emptySdkRoot.isValid());

    NocaiDirectPrintClient client;
    client.setSdkRootPath(emptySdkRoot.path());
    client.setAutoDiscoverSdk(false);

    IPrintOutputClient* outputClient = &client;
    QVERIFY(!outputClient->vendorName().isEmpty());
    QVERIFY(!outputClient->isAvailable());

    DirectPrintRaster raster;
    DirectPrintSettings settings;
    QVERIFY(!outputClient->submitPreparedJob(raster, settings));
    QVERIFY(!outputClient->lastError().isEmpty());
    QVERIFY(client.supportsMaintenance(QStringLiteral("X-33")));
    QVERIFY(!client.supportsMaintenance(QStringLiteral("X-36NC (Photo Printer)")));
}

void VendorIsolationTest::missingSdkEnvironmentIsNotRequired()
{
    qunsetenv("DIRECT_PRINT_SDK_ROOT");

    NocaiDirectPrintClient client;
    QVERIFY(!client.vendorName().isEmpty());
    QVERIFY(!client.isAvailable());
    QVERIFY(!client.lastError().isEmpty());
}

void VendorIsolationTest::mangledCompatibilitySymbolsAreSupported()
{
    const QFileInfo fakeSdk(QStringLiteral(PRINTFLOW_FAKE_MANGLED_SDK_PATH));
    QVERIFY2(fakeSdk.exists(), qPrintable(fakeSdk.absoluteFilePath()));

    NocaiDirectPrintClient client;
    client.setSdkRootPath(fakeSdk.absolutePath());

    QVERIFY2(client.isAvailable(), qPrintable(client.lastError()));
    QVERIFY(client.refreshPrinters());
    QCOMPARE(client.printers().size(), 1);
    QCOMPARE(client.printers().first().toMap().value("name").toString(),
             QStringLiteral("Fake x64 SDK Printer"));
    // The application may connect immediately after a one-printer search.
    // The client must mirror the vendor demo's Search -> Select -> Connect order.
    QVERIFY2(client.connectPrinter(), qPrintable(client.lastError()));
    QVERIFY(client.isConnected());
    QVERIFY2(client.printNozzleCheck(), qPrintable(client.lastError()));

    std::vector<std::vector<std::vector<uint8_t>>> packedLines = {
        {{0x01, 0x01, 0x01, 0x01}},
        {{0x02, 0x02, 0x02, 0x02}},
        {{0x03, 0x03, 0x03, 0x03}},
        {{0x04, 0x04, 0x04, 0x04}}
    };
    DirectPrintRaster raster;
    raster.packedLines = &packedLines;
    raster.channelOrder = {2, 1, 0, 3};
    raster.width = 16;
    raster.height = 1;
    raster.xdpi = 720;
    raster.ydpi = 720;
    raster.bytesPerLine = 4;
    raster.format = DirectPrintRasterFormat::NocaiX33Standard;
    raster.canonicalHeader = NocaiPrnWriter::makeStandardCmykHeader(
        raster.width, raster.height, raster.xdpi, raster.ydpi, raster.bytesPerLine);

    DirectPrintSettings settings;
    settings.printerIndex = 0;
    // Reproduce a persisted bidirectional selection. The x64 SDK's legacy
    // X-33 path must override this to its proven-safe left-to-right mode.
    settings.printDirection = 0;
    // CPrinter_Model_X33's generic two-head configuration maps to selection 0.
    settings.headSelect = 0;
    settings.mediaHeightMm = 5.5;
    settings.printOffsetXmm = 12;
    settings.printOffsetYmm = 34;
    // The X-33 canonical header must win over any independently persisted
    // direct-print pass setting.
    settings.pass = 9;
    QVERIFY2(client.submitPreparedJob(raster, settings), qPrintable(client.lastError()));

    packedLines.push_back({{0x05, 0x05, 0x05, 0x05}});
    raster.channelOrder = {2, 1, 0, 3, 4, 4};
    raster.canonicalHeader = NocaiPrnWriter::makeStandardX33Header(
        raster.width,
        raster.height,
        raster.xdpi,
        raster.ydpi,
        raster.bytesPerLine,
        static_cast<int>(raster.channelOrder.size()));
    settings.wcSequence = 1;
    settings.whiteInkPercent = 3;
    settings.whiteInkPassCount = 2;
    QVERIFY2(client.submitPreparedJob(raster, settings), qPrintable(client.lastError()));
}

void VendorIsolationTest::configuredVendorSdkLoadsWhenPresent()
{
#if defined(PRINTFLOW_CONFIGURED_SDK_ROOT)
    NocaiDirectPrintClient client;
    client.setSdkRootPath(QStringLiteral(PRINTFLOW_CONFIGURED_SDK_ROOT));
    QVERIFY2(client.isAvailable(), qPrintable(client.lastError()));
#else
    QSKIP("No architecture-compatible vendor SDK was configured.");
#endif
}

QTEST_GUILESS_MAIN(VendorIsolationTest)
#include "VendorIsolationTest.moc"
