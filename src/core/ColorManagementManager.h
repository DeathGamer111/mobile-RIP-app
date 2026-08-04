#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QVariantMap>

class ColorManagementManager : public QObject {
    Q_OBJECT

    // Printer context (so we can return printer-specific defaults)
    Q_PROPERTY(QString selectedPrinter         READ selectedPrinter         WRITE setSelectedPrinter         NOTIFY selectedPrinterChanged)

    // ICC profiles (paths or identifiers)
    Q_PROPERTY(QString defaultInputProfile     READ defaultInputProfile     WRITE setDefaultInputProfile     NOTIFY profilesChanged)
    Q_PROPERTY(QString defaultOutputProfile    READ defaultOutputProfile    WRITE setDefaultOutputProfile    NOTIFY profilesChanged)
    Q_PROPERTY(QString effectiveOutputProfile  READ effectiveOutputProfile                               	 NOTIFY profilesChanged)

    // Dot strategy defaults
    Q_PROPERTY(int  minInkThreshold            READ minInkThreshold         WRITE setMinInkThreshold       NOTIFY dotStrategyChanged)
    Q_PROPERTY(int  smallDotThreshold          READ smallDotThreshold       WRITE setSmallDotThreshold     NOTIFY dotStrategyChanged)
    Q_PROPERTY(int  medDotThreshold            READ medDotThreshold         WRITE setMedDotThreshold       NOTIFY dotStrategyChanged)
    Q_PROPERTY(bool enablePromotion            READ enablePromotion         WRITE setEnablePromotion       NOTIFY dotStrategyChanged)

    // Linearization
    Q_PROPERTY(bool    linearizationEnabled    READ linearizationEnabled    WRITE setLinearizationEnabled    NOTIFY linearizationChanged)
    Q_PROPERTY(QString linearizationPresetName READ linearizationPresetName WRITE setLinearizationPresetName NOTIFY linearizationChanged)
    Q_PROPERTY(QString linearizationDataPath   READ linearizationDataPath   WRITE setLinearizationDataPath   NOTIFY linearizationChanged)

    // Floor gating + dot swap defaults (mirrors JobDetailsView)
    Q_PROPERTY(int  floorRangeCMY              READ floorRangeCMY           WRITE setFloorRangeCMY          NOTIFY dotStrategyChanged)
    Q_PROPERTY(int  floorMaxCMY                READ floorMaxCMY             WRITE setFloorMaxCMY            NOTIFY dotStrategyChanged)
    Q_PROPERTY(int  floorRangeK                READ floorRangeK             WRITE setFloorRangeK            NOTIFY dotStrategyChanged)
    Q_PROPERTY(int  floorMaxK                  READ floorMaxK               WRITE setFloorMaxK              NOTIFY dotStrategyChanged)
    Q_PROPERTY(bool enableDotSwap              READ enableDotSwap           WRITE setEnableDotSwap          NOTIFY dotStrategyChanged)

    // Direct Nocai SDK print settings
    Q_PROPERTY(QString multiInkOutputMode      READ multiInkOutputMode      WRITE setMultiInkOutputMode    NOTIFY directPrintSettingsChanged)
    Q_PROPERTY(QString directPrintSdkRootPath  READ directPrintSdkRootPath  WRITE setDirectPrintSdkRootPath NOTIFY directPrintSettingsChanged)
    Q_PROPERTY(QString multiInkDirectPrintSdkRootPath READ multiInkDirectPrintSdkRootPath WRITE setMultiInkDirectPrintSdkRootPath NOTIFY directPrintSettingsChanged)

    // Specialty ink helpers
    Q_INVOKABLE QVariantMap getWhiteParams(int inkMode) const;
    Q_INVOKABLE QVariantMap getVarnishParams(int inkMode) const;

public:
    explicit ColorManagementManager(QObject* parent = nullptr);

    // Multi-Param Getter/Setter
    Q_INVOKABLE QVariantMap getMultiInkParams(int inkMode) const;
    Q_INVOKABLE void setMultiInkParams(int inkMode, const QVariantMap& params);

    // Single Param Getter/Setter
    Q_INVOKABLE QVariant getMultiInkParam(int inkMode, const QString& key) const;
    Q_INVOKABLE void setMultiInkParam(int inkMode, const QString& key, const QVariant& value);

    QVariantMap defaultMultiInkParamsForMode(int inkMode) const;

    QString selectedPrinter() const;
    void setSelectedPrinter(const QString& p);

    QString defaultInputProfile() const;
    void setDefaultInputProfile(const QString& p);

	// Legacy output-profile API kept for compatibility
    QString defaultOutputProfile() const;
    void setDefaultOutputProfile(const QString& p);
    QString effectiveOutputProfile() const;

    // Printer-specific output profile mapping
    Q_INVOKABLE QString printerOutputProfile(const QString& printerName) const;
    Q_INVOKABLE void setPrinterOutputProfile(const QString& printerName, const QString& profilePath);
    	
	// Colar Family Aware Profile API
	Q_INVOKABLE QString outputProfileFamilyForInkMode(int inkMode) const;

