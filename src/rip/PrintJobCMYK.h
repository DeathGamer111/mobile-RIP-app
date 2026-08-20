// PrintJobCMYK.h

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtConcurrent>
#include <QVariantMap>
#include <QVariantList>
#include <QList>
#include <QPair>
#include <array>
#include <atomic>
#include <vector>
#include <cstdint>
#include <memory>
#include <Magick++.h>
#include "AssetManager.h"
#include "ColorManagementManager.h"
#include "IPrintOutputClient.h"
#include "MultiInkLinearization.h"
#include "RasterAlphaMask.h"
#include "X33WhiteToneBuilder.h"

// CMYK raster pipeline: input load -> optional ICC convert -> CMYK separation ->
// optional X-33 linearization -> blue-noise thresholding -> dot classification,
// then vendor PRN output.

class ColorManagementManager;

// Ink dot strategy (thresholds and optional neighborhood promotion).
struct DotStrategy {
	int minInkThreshold = 8;		// Below this (channel < min), skip Ink dotting in FM gate
	int smallDotThreshold = 104;	// Base cut for SMALL after tone-normalization (tRel <= smallCut == small dot(1)
	int medDotThreshold = 168;		// Base cut for MEDIUM (smallCut < tRel <= medCut == medium dot(2)
									// Everything else == large dot (3)
	bool enablePromotion = false;	// Optional neighborhood-based upsize in dense areas
	
    // Floor gating for FM screening.
	uint8_t floorRangeCMY = 24;		// FM Screening Range for CMY
    uint8_t floorMaxCMY   = 2;		// FM Screening Max for CMY
    uint8_t floorRangeK   = 12;		// FM Screening Range for K
    uint8_t floorMaxK     = 0;		// FM Screening Max for K
    
    bool enableDotSwap = false;		// Optional, allows swapping Small and Large Ink Dots
};


class PrintJobCMYK : public QObject {
    Q_OBJECT

signals:
    void prnGenerationFinished(bool success);	// Emitted when runPRNGeneration completes.
    void outputPhaseChanged(const QString& phase);
    void outputProgressChanged(qint64 completed, qint64 total);

public slots:
    Q_INVOKABLE void runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath);		// End-to-end async entry.
    Q_INVOKABLE void runDirectPrint(const QVariantMap& jobMap);
    Q_INVOKABLE void cancelOutput();

public:
    explicit PrintJobCMYK(QObject* parent = nullptr);
    
    void setColorManager(ColorManagementManager* mgr);
    void setDirectPrintClient(IPrintOutputClient* client);

    // QML-exposed pipeline
    Q_INVOKABLE bool loadInputImage(const QString& imagePath);										// Read + stage RGB/CMYK.
    Q_INVOKABLE bool applyICCConversion(const QString& inputProfile, const QString& outputProfile);	// Input->output ICC.
    Q_INVOKABLE bool generateFinalPRN(const QString& outputPath, int xdpi, int ydpi);				// Threshold, pack, write.
    
    // Internal Assets Handling for Blue Noise Mask and ICC Profiles
    Q_INVOKABLE void prepareAssets();																// Extract/copy assets to temp.
    Q_INVOKABLE void cleanupTemporaryFiles(const QString& baseName, const QString& workingDir);		// Remove temp by base.
    Q_INVOKABLE void cleanupRuntimeAssets();														// Tear down temp dir, etc.
    
    // ICC Profile Handling
    Q_INVOKABLE QVariantList getAvailableICCProfiles() const;										// [{name, path}, ...]
    Q_INVOKABLE QString getDefaultOutputICCProfile() const;											// Default out profile path.
    Q_INVOKABLE QString getDefaultInputCMYKProfile() const;											// Default input CMYK path.
    Q_INVOKABLE void setDefaultOutputICCProfile(const QString& outputProfile);						// Set Default input ICC path.
    Q_INVOKABLE void setDefaultInputCMYKProfile(const QString& inputProfilePath);					// Set Default input CMYK path.
    Q_INVOKABLE void enableDefaultInputCMYK(bool enabled);   										// Global toggle (can be overridden per jobMap)
    Q_INVOKABLE bool checkDefaultInputCMYK() const;													
    Q_INVOKABLE void addICCProfile(const QString& name, const QString& path);						// Add ICC Profile to available list.

	// Ink dot thresholds (single call to set all).
	Q_INVOKABLE void setDotStrategy(int minInkThreshold, int smallDotThreshold, int medDotThreshold, bool enablePromotion, uint8_t floorRangeCMY, uint8_t floorMaxCMY, uint8_t floorRangeK, uint8_t floorMaxK, bool enableDotSwap);


