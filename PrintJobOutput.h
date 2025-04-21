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

    Q_INVOKABLE bool loadPrinter(const QString &printerName);
    Q_INVOKABLE bool registerPrinterFromPPD(const QString &printerName, const QString &ppdPath);
    Q_INVOKABLE bool loadPPDFile(const QString &ppdPath);
    Q_INVOKABLE QStringList supportedResolutions() const;
    Q_INVOKABLE QStringList supportedMediaSizes() const;
    Q_INVOKABLE QStringList supportedDuplexModes() const;
    Q_INVOKABLE QStringList supportedColorModes() const;
    Q_INVOKABLE bool generatePRN(const PrintJob &job, const QString &inputFile, const QString &outputPath);
    Q_INVOKABLE bool generatePRN(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath);

private:
    QString printerName;
    QStringList getOptionValues(const QString &optionName) const;
    void markPpdOptionsFromJob(const PrintJob &job);
    ppd_file_t *ppd = nullptr;
};
