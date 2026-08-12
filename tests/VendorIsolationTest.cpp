#include "NocaiDirectPrintClient.h"
#include "NocaiDirectPrintCompatibility.h"
#include "NocaiPrnWriter.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdint>
#include <cstring>
#include <array>

#if defined(__linux__) && defined(__aarch64__)
#include <sys/socket.h>
#include <unistd.h>
#endif

class VendorIsolationTest : public QObject
{
    Q_OBJECT

private slots:
    void directPrintUnavailablePathFailsCleanly();
    void missingSdkEnvironmentIsNotRequired();
    void mangledCompatibilitySymbolsAreSupported();
    void configuredVendorSdkLoadsWhenPresent();
    void armCommandTagCompatibilityCorrectsWireCopy();
    void armRealSdkConnectionProbe();
    void maintenanceActionsCompleteAsynchronously();
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
#if !defined(__linux__) || !defined(__x86_64__)
    QSKIP("The vendor's mangled-symbol compatibility shim is Linux x86-64 only.");
#else
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
    // Model an origin left behind by an interrupted older client. Nozzle
    // checks must always reconcile that persistent controller state to 0,0.
    QVERIFY2(client.setPrintXYValue(12, 34), qPrintable(client.lastError()));
    QVERIFY2(client.printNozzleCheck(), qPrintable(client.lastError()));
    QVariantMap nozzleOrigin = client.getPrintXYValue();
    QVERIFY2(nozzleOrigin.value("ok").toBool(), qPrintable(client.lastError()));
    QCOMPARE(nozzleOrigin.value("xRawHundredthsMm").toULongLong(), 0ULL);
    QCOMPARE(nozzleOrigin.value("yRawHundredthsMm").toULongLong(), 0ULL);

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

    // Exercise the worker entry condition: discovery has completed, but this
    // client has not connected yet. printPackedJob must route through the same
    // public Choose -> Connect implementation used by Printer Setup.
    NocaiDirectPrintClient printClient;
    printClient.setSdkRootPath(fakeSdk.absolutePath());
    QVERIFY2(printClient.isAvailable(), qPrintable(printClient.lastError()));
    QVERIFY2(printClient.refreshPrinters(), qPrintable(printClient.lastError()));
    QVERIFY(!printClient.isConnected());
    QVERIFY2(printClient.submitPreparedJob(raster, settings),
             qPrintable(printClient.lastError()));
    QVERIFY(printClient.isConnected());
    QVariantMap restoredOrigin = printClient.getPrintXYValue();
    QVERIFY2(restoredOrigin.value("ok").toBool(),
             qPrintable(printClient.lastError()));
    QCOMPARE(restoredOrigin.value("xRawHundredthsMm").toULongLong(), 0ULL);
    QCOMPARE(restoredOrigin.value("yRawHundredthsMm").toULongLong(), 0ULL);

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
    QVERIFY2(printClient.submitPreparedJob(raster, settings),
             qPrintable(printClient.lastError()));
    restoredOrigin = printClient.getPrintXYValue();
    QVERIFY2(restoredOrigin.value("ok").toBool(),
             qPrintable(printClient.lastError()));
    QCOMPARE(restoredOrigin.value("xRawHundredthsMm").toULongLong(), 0ULL);
    QCOMPARE(restoredOrigin.value("yRawHundredthsMm").toULongLong(), 0ULL);
#endif
}

void VendorIsolationTest::configuredVendorSdkLoadsWhenPresent()
{
#if defined(PRINTFLOW_CONFIGURED_SDK_ROOT)
    NocaiDirectPrintClient client;
    client.setSdkRootPath(QStringLiteral(PRINTFLOW_CONFIGURED_SDK_ROOT));
    QVERIFY2(client.isAvailable(), qPrintable(client.lastError()));
#if defined(__linux__) && defined(__aarch64__)
    QVERIFY2(NocaiDirectPrintCompatibility::isInstalledFor(&client),
             "The real ARM SDK loaded without its required host print callbacks.");
#endif
#else
    QSKIP("No architecture-compatible vendor SDK was configured.");
#endif
}

