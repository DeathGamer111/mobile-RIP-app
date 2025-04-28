#include <QObject>
#include <QString>
#include <QStringList>
#include <cups/cups.h>
#include <cups/ppd.h>

class PrintJob;

class PrintJobOutput : public QObject {
    Q_OBJECT

public:
    explicit PrintJobOutput(QObject *parent = nullptr);
    ~PrintJobOutput();

    Q_PROPERTY(QStringList detectedPrinters READ detectedPrinters NOTIFY detectedPrintersChanged)

    Q_INVOKABLE QStringList detectedPrinters() const;
    Q_INVOKABLE void refreshDetectedPrinters();

    Q_INVOKABLE bool loadPrinter(const QString &printerName);
    Q_INVOKABLE bool loadPPDFile(const QString &ppdPath);
    Q_INVOKABLE bool registerPrinterFromPPD(const QString &printerName, const QString &ppdPath);

    Q_INVOKABLE QStringList supportedResolutions() const;
    Q_INVOKABLE QStringList supportedMediaSizes() const;
    Q_INVOKABLE QStringList supportedDuplexModes() const;
    Q_INVOKABLE QStringList supportedColorModes() const;

    Q_INVOKABLE bool isOptionSupported(const QString &option) const;
    Q_INVOKABLE bool isOptionValueSupported(const QString &option, const QString &value) const;
    Q_INVOKABLE QString getDefaultOptionValue(const QString &option) const;
    Q_INVOKABLE QStringList getSupportedValues(const QString &option) const;

    Q_INVOKABLE bool generatePRN(const PrintJob &job, const QString &inputFile, const QString &outputPath);
    Q_INVOKABLE bool generatePRN(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath);

    Q_INVOKABLE bool generatePRNviaFilter(const PrintJob &job, const QString ppdPath, const QString &inputFile, const QString &outputPath);
    Q_INVOKABLE bool generatePRNviaFilter(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath);

private:
    QString printerName;
    QStringList m_detectedPrinters;

    ppd_file_t *ppd = nullptr;
    cups_dinfo_t *printerInfo = nullptr;

    void markPpdOptionsFromJob(const PrintJob &job);    

signals:
    void detectedPrintersChanged();
};
