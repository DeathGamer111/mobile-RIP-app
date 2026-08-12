#include <IPrintOutputClient.h>
#include <PrinterServiceClient.h>
#include <PrinterServiceProtocol.h>

#include <QtTest>

#include <type_traits>

class PrinterApiSurfaceTest : public QObject
{
    Q_OBJECT

private slots:
    void publicTypesAndLinkage();
};

void PrinterApiSurfaceTest::publicTypesAndLinkage()
{
    static_assert(std::is_base_of_v<IPrintOutputClient, PrinterServiceClient>);
    static_assert(PrintFlowPrinterServiceProtocol::Version == 1);

    DirectPrintSettings settings;
    settings.printerIndex = 7;
    const QVariantMap encoded =
        PrintFlowPrinterServiceProtocol::settingsToMap(settings);
    QCOMPARE(encoded.value(QStringLiteral("printerIndex")).toInt(), 7);

    const auto connectMethod = &PrinterServiceClient::connectPrinter;
    QVERIFY(connectMethod != nullptr);
    const auto reconnectMethod = &PrinterServiceClient::startReconnectPrinter;
    const auto restartMethod = &PrinterServiceClient::startRestartService;
    const auto diagnosticMethod = &PrinterServiceClient::diagnosticLog;
    QVERIFY(reconnectMethod != nullptr);
    QVERIFY(restartMethod != nullptr);
    QVERIFY(diagnosticMethod != nullptr);
}

QTEST_GUILESS_MAIN(PrinterApiSurfaceTest)
#include "PrinterApiSurfaceTest.moc"
