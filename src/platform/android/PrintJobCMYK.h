#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

class ColorManagementManager;
class IPrintOutputClient;

class PrintJobCMYK : public QObject
{
    Q_OBJECT

signals:
    void prnGenerationFinished(bool success);
    void outputPhaseChanged(const QString& phase);
    void outputProgressChanged(qint64 completed, qint64 total);

public:
    explicit PrintJobCMYK(QObject* parent = nullptr);

    void setColorManager(ColorManagementManager* mgr);
    void setDirectPrintClient(IPrintOutputClient* client);

    Q_INVOKABLE void runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath);
    Q_INVOKABLE void runDirectPrint(const QVariantMap& jobMap);
    Q_INVOKABLE void cancelOutput();
    Q_INVOKABLE bool loadInputImage(const QString& imagePath);
    Q_INVOKABLE bool applyICCConversion(const QString& inputProfile, const QString& outputProfile);
    Q_INVOKABLE bool generateFinalPRN(const QString& outputPath, int xdpi, int ydpi);

    Q_INVOKABLE void prepareAssets();
    Q_INVOKABLE void cleanupTemporaryFiles(const QString& baseName, const QString& workingDir);
    Q_INVOKABLE void cleanupRuntimeAssets();

    Q_INVOKABLE QVariantList getAvailableICCProfiles() const;
    Q_INVOKABLE QString getDefaultOutputICCProfile() const;
    Q_INVOKABLE QString getDefaultInputCMYKProfile() const;
    Q_INVOKABLE void setDefaultOutputICCProfile(const QString& outputProfile);
    Q_INVOKABLE void setDefaultInputCMYKProfile(const QString& inputProfilePath);
    Q_INVOKABLE void enableDefaultInputCMYK(bool enabled);
    Q_INVOKABLE bool checkDefaultInputCMYK() const;
    Q_INVOKABLE void addICCProfile(const QString& name, const QString& path);
    Q_INVOKABLE void setDotStrategy(int minInkThreshold,
                                    int smallDotThreshold,
                                    int medDotThreshold,
                                    bool enablePromotion,
                                    uint8_t floorRangeCMY,
                                    uint8_t floorMaxCMY,
                                    uint8_t floorRangeK,
                                    uint8_t floorMaxK,
                                    bool enableDotSwap);

private:
    ColorManagementManager* m_colorManager = nullptr;
    QVariantList m_profiles;
    QString m_defaultOutputICCPath;
    QString m_defaultInputCMYKPath;
    bool m_useDefaultInputCMYK = true;
};
