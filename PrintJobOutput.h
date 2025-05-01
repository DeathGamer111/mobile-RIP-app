#include <QObject>
#include <QString>
#include <QStringList>
#include <cups/cups.h>
#include <cups/ppd.h>

class PrintJob;


/*******************************************************************************
    PrintJobOutput handles printer detection and PRN file generation using CUPS.
    Supports both direct job-based rendering and fallback filter-based output.
********************************************************************************/

class PrintJobOutput : public QObject {
    Q_OBJECT

public:
    explicit PrintJobOutput(QObject *parent = nullptr);
    ~PrintJobOutput();

    // List of currently detected printers (refreshed via CUPS)
    Q_PROPERTY(QStringList detectedPrinters READ detectedPrinters NOTIFY detectedPrintersChanged)

    // Accessor for printer list
    Q_INVOKABLE QStringList detectedPrinters() const;
    // Refresh printer list from system
    Q_INVOKABLE void refreshDetectedPrinters();

    // Load printer or simulate via PPD
    Q_INVOKABLE bool loadPrinter(const QString &printerName);                                       // Load real printer via CUPS
    Q_INVOKABLE bool loadPPDFile(const QString &ppdPath);                                           // Load printer using PPD definition
    Q_INVOKABLE bool registerPrinterFromPPD(const QString &printerName, const QString &ppdPath);    // Register virtual printer from PPD

    // Supported options for current printer
    Q_INVOKABLE QStringList supportedResolutions() const;
    Q_INVOKABLE QStringList supportedMediaSizes() const;
    Q_INVOKABLE QStringList supportedDuplexModes() const;
    Q_INVOKABLE QStringList supportedColorModes() const;

    // Option validation helpers
    Q_INVOKABLE bool isOptionSupported(const QString &option) const;
    Q_INVOKABLE bool isOptionValueSupported(const QString &option, const QString &value) const;
    Q_INVOKABLE QString getDefaultOptionValue(const QString &option) const;
    Q_INVOKABLE QStringList getSupportedValues(const QString &option) const;

    // PRN generation using CUPS job flow (preferred)
    Q_INVOKABLE bool generatePRN(const PrintJob &job, const QString &inputFile, const QString &outputPath);
    Q_INVOKABLE bool generatePRN(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath);

    // PRN generation using cupsfilter (fallback)
    Q_INVOKABLE bool generatePRNviaFilter(const PrintJob &job, const QString ppdPath, const QString &inputFile, const QString &outputPath);
    Q_INVOKABLE bool generatePRNviaFilter(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath);

private:
    QString printerName;                                // Currently loaded printer name
    QStringList m_detectedPrinters;                     // Available printers

    ppd_file_t *ppd = nullptr;                          // PPD file object (deprecated API)
    cups_dinfo_t *printerInfo = nullptr;                // CUPS destination info (preferred API)

    void markPpdOptionsFromJob(const PrintJob &job);    // Apply job settings to PPD options


signals:
    void detectedPrintersChanged();                     // Emitted when printer list changes
};
