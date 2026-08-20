#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

#include "IPrintOutputClient.h"

class ColorManagementManager;

class PrintJobMultiInk : public QObject
{
    Q_OBJECT

public:
    enum class InkMode {
        FourColor_YMCK = 4,
        FiveColor_YMCK_W = 5,
        SixColor_YMCK_Lm_Lc = 6,
        SevenColor_YMCK_Lm_Lc_W = 7,
        EightColor_YMCK_Lm_Lc_Lk_LLk = 8,
        TenColor_YMCK_Lm_Lc_Lk_LLk_W_V = 10
    };
    Q_ENUM(InkMode)

    explicit PrintJobMultiInk(QObject* parent = nullptr);

signals:
    void prnGenerationFinished(bool success);
    void outputPhaseChanged(const QString& phase);
    void outputProgressChanged(qint64 completed, qint64 total);

public slots:
    Q_INVOKABLE void runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath);
    Q_INVOKABLE void runDirectPrint(const QVariantMap& jobMap);
    Q_INVOKABLE void cancelOutput();

public:
    void setColorManager(ColorManagementManager* mgr);
    void setDirectPrintClient(IPrintOutputClient* client);

    Q_INVOKABLE bool loadInputImage(const QString& imagePath);
    Q_INVOKABLE bool applyICCConversion(const QString& inputProfile, const QString& outputProfile);
    Q_INVOKABLE bool generateFinalPRN(const QString& outputPath, int xdpi, int ydpi);

    Q_INVOKABLE void setInkMode(int mode);
    Q_INVOKABLE int inkMode() const;

    Q_INVOKABLE bool prepareAssets();
    Q_INVOKABLE bool cleanupAssets();
    Q_INVOKABLE void cleanupTemporaryFiles(const QString& baseName, const QString& workingDir);

    Q_INVOKABLE void enableDeviceLink(bool enabled);
    Q_INVOKABLE bool isDeviceLinkEnabled() const;
    Q_INVOKABLE void setDefaultDeviceLinkProfile(const QString& path);
    Q_INVOKABLE QString getDefaultDeviceLinkProfile() const;
    Q_INVOKABLE void addDeviceLinkProfile(const QString& name, const QString& path);
    Q_INVOKABLE QVariantList getAvailableDeviceLinkProfiles() const;

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

    Q_INVOKABLE void enableLinearization(bool enabled);
    Q_INVOKABLE bool isLinearizationEnabled() const;

private:
    ColorManagementManager* m_colorManager = nullptr;
    IPrintOutputClient* m_directPrintClient = nullptr;
    QVariantList m_profiles;
    QVariantList m_deviceLinks;
    QString m_defaultOutputICCPath;
    QString m_defaultInputCMYKPath;
    QString m_defaultDeviceLinkPath;
    int m_inkMode = 4;
    bool m_useDefaultInputCMYK = false;
    bool m_useDeviceLink = false;
    bool m_enableLinearization = true;
};