private:
	ColorManagementManager* m_colorManager = nullptr;
    IPrintOutputClient* m_directPrintClient = nullptr;

    // Working images and intermediate data.
    Magick::Image inputImage;                        	// RGB input (temporary copy)
    RasterAlphaMask sourceAlphaMask;                 // Preserved before ICC conversion.
    // Paths and temp handling
    AssetManager m_assetManager;
    QString assetsExtractPath;
	bool assetsPrepared = false;

    // Per-job white configuration for the legacy X-33 path. One logical
    // screened W plane is generated here and emitted twice as YMCKWW.
    X33WhiteToneBuilder::Mode x33WhiteMode = X33WhiteToneBuilder::Mode::Off;
    QString x33WhitePlatePath;
    QString x33WhiteMaskKey = QStringLiteral("w");
    int x33WhiteThreshold = 8;
    int x33WhiteDensity = 255;
    bool x33WhiteUseOwnDotStrategy = false;
    int x33WhiteSmallDotThreshold = 104;
    int x33WhiteMedDotThreshold = 168;
    bool x33WhiteEnablePromotion = false;

    // X-33 uses the same TransferCurve XML parser and dark CMYK LUTs as the
    // X-36 Studio four-color path, applied before FM screening.
    MultiInkLinearization x33Linearization;
    QString x33LinearizationPath;
    
    // ICC profile state.
    Magick::Blob loadICCProfile(const QString& filePath);
    QString defaultOutputICCPath;
	QString defaultInputCMYKPath;
    bool useDefaultInputCMYK = true;
    QList<QPair<QString, QString>> availableICCProfiles; // name, path

  	// Screening/packing parameters.
	DotStrategy dotStrategy;
	uint32_t screenSeed = 0;  // seed the mask phase per run
    double inputXDpi = 720.0;
    double inputYDpi = 720.0;
#if defined(PRINTFLOW_LEGACY_RASTER_REFERENCE)
    std::vector<std::vector<uint8_t>> dotClassification(const std::vector<uint8_t>& dithered, const std::vector<uint8_t>& mask, const std::vector<uint8_t>& channel, int width, int height, const DotStrategy& strategy);
#endif
    bool prepareJobForOutput(const QVariantMap& jobMap, int& xdpi, int& ydpi,
                             bool includeFinalPrn);
    void seedX33DefaultAssets();
    bool reloadLinearizationFromManager();
    bool loadInputImageForOutput(const QString& imagePath, int xdpi, int ydpi);
    bool buildRasterSpool(int xdpi, int ydpi, DirectPrintSpool& spool,
                          bool includeFinalPrn = false);
    bool writePRNFile(const DirectPrintSpool& spool, const QString& outputPath);
    bool sendDirectPrint(const DirectPrintSpool& spool, const QVariantMap& jobMap);
    DirectPrintSettings directPrintSettingsFromJob(const QVariantMap& jobMap,
                                                   const DirectPrintSpool& spool) const;
    std::atomic_bool m_cancelRequested{false};

#if defined(PRINTFLOW_LEGACY_RASTER_REFERENCE)
    void apply4x4Promotion(std::vector<std::vector<uint8_t>>& dotMap, const std::vector<uint8_t>& tone, int width, int height);
#endif

};
