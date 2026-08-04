#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QStringList>
#include <QLibrary>
#include <QRecursiveMutex>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

#include "IPrintOutputClient.h"

class NocaiDirectPrintClient : public QObject, public IPrintOutputClient
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(QString sdkRootPath READ sdkRootPath WRITE setSdkRootPath NOTIFY statusChanged)
    Q_PROPERTY(bool autoDiscoverSdk READ autoDiscoverSdk WRITE setAutoDiscoverSdk NOTIFY statusChanged)
    Q_PROPERTY(QVariantList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QStringList maintenanceSupportedPrinters READ maintenanceSupportedPrinters CONSTANT)

public:
    explicit NocaiDirectPrintClient(QObject* parent = nullptr);
    ~NocaiDirectPrintClient() override;

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

    Q_INVOKABLE QVariantList searchPrinters();
    Q_INVOKABLE bool supportsMaintenance(const QString& printerName) const;
    Q_INVOKABLE bool refreshPrinters();
    Q_INVOKABLE bool choosePrinter(int index);
    Q_INVOKABLE bool abortPrint();
    Q_INVOKABLE bool pausePrint();
    Q_INVOKABLE bool continuePrint();
    Q_INVOKABLE QString statusText();
    Q_INVOKABLE bool connectPrinter();
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

    bool submitPreparedJob(const DirectPrintRaster& raster,
                           const DirectPrintSettings& settings) override;
    bool printPackedJob(const DirectPrintRaster& raster,
                        const DirectPrintSettings& settings);
    static int runSerializedPrintWorker(const QString& jobPath);

signals:
    void statusChanged();
    void printersChanged();