void VendorIsolationTest::armCommandTagCompatibilityCorrectsWireCopy()
{
#if !defined(__linux__) || !defined(__aarch64__) || \
    !defined(PRINTFLOW_CONFIGURED_SDK_ROOT)
    QSKIP("The validated real ARM64 vendor SDK is required.");
#else
    NocaiDirectPrintClient client;
    client.setSdkRootPath(QStringLiteral(PRINTFLOW_CONFIGURED_SDK_ROOT));
    QVERIFY2(client.isAvailable(), qPrintable(client.lastError()));

    int sockets[2] = {-1, -1};
    QVERIFY(::socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);

    std::array<unsigned char, 30> packet{};
    packet[0] = 0x45;
    packet[9] = 233;
    packet[28] = 3;
    packet[29] = 0x49;

    QCOMPARE(::sendto(sockets[0], packet.data(), packet.size(), 0, nullptr, 0),
             static_cast<ssize_t>(packet.size()));
    std::array<unsigned char, 30> received{};
    QCOMPARE(::recv(sockets[1], received.data(), received.size(), 0),
             static_cast<ssize_t>(received.size()));
    QCOMPARE(packet[29], static_cast<unsigned char>(0x49));
    QCOMPARE(received[29], static_cast<unsigned char>(0x47));

    std::array<unsigned char, 26> acknowledgement{};
    acknowledgement[0] = 0x00;
    acknowledgement[1] = 0x19;
    acknowledgement[3] = 0x58;
    acknowledgement[8] = 0xa1;
    acknowledgement[9] = 0x49;

    QCOMPARE(::send(sockets[0], acknowledgement.data(),
                    acknowledgement.size(), 0),
             static_cast<ssize_t>(acknowledgement.size()));
    received.fill(0);
    QCOMPARE(::recv(sockets[1], received.data(), acknowledgement.size(), 0),
             static_cast<ssize_t>(acknowledgement.size()));
    QCOMPARE(acknowledgement[9], static_cast<unsigned char>(0x49));
    QCOMPARE(received[9], static_cast<unsigned char>(0x47));

    ::close(sockets[0]);
    ::close(sockets[1]);
#endif
}

void VendorIsolationTest::maintenanceActionsCompleteAsynchronously()
{
    NocaiDirectPrintClient client;
    QSignalSpy completed(&client, &NocaiDirectPrintClient::maintenanceActionFinished);
    QVERIFY(client.startMaintenanceAction(QStringLiteral("UnknownTestAction")));
    if (completed.isEmpty())
        QVERIFY(completed.wait(5000));
    QCOMPARE(completed.size(), 1);
    const QList<QVariant> result = completed.takeFirst();
    QCOMPARE(result.at(0).toString(), QStringLiteral("UnknownTestAction"));
    QVERIFY(!result.at(1).toBool());
    QVERIFY(!result.at(3).toString().isEmpty());
    QVERIFY(!client.maintenanceBusy());
}

void VendorIsolationTest::armRealSdkConnectionProbe()
{
#if !defined(__linux__) || !defined(__aarch64__) || \
    !defined(PRINTFLOW_CONFIGURED_SDK_ROOT)
    QSKIP("The validated real ARM64 vendor SDK is required.");
#else
    if (qEnvironmentVariableIntValue("PRINTFLOW_RUN_ARM_CONNECTION_PROBE") != 1)
        QSKIP("Set PRINTFLOW_RUN_ARM_CONNECTION_PROBE=1 for the connection-only hardware probe.");

    const QString sdkRoot = qEnvironmentVariable(
        "PRINTFLOW_ARM_PROBE_SDK_ROOT",
        QStringLiteral(PRINTFLOW_CONFIGURED_SDK_ROOT));
    qInfo() << "ARM connection probe SDK root:" << sdkRoot;

    NocaiDirectPrintClient client;
    client.setSdkRootPath(sdkRoot);
    QVERIFY2(client.isAvailable(), qPrintable(client.lastError()));
    QVERIFY2(client.refreshPrinters(), qPrintable(client.lastError()));

    const QVariantList printers = client.printers();
    qInfo() << "ARM connection probe found" << printers.size()
            << "printer(s):" << printers;
    QCOMPARE(printers.size(), 1);

    const int printerIndex = printers.first().toMap().value("index").toInt();
    QVERIFY2(client.choosePrinter(printerIndex), qPrintable(client.lastError()));
    QVERIFY2(client.connectPrinter(), qPrintable(client.lastError()));
    QVERIFY(client.isConnected());

    const QVariantMap status = client.getPrinterStatus();
    const QVariantMap info = client.getPrinterInfo();
    qInfo() << "ARM connection probe post-connect status:" << status;
    qInfo() << "ARM connection probe post-connect info:" << info;
    qInfo() << "ARM connection probe post-connect last error:"
            << client.lastError();

    // ARM ConnectPrinter is controller initialization rather than a
    // per-operation socket. Prove that the public refresh/connect operations
    // are idempotent and retain the one process-owned session instead of
    // attempting an unsafe GUI-to-worker handoff.
    QVERIFY2(client.refreshPrinters(), qPrintable(client.lastError()));
    QCOMPARE(client.printers().size(), 1);
    QVERIFY2(client.connectPrinter(), qPrintable(client.lastError()));
    QVERIFY(client.isConnected());
    qInfo() << "ARM connection probe retained one session across redundant refresh/connect calls.";
#endif
}

QTEST_GUILESS_MAIN(VendorIsolationTest)
#include "VendorIsolationTest.moc"