	Q_INVOKABLE QString familyDefaultOutputProfile(const QString& familyKey) const;
	Q_INVOKABLE void setFamilyDefaultOutputProfile(const QString& familyKey, const QString& profilePath);

	Q_INVOKABLE QString printerFamilyOutputProfile(const QString& printerName, const QString& familyKey) const;
	Q_INVOKABLE void setPrinterFamilyOutputProfile(const QString& printerName, const QString& familyKey, const QString& profilePath);

	Q_INVOKABLE QString effectiveOutputProfileForPrinterAndInkMode(const QString& printerName, int inkMode) const;

    // Dot strategy
    int minInkThreshold() const;
    void setMinInkThreshold(int v);

    int smallDotThreshold() const;
    void setSmallDotThreshold(int v);

    int medDotThreshold() const;
    void setMedDotThreshold(int v);

    bool enablePromotion() const;
    void setEnablePromotion(bool v);

    // Floor gating + dot swap defaults
    int floorRangeCMY() const;
    void setFloorRangeCMY(int v);

    int floorMaxCMY() const;
    void setFloorMaxCMY(int v);

    int floorRangeK() const;
    void setFloorRangeK(int v);

    int floorMaxK() const;
    void setFloorMaxK(int v);

    bool enableDotSwap() const;
    void setEnableDotSwap(bool v);

	// Linearization
    bool linearizationEnabled() const;
    void setLinearizationEnabled(bool enabled);

    QString linearizationPresetName() const;
    void setLinearizationPresetName(const QString& name);

    QString linearizationDataPath() const;
    void setLinearizationDataPath(const QString& path);

    // Family-aware linearization API
    Q_INVOKABLE QString familyDefaultLinearizationPath(const QString& familyKey) const;
    Q_INVOKABLE void setFamilyDefaultLinearizationPath(const QString& familyKey, const QString& xmlPath);

    Q_INVOKABLE QString printerFamilyLinearizationPath(const QString& printerName, const QString& familyKey) const;
    Q_INVOKABLE void setPrinterFamilyLinearizationPath(const QString& printerName, const QString& familyKey, const QString& xmlPath);

    Q_INVOKABLE QString effectiveLinearizationPathForPrinterAndInkMode(const QString& printerName, int inkMode) const;

    // Direct Nocai SDK print settings
    QString multiInkOutputMode() const;
    Q_INVOKABLE void setMultiInkOutputMode(const QString& mode);

    QString directPrintSdkRootPath() const;
    Q_INVOKABLE void setDirectPrintSdkRootPath(const QString& path);

    QString multiInkDirectPrintSdkRootPath() const;
    Q_INVOKABLE void setMultiInkDirectPrintSdkRootPath(const QString& path);
    Q_INVOKABLE QString directPrintSdkFamilyForPrinter(const QString& printerName) const;
    Q_INVOKABLE QString directPrintSdkRootForPrinter(const QString& printerName) const;

    Q_INVOKABLE QVariantMap directPrintSettings() const;
    Q_INVOKABLE void setDirectPrintSettings(const QVariantMap& settings);
    Q_INVOKABLE QVariant directPrintSetting(const QString& key) const;
    Q_INVOKABLE void setDirectPrintSetting(const QString& key, const QVariant& value);

    // Persistence
    Q_INVOKABLE bool load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE void resetToDefaults();

signals:
    void selectedPrinterChanged();
    void profilesChanged();
    void dotStrategyChanged();
    void multiInkParamsChanged();
    void linearizationChanged();
    void directPrintSettingsChanged();

private:
    // Persisted per-mode maps
    QMap<int, QVariantMap> m_multiInkParamsByMode;

    QString _settingsPath() const;

    QString m_selectedPrinter;

	// Legacy single linearization path
    bool m_enableLinearization = true;
    QString m_linearizationPresetName = "Default";
    QString m_linearizationDataPath;

    // Family-aware linearization storage
    QVariantMap m_familyDefaultLinearizationPaths;
    QVariantMap m_printerFamilyLinearizationPaths;

    // printerName -> profilePath
    QVariantMap m_printerOutputProfiles;
    
	QString m_defaultInputProfile;
    QString m_defaultOutputProfile;
    
    QVariantMap m_familyDefaultOutputProfiles;
    QVariantMap m_printerFamilyOutputProfiles;

    // Dot strategy defaults
    int  m_minInkThreshold     = 8;
    int  m_smallDotThreshold   = 64;
    int  m_medDotThreshold     = 128;
    bool m_enablePromotion     = false;

    // Added: floor gating + dot swap defaults
    int  m_floorRangeCMY       = 24;
    int  m_floorMaxCMY         = 2;
    int  m_floorRangeK         = 12;
    int  m_floorMaxK           = 0;
    bool m_enableDotSwap       = false;

    QString m_multiInkOutputMode = "prn";
    // Existing setting remains the legacy/standard-CMYK SDK root.
    QString m_directPrintSdkRootPath;
    QString m_multiInkDirectPrintSdkRootPath;
    QVariantMap m_directPrintSettings;
};