private:
    struct PrinterInfoList;
    struct PrintJobProperty;
    struct JobSettings;
    struct AlignmentValues;
    struct PrinterStatus;
    struct PrinterInfo;
    struct UVParamValues;
    struct NewUVParamValues;

    using SearchPrinterFn = int (*)(PrinterInfoList*, int);
    using ChoosePrinterFn = int (*)(int);
    using ContinuePrintFn = int (*)();
    using InitPrinterFn = int (*)();
    using StartPrintFn = int (*)(PrintJobProperty*);
    using PrintALineFn = int (*)(char*, uint32_t);
    using AbortPrintFn = int (*)();
    using PausePrintFn = int (*)();
    using EndPrintFn = int (*)();
    using ClosePrintFn = int (*)();
    using SetJobSettingsFn = int (*)(JobSettings*, int);
    using GetJobSettingsFn = int (*)(JobSettings*, int);
    using ConnectPrinterFn = int (*)();
    using HeadMaskFn = int (*)(int);
    using NoArgFn = int (*)();
    using MoveAxisFn = int (*)(int, int);
    using AxisPosFn = int (*)(int, int*);
    using SetPrintHeightFn = int (*)(uint16_t);
    using GetPrintHeightFn = int (*)(uint16_t*);
    using SetAlignmentValuesFn = int (*)(AlignmentValues*, int, int);
    using GetAlignmentValuesFn = int (*)(AlignmentValues*, int);
    using ConfigFileFn = int (*)(char*);
    using PrintAlignmentPatternFn = int (*)(int);
    using GetPrinterStatusFn = int (*)(PrinterStatus*, int);
    using GetPrinterInfoFn = int (*)(PrinterInfo*, int);
    using SetPrintXYValueFn = int (*)(uint32_t, uint32_t);
    using GetPrintXYValueFn = int (*)(uint32_t*, uint32_t*);
    using SetUVParamValuesFn = int (*)(UVParamValues*, int, int);
    using GetUVParamValuesFn = int (*)(UVParamValues*, int);
    using GetSupportNewUVParamFunctionFn = int (*)();
    using SetNewUVParamFunctionFn = int (*)(int);
    using SetNewUVParamValuesFn = int (*)(NewUVParamValues*, int, int);
    using GetNewUVParamValuesFn = int (*)(NewUVParamValues*, int);

    bool ensureLoaded();
    bool resolveSymbols();
    QString resolveSdkRoot() const;
    QStringList sdkRootCandidates() const;
    void setError(const QString& message);
    bool callSucceeded(int result, const QString& functionName);
    bool requireFunction(const void* fn, const QString& functionName);
    bool withSdkWorkingDirectory(const std::function<bool()>& callback);
    JobSettings makeJobSettings(const DirectPrintSettings& settings) const;
    QVariantMap jobSettingsToMap(const JobSettings& settings) const;
    JobSettings jobSettingsFromMap(const QVariantMap& settings) const;
    QVariantMap alignmentValuesToMap(const AlignmentValues& values) const;
    AlignmentValues alignmentValuesFromMap(const QVariantMap& settings) const;
    QVariantMap uvParamValuesToMap(const UVParamValues& values) const;
    UVParamValues uvParamValuesFromMap(const QVariantMap& settings) const;
    QVariantMap newUvParamValuesToMap(const NewUVParamValues& values) const;
    NewUVParamValues newUvParamValuesFromMap(const QVariantMap& settings) const;
    QString controllerErrorDetails() const;
    bool submitPreparedJobIsolated(const DirectPrintRaster& raster,
                                   const DirectPrintSettings& settings);

    mutable QRecursiveMutex m_mutex;
    QLibrary m_library;
    bool m_symbolsResolved = false;
    bool m_autoDiscoverSdk = true;
    bool m_connected = false;
    QString m_sdkRootPath;
    QString m_resolvedSdkRoot;
    QString m_lastError;
    QVariantList m_printers;
    int m_selectedPrinterIndex = -1;

    SearchPrinterFn m_searchPrinter = nullptr;
    ChoosePrinterFn m_choosePrinter = nullptr;
    ContinuePrintFn m_continuePrint = nullptr;
    InitPrinterFn m_initPrinter = nullptr;
    StartPrintFn m_startPrint = nullptr;
    PrintALineFn m_printALine = nullptr;
    AbortPrintFn m_abortPrint = nullptr;
    PausePrintFn m_pausePrint = nullptr;
    EndPrintFn m_endPrint = nullptr;
    ClosePrintFn m_closePrint = nullptr;
    SetJobSettingsFn m_setJobSettings = nullptr;
    GetJobSettingsFn m_getJobSettings = nullptr;
    ConnectPrinterFn m_connectPrinter = nullptr;
    HeadMaskFn m_wipePrintHead = nullptr;
    HeadMaskFn m_startCleanOperation = nullptr;
    HeadMaskFn m_startPump = nullptr;
    NoArgFn m_stopPumpOperation = nullptr;
    HeadMaskFn m_spitPrintHead = nullptr;
    NoArgFn m_stopSpitOperation = nullptr;
    NoArgFn m_capPrintHead = nullptr;
    MoveAxisFn m_moveAxis = nullptr;
    AxisPosFn m_stopAxis = nullptr;
    AxisPosFn m_saveAxisPos = nullptr;
    SetPrintHeightFn m_setPrintHeight = nullptr;
    GetPrintHeightFn m_getPrintHeight = nullptr;
    SetAlignmentValuesFn m_setAlignmentValues = nullptr;
    GetAlignmentValuesFn m_getAlignmentValues = nullptr;
    ConfigFileFn m_exportConfigFile = nullptr;
    ConfigFileFn m_importConfigFile = nullptr;
    PrintAlignmentPatternFn m_printAlignmentPattern = nullptr;
    GetPrinterStatusFn m_getPrinterStatus = nullptr;
    GetPrinterInfoFn m_getPrinterInfo = nullptr;
    SetPrintXYValueFn m_setPrintXYValue = nullptr;
    GetPrintXYValueFn m_getPrintXYValue = nullptr;
    SetUVParamValuesFn m_setUVParamValues = nullptr;
    GetUVParamValuesFn m_getUVParamValues = nullptr;
    GetSupportNewUVParamFunctionFn m_getSupportNewUVParamFunction = nullptr;
    SetNewUVParamFunctionFn m_setNewUVParamFunction = nullptr;
    SetNewUVParamValuesFn m_setNewUVParamValues = nullptr;
    GetNewUVParamValuesFn m_getNewUVParamValues = nullptr;

    // Optional diagnostic data exported by the legacy SDK. CurErrorInfo is
    // the controller's raw 42-byte error response; the remaining values help
    // distinguish a transport/raster failure from a printer hardware error.
    const unsigned char* m_currentErrorInfo = nullptr;
    const uint32_t* m_sdkJobProperty = nullptr;
    const int* m_sdkPrinterError = nullptr;
    const int* m_sdkNetError = nullptr;
    const int* m_sdkUartError = nullptr;
    const int* m_sdkErrorSlice = nullptr;
    const int* m_sdkErrorSwath = nullptr;
};
