#pragma once

#include "IPrintOutputClient.h"
#include "PrintFlowPrinterApiExport.h"

#include <QFutureWatcher>
#include <QObject>
#include <QRecursiveMutex>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class PRINTFLOW_PRINTER_API PrinterServiceClient : public QObject,
                                                    public IPrintOutputClient
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(QString sdkRootPath READ sdkRootPath WRITE setSdkRootPath NOTIFY statusChanged)
    Q_PROPERTY(bool autoDiscoverSdk READ autoDiscoverSdk WRITE setAutoDiscoverSdk NOTIFY statusChanged)
    Q_PROPERTY(QVariantList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QStringList maintenanceSupportedPrinters READ maintenanceSupportedPrinters CONSTANT)
    Q_PROPERTY(bool maintenanceBusy READ maintenanceBusy NOTIFY maintenanceBusyChanged)
    Q_PROPERTY(qint64 servicePid READ servicePid NOTIFY statusChanged)

public:
    explicit PrinterServiceClient(QObject* parent = nullptr);
    ~PrinterServiceClient() override;

    bool isAvailable() override;
    bool isConnected() const;
    QString vendorName() const override;
    QString lastError() const override;
    QString sdkRootPath() const;
    void setSdkRootPath(const QString& path);
    bool autoDiscoverSdk() const;
    void setAutoDiscoverSdk(bool enabled);
    QVariantList printers() const;
    QStringList maintenanceSupportedPrinters() const;
    bool maintenanceBusy() const;
    qint64 servicePid() const;

    Q_INVOKABLE QVariantList searchPrinters();
    Q_INVOKABLE bool supportsMaintenance(const QString& printerName) const;
    Q_INVOKABLE bool refreshPrinters();
    Q_INVOKABLE bool choosePrinter(int index);
    Q_INVOKABLE bool abortPrint();
    Q_INVOKABLE bool pausePrint();
    Q_INVOKABLE bool continuePrint();
    Q_INVOKABLE QString statusText();
    Q_INVOKABLE bool connectPrinter();
    Q_INVOKABLE bool startReconnectPrinter(int printerIndex);
    Q_INVOKABLE bool startRestartService(int printerIndex);
    Q_INVOKABLE QString diagnosticLogPath() const;
    Q_INVOKABLE QString diagnosticLog() const;
    Q_INVOKABLE bool wipePrintHead(int printHeadMask);
    Q_INVOKABLE bool startCleanOperation(int printHeadMask);
    Q_INVOKABLE bool startPump(int printHeadMask);
    Q_INVOKABLE bool stopPumpOperation();
    Q_INVOKABLE bool spitPrintHead(int printHeadMask);
    Q_INVOKABLE bool stopSpitOperation();
    Q_INVOKABLE bool capPrintHead();
    Q_INVOKABLE bool moveAxis(int axis, int direction);
    Q_INVOKABLE QVariantMap stopAxis(int axis);
    Q_INVOKABLE QVariantMap saveAxisPos(int axis);
    Q_INVOKABLE bool setPrintHeight(double heightMm);
    Q_INVOKABLE QVariantMap getPrintHeight();
    Q_INVOKABLE QVariantMap getJobSettings();
    Q_INVOKABLE bool setJobSettingsFromMap(const QVariantMap& settings);
    Q_INVOKABLE bool exportConfigFile(const QString& path);
    Q_INVOKABLE bool importConfigFile(const QString& path);
    Q_INVOKABLE QVariantMap getAlignmentValues();
    Q_INVOKABLE bool setAlignmentValues(const QVariantMap& settings, int type);
    Q_INVOKABLE bool printNozzleCheck();
    Q_INVOKABLE bool printAlignmentPattern(int type);
    Q_INVOKABLE QVariantMap getPrinterStatus();
    Q_INVOKABLE QVariantMap getPrinterInfo();
    Q_INVOKABLE bool setPrintXYValue(int xMm, int yMm);
    Q_INVOKABLE QVariantMap getPrintXYValue();
    Q_INVOKABLE QVariantMap getUVParamValues();
    Q_INVOKABLE bool setUVParamValues(const QVariantMap& settings, int type);
    Q_INVOKABLE int getSupportNewUVParamFunction();
    Q_INVOKABLE bool setNewUVParamFunction(int type);
    Q_INVOKABLE QVariantMap getNewUVParamValues();
    Q_INVOKABLE bool setNewUVParamValues(const QVariantMap& settings, int type);
    Q_INVOKABLE bool startMaintenanceAction(const QString& action,
                                             const QVariantMap& arguments = {});

    bool submitPreparedJob(const DirectPrintRaster& raster,
                           const DirectPrintSettings& settings) override;

signals:
    void statusChanged();
    void printersChanged();
    void maintenanceBusyChanged();
    void maintenanceActionFinished(const QString& action, bool succeeded,
                                   const QVariant& result,
                                   const QString& errorMessage);

private:
    QVariantMap request(const QString& command,
                        const QVariantMap& arguments = {},
                        const QByteArray& payload = {},
                        int timeoutMs = 30000);
    QVariantMap exchange(const QString& command,
                         const QVariantMap& arguments,
                         const QByteArray& payload,
                         int timeoutMs, bool startService);
    bool ensureService(QString* errorMessage = nullptr);
    bool startAsyncAction(const QString& action,
                          std::function<QVariantMap()> operation);
    QVariantMap restartService(int printerIndex);
    void applyResponse(const QVariantMap& response);
    bool responseOk(const QVariantMap& response);
    bool maintenanceBool(const QString& action,
                         const QVariantMap& arguments = {});
    QVariantMap maintenanceMap(const QString& action,
                               const QVariantMap& arguments = {});
    QVariantMap configureArguments() const;

    mutable QRecursiveMutex m_mutex;
    bool m_available = false;
    bool m_connected = false;
    bool m_autoDiscoverSdk = true;
    bool m_maintenanceBusy = false;
    qint64 m_servicePid = 0;
    QString m_lastError;
    QString m_sdkRootPath;
    QVariantList m_printers;
    QString m_currentMaintenanceAction;
    QFutureWatcher<QVariantMap> m_maintenanceWatcher;
};
