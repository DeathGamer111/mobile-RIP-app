#include "PrintJobMultiInk.h"
#include "NocaiPrnWriter.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <fstream>

#include <lcms2.h>


namespace {
	enum class SpecialtyMode {
		Off = 0,
		AutoUnderbase = 1, // White
		Flood = 2,         // White/Varnish
		Plate = 3,         // External grayscale plate
		Overprint = 1      // Varnish alias
	};


	// -----------------------------------------------------------------------------
	// File-local helpers
	// -----------------------------------------------------------------------------

	static constexpr int kBaseXDpi = 720;
	static constexpr int kBaseYDpi = 600;


	static int snapToMultiple(int v, int step)
	{
		if (step <= 0) return v;
		if (v <= 0) return step;
		const int q = (v + step / 2) / step;
		return std::max(step, q * step);
	}


	static QString modeToString(int mode)
	{
		switch (mode) {
		case 4:  return "4 (YMCK)";
		case 5:  return "5 (YMCK+W)";
		case 6:  return "6 (YMCK+Lm+Lc)";
		case 7:  return "7 (YMCK+Lm+Lc+W)";
		case 8:  return "8 (YMCK+Lm+Lc+Lk+LLk)";
		case 10: return "10 (YMCK+Lm+Lc+Lk+LLk+W+V)";
		default: return QString::number(mode);
		}
	}
	

	static bool isSupportedMultiInkMode(int mode)
	{
		switch (mode) {
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 10:
		    return true;
		default:
		    return false;
		}
	}


	static int vmInt(const QVariantMap& m, const char* k, int def)
	{
		auto it = m.constFind(k);
		return (it != m.constEnd()) ? it.value().toInt() : def;
	}


	static bool vmBool(const QVariantMap& m, const char* k, bool def)
	{
		auto it = m.constFind(k);
		return (it != m.constEnd()) ? it.value().toBool() : def;
	}


	static void dumpMultiInkRunConfig(
		int modeInt,
		const QString& imagePath,
		const QSize& requestedDpi,
		const QString& outputPath,
		const QString& outputICC,
		bool useDefaultInputCMYK,
		const QString& defaultInputCMYKPath,
		ColorManagementManager* colorMgr,
		const MultiInkDotStrategy& dotStrategy,
		const QVariantMap& modeParams)
	{
		auto I = [&](const char* k, int def) { return vmInt(modeParams, k, def); };
		auto B = [&](const char* k, bool def) { return vmBool(modeParams, k, def); };

		QString resolvedInputCMYK = defaultInputCMYKPath;
		if (colorMgr) {
		    const QString cmIn = colorMgr->defaultInputProfile().trimmed();
		    if (!cmIn.isEmpty()) resolvedInputCMYK = cmIn;
		}
		if (resolvedInputCMYK.trimmed().isEmpty()) resolvedInputCMYK = "(none)";

		qDebug().noquote() << "========== MultiInk PRN Pipeline Config ==========";
		qDebug().noquote() << QString("  inkMode: %1").arg(modeToString(modeInt));
		qDebug().noquote() << QString("  imagePath: %1").arg(imagePath);
		qDebug().noquote() << QString("  outputPath: %1").arg(outputPath);
		qDebug().noquote() << QString("  requested DPI: %1 x %2")
		                      .arg(requestedDpi.width()).arg(requestedDpi.height());

		qDebug().noquote() << QString("  ICC output: %1").arg(outputICC);
		qDebug().noquote() << QString("  ICC useDefaultInputCMYK: %1")
		                      .arg(useDefaultInputCMYK ? "true" : "false");
		qDebug().noquote() << QString("  ICC defaultInputCMYK: %1").arg(resolvedInputCMYK);

		qDebug().noquote() << QString("  DotStrategy: minInk=%1 small=%2 med=%3 promo=%4 dotSwap=%5")
		                      .arg(dotStrategy.minInkThreshold)
		                      .arg(dotStrategy.smallDotThreshold)
		                      .arg(dotStrategy.medDotThreshold)
		                      .arg(dotStrategy.enablePromotion ? "true" : "false")
		                      .arg(dotStrategy.enableDotSwap ? "true" : "false");

		qDebug().noquote() << QString("  Floor: CMY(range=%1 max=%2)  K(range=%3 max=%4)")
		                      .arg(int(dotStrategy.floorRangeCMY))
		                      .arg(int(dotStrategy.floorMaxCMY))
		                      .arg(int(dotStrategy.floorRangeK))
		                      .arg(int(dotStrategy.floorMaxK));

		qDebug().noquote() << "  [Per-Mode Params]";
		qDebug().noquote() << QString("    C split:  start=%1 end=%2")
		                      .arg(I("cLightStart", -1)).arg(I("cLightEnd", -1));
		qDebug().noquote() << QString("    M split:  start=%1 end=%2")
		                      .arg(I("mLightStart", -1)).arg(I("mLightEnd", -1));
		qDebug().noquote() << QString("    LightInk override: enabled=%1 minT=%2")
		                      .arg(B("useLightInkMinThresholdOverride", false) ? "true" : "false")
		                      .arg(I("lightInkMinThreshold", -1));
		qDebug().noquote() << QString("    K split:   t1Start=%1 t1End=%2 t2Start=%3 t2End=%4")
		                      .arg(I("kT1Start", -1)).arg(I("kT1End", -1))
		                      .arg(I("kT2Start", -1)).arg(I("kT2End", -1));
		qDebug().noquote() << QString(
				                  "    GCR: enabled=%1 neutralGate=%2 gcrMaxTone=%3 gcrStrength=%4 "
				                  "kGain=%5 kMinNeutral=%6 lcFadePct=%7 lmFadePct=%8")
				              .arg(B("gcrEnabled", false) ? "true" : "false")
				              .arg(I("neutralGate", -1))
				              .arg(I("gcrMaxTone", -1))
				              .arg(I("gcrStrength", -1))
				              .arg(I("kGain", -1))
				              .arg(I("kMinInNeutral", -1))
				              .arg(I("lcFadePctInNeutral", -1))
				              .arg(I("lmFadePctInNeutral", -1));
		qDebug().noquote() << QString(
		                          "    Promotion: toneGate=%1 medLo=%2 medHi=%3 lrgLo=%4 "
		                          "lrgHi=%5 flatVarEps=%6 minNei=%7 kick=%8")
		                      .arg(I("promoToneGate", -1))
		                      .arg(I("promoMedLo", -1))
		                      .arg(I("promoMedHi", -1))
		                      .arg(I("promoLrgLo", -1))
		                      .arg(I("promoLrgHi", -1))
		                      .arg(I("promoFlatVarEps", -1))
		                      .arg(I("promoMinNeiInked", -1))
		                      .arg(I("promoKickBonus", -1));

		qDebug().noquote() << QString(
		                          "    White: mode=%1 threshold=%2 density=%3 ownDots=%4 "
		                          "small=%5 med=%6 promo=%7 mask=%8")
		                      .arg(I("whiteMode", 0))
		                      .arg(I("whiteThreshold", -1))
		                      .arg(I("whiteDensity", -1))
		                      .arg(B("whiteUseOwnDotStrategy", false) ? "true" : "false")
		                      .arg(I("whiteSmallDotThreshold", -1))
		                      .arg(I("whiteMedDotThreshold", -1))
		                      .arg(B("whiteEnablePromotion", false) ? "true" : "false")
		                      .arg(modeParams.value("whiteMaskKey", "w").toString());

		qDebug().noquote() << QString(
		                          "    Varnish: mode=%1 threshold=%2 density=%3 ownDots=%4 "
		                          "small=%5 med=%6 promo=%7 mask=%8")
		                      .arg(I("varnishMode", 0))
		                      .arg(I("varnishThreshold", -1))
		                      .arg(I("varnishDensity", -1))
		                      .arg(B("varnishUseOwnDotStrategy", false) ? "true" : "false")
		                      .arg(I("varnishSmallDotThreshold", -1))
		                      .arg(I("varnishMedDotThreshold", -1))
		                      .arg(B("varnishEnablePromotion", false) ? "true" : "false")
		                      .arg(modeParams.value("varnishMaskKey", "v").toString());

		qDebug().noquote() << "==================================================";
	}


} // end of namespace



// -----------------------------------------------------------------------------
// Construction / basic state
// -----------------------------------------------------------------------------

PrintJobMultiInk::PrintJobMultiInk(QObject* parent)
    : QObject(parent)
    , m_inkMode(InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V)
    , m_assetsPrepared(false)
    , useDefaultInputCMYK(true)
{
}


void PrintJobMultiInk::setColorManager(ColorManagementManager* mgr)
{
    m_colorManager = mgr;
}


void PrintJobMultiInk::setDirectPrintClient(IPrintOutputClient* client)
{
    m_directPrintClient = client;
}


void PrintJobMultiInk::setInkMode(int mode)
{
    switch (mode) {
    case 4:
        m_inkMode = InkMode::FourColor_YMCK;
        break;
    case 5:
        m_inkMode = InkMode::FiveColor_YMCK_W;
        break;
    case 6:
        m_inkMode = InkMode::SixColor_YMCK_Lm_Lc;
        break;
    case 7:
        m_inkMode = InkMode::SevenColor_YMCK_Lm_Lc_W;
        break;
    case 8:
        m_inkMode = InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk;
        break;
    case 10:
        m_inkMode = InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V;
        break;
    default:
        qWarning() << "PrintJobMultiInk: invalid ink mode" << mode
                   << "defaulting to FourColor_YMCK (4)";
        m_inkMode = InkMode::FourColor_YMCK;
        break;
    }

    qDebug() << "PrintJobMultiInk: Ink mode set to" << mode;
}


int PrintJobMultiInk::inkMode() const
{
    return static_cast<int>(m_inkMode);
}



// -----------------------------------------------------------------------------
// Public runtime entrypoint
// -----------------------------------------------------------------------------

void PrintJobMultiInk::runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath)
{
    (void)QtConcurrent::run([=]() {
        bool success = false;
        if (const_cast<PrintJobMultiInk*>(this)->prepareJobForOutput(jobMap, outputPath)) {
            RasterPayload payload;
            const QSize resolution = jobMap.value("resolution").toSize();
            success = const_cast<PrintJobMultiInk*>(this)->buildRasterPayload(
                resolution.width(),
                resolution.height(),
                payload);

            if (success)
                success = const_cast<PrintJobMultiInk*>(this)->writePRNFile(payload, outputPath);
        }

        emit prnGenerationFinished(success);
    });
}


void PrintJobMultiInk::runDirectPrint(const QVariantMap& jobMap)
{
    (void)QtConcurrent::run([=]() {
        bool success = false;
        if (const_cast<PrintJobMultiInk*>(this)->prepareJobForOutput(jobMap, QStringLiteral("(direct print)"))) {
            RasterPayload payload;
            const QSize resolution = jobMap.value("resolution").toSize();
            success = const_cast<PrintJobMultiInk*>(this)->buildRasterPayload(
                resolution.width(),
                resolution.height(),
                payload);

            if (success)
                success = const_cast<PrintJobMultiInk*>(this)->sendDirectPrint(payload, jobMap);
        }

        emit prnGenerationFinished(success);
    });
}



// -----------------------------------------------------------------------------
// Input loading / raw asset helpers
// -----------------------------------------------------------------------------

bool PrintJobMultiInk::prepareJobForOutput(const QVariantMap& jobMap, const QString& outputPathForLogging)
{
    bool success = false;

    if (!prepareAssets()) {
        qWarning() << "PrintJobMultiInk: failed to prepare assets.";
        return false;
    }

    qDebug() << "PrintJobMultiInk::prepareJobForOutput: assets root ="
             << m_assetManager.rootPath();

    const QString imagePath = jobMap.value("imagePath").toString();
    const QSize resolution = jobMap.value("resolution").toSize();

    int modeInt = inkMode();
    if (!isSupportedMultiInkMode(modeInt)) {
        qWarning() << "PrintJobMultiInk: unsupported instance inkMode"
                   << modeInt << "forcing 4";
        modeInt = 4;
        setInkMode(4);
    }

    const QString whiteStrategy = jobMap.value("whiteStrategy").toString().trimmed();
    const QString varnishType = jobMap.value("varnishType").toString().trimmed();
    m_whitePlatePath = jobMap.value("whitePlatePath").toString().trimmed();
    m_varnishPlatePath = jobMap.value("varnishPlatePath").toString().trimmed();

    QVariantMap modeParams;
    if (m_colorManager) {
        modeParams = m_colorManager->getMultiInkParams(modeInt);
        enableLinearization(m_colorManager->linearizationEnabled());

        if (!modeParams.contains("gcrEnabled")) {
            modeParams["gcrEnabled"] = false;
        }
    }

    qDebug() << "PrintJobMultiInk: linearization enabled =" << (m_enableLinearization ? "true" : "false");

    if (!reloadLinearizationFromManager()) {
        qWarning() << "PrintJobMultiInk: failed to initialize linearization state.";
        return false;
    }

    bool hasWhiteOverride = false;
    bool hasVarnishOverride = false;

    const int whiteModeOverride =
        MultiInkToneBuilder::whiteModeOverrideFromJob(whiteStrategy, hasWhiteOverride);
    const int varnishModeOverride =
        MultiInkToneBuilder::varnishModeOverrideFromJob(varnishType, hasVarnishOverride);

    if (hasWhiteOverride) {
        modeParams["whiteMode"] = whiteModeOverride;
    }

    if (hasVarnishOverride) {
        modeParams["varnishMode"] = varnishModeOverride;
    }

    m_modeParams = modeParams;

    const int minThreshold =
        (m_colorManager ? m_colorManager->minInkThreshold() : 8);
    const int smallThreshold =
        (m_colorManager ? m_colorManager->smallDotThreshold() : 104);
    const int medThreshold =
        (m_colorManager ? m_colorManager->medDotThreshold() : 168);
    const bool promotionEnabled =
        (m_colorManager ? m_colorManager->enablePromotion() : false);

    const int floorRangeCMY =
        (m_colorManager ? m_colorManager->floorRangeCMY() : 24);
    const int floorMaxCMY =
        (m_colorManager ? m_colorManager->floorMaxCMY() : 2);
    const int floorRangeK =
        (m_colorManager ? m_colorManager->floorRangeK() : 12);
    const int floorMaxK =
        (m_colorManager ? m_colorManager->floorMaxK() : 0);
    const bool enableDotSwap =
        (m_colorManager ? m_colorManager->enableDotSwap() : false);

    setDotStrategy(
        minThreshold,
        smallThreshold,
        medThreshold,
        promotionEnabled,
        static_cast<uint8_t>(std::clamp(floorRangeCMY, 0, 64)),
        static_cast<uint8_t>(std::clamp(floorMaxCMY, 0, 8)),
        static_cast<uint8_t>(std::clamp(floorRangeK, 0, 64)),
        static_cast<uint8_t>(std::clamp(floorMaxK, 0, 8)),
        enableDotSwap
    );

    auto normalizeLocalPath = [](const QString& s) -> QString {
        if (s.startsWith("file:", Qt::CaseInsensitive))
            return QUrl(s).toLocalFile();
        return s;
    };

    auto looksLikeIccPath = [&](const QString& s) -> bool {
        const QString p = normalizeLocalPath(s).trimmed();
        if (p.isEmpty()) return false;
        const QString low = p.toLower();
        if (!(low.endsWith(".icc") || low.endsWith(".icm"))) return false;
        return QFileInfo::exists(p);
    };

    const QString jobColorProfile = jobMap.value("colorProfile").toString().trimmed();
    QString outputICC;

    if (looksLikeIccPath(jobColorProfile)) {
        outputICC = normalizeLocalPath(jobColorProfile);
    } else if (m_colorManager) {
        QString selectedPrinter = m_colorManager->selectedPrinter().trimmed();

        const QString jobPrinterName = jobMap.value("printerName").toString().trimmed();
        if (!jobPrinterName.isEmpty()) {
            selectedPrinter = jobPrinterName;
        }

        const QString resolvedProfile =
            m_colorManager->effectiveOutputProfileForPrinterAndInkMode(selectedPrinter, modeInt).trimmed();

        if (looksLikeIccPath(resolvedProfile)) {
            outputICC = normalizeLocalPath(resolvedProfile);
        }
    }

    if (outputICC.isEmpty()) {
        qWarning() << "PrintJobMultiInk: no valid output ICC resolved for printer"
                   << (m_colorManager ? m_colorManager->selectedPrinter() : QString("(none)"))
                   << "inkMode" << modeInt
                   << "- expected per-job override, printer family override, or family default.";
    }

    QString resolvedDeviceLink;
    if (looksLikeIccPath(m_defaultDeviceLinkPath)) {
        resolvedDeviceLink = normalizeLocalPath(m_defaultDeviceLinkPath);
    }

    if (m_useDeviceLink && resolvedDeviceLink.isEmpty()) {
        qWarning() << "PrintJobMultiInk: DeviceLink enabled but no DeviceLink profile selected in UI.";
    }

    qDebug() << "PrintJobMultiInk: dot strategy updated (ColorManager precedence), inkMode ="
             << modeInt
             << "outputICC =" << outputICC
             << "deviceLinkEnabled =" << (m_useDeviceLink ? "true" : "false")
             << "deviceLink =" << (resolvedDeviceLink.isEmpty() ? "(none)" : resolvedDeviceLink);

    dumpMultiInkRunConfig(modeInt,
                          imagePath,
                          resolution,
                          outputPathForLogging,
                          outputICC,
                          useDefaultInputCMYK,
                          defaultInputCMYKPath,
                          m_colorManager,
                          dotStrategy,
                          m_modeParams);

    if (!loadInputImage(imagePath))
        return false;

    if (m_useDeviceLink) {
        if (resolvedDeviceLink.isEmpty()) {
            qWarning() << "PrintJobMultiInk: DeviceLink enabled but no DeviceLink profile is set.";
            success = false;
        } else {
            qDebug() << "PrintJobMultiInk: applying DeviceLink (overrides ICC):"
                     << resolvedDeviceLink;
            success = applyDeviceLinkConversion(resolvedDeviceLink);

            if (success && inputImage.colorSpace() != Magick::CMYKColorspace) {
                qWarning() << "PrintJobMultiInk: DeviceLink conversion completed but image is not CMYK.";
                success = false;
            }
        }
    } else {
        if (inputImage.colorSpace() != Magick::CMYKColorspace) {
            qDebug() << "PrintJobMultiInk: input NOT CMYK - applying ICC (sRGB -> printer CMYK)";

            const QString inputICC = m_assetManager.assetPath("sRGBProfile.icm");
            if (!outputICC.isEmpty()) {
                success = applyICCConversion(inputICC, outputICC);
            } else {
                qWarning() << "PrintJobMultiInk: no output ICC available; cannot convert.";
                success = false;
            }
        } else {
            if (useDefaultInputCMYK) {
                QString inCMYK = defaultInputCMYKPath;
                if (m_colorManager) {
                    const QString cmIn = m_colorManager->defaultInputProfile().trimmed();
                    if (!cmIn.isEmpty())
                        inCMYK = cmIn;
                }

                if (!inCMYK.isEmpty() && !outputICC.isEmpty()) {
                    qDebug() << "PrintJobMultiInk: input CMYK - applying ICC (Default CMYK -> printer CMYK)";
                    success = applyICCConversion(inCMYK, outputICC);
                } else {
                    qWarning() << "PrintJobMultiInk: CMYK input ICC or output ICC missing; skipping conversion.";
                    success = true;
                }
            } else {
                qDebug() << "PrintJobMultiInk: input CMYK - skipping ICC conversion.";
                success = true;
            }
        }
    }

    screenSeed = qHash(imagePath) ^
                 static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    return success;
}


bool PrintJobMultiInk::loadInputImage(const QString& imagePath)
{
    try {
        const QString localPath = QUrl(imagePath).toLocalFile();
        inputImage.read(localPath.toStdString());

        QFileInfo fi(localPath);
        originalFilename = fi.fileName();

        tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            qWarning() << "PrintJobMultiInk: failed to create temp dir";
            return false;
        }

        tempImagePath = tempDir->filePath(originalFilename);
        inputImage.write(tempImagePath.toStdString());

        qDebug() << "PrintJobMultiInk: loaded and copied to" << tempImagePath;
        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "PrintJobMultiInk: image load failed:" << e.what();
        return false;
    }
}


bool PrintJobMultiInk::loadMaskRaw(const QString& key,
                                   std::vector<uint8_t>& maskRaw,
                                   int& maskW,
                                   int& maskH) const
{
    try {
        const QString maskPath = m_assetManager.assetPath(QString("mask_512_%1.tiff").arg(key));

        Magick::Image maskImg;
        maskImg.read(maskPath.toStdString());

        maskW = static_cast<int>(maskImg.columns());
        maskH = static_cast<int>(maskImg.rows());

        if (maskW <= 0 || maskH <= 0) {
            qWarning() << "PrintJobMultiInk: loaded mask has invalid size:"
                       << maskPath << "w=" << maskW << "h=" << maskH;
            return false;
        }

        maskRaw.resize(size_t(maskW) * size_t(maskH));
        maskImg.write(0, 0, maskW, maskH, "I", Magick::CharPixel, maskRaw.data());
        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "PrintJobMultiInk: failed to load mask:" << key << "err:" << e.what();
        return false;
    }
}


bool PrintJobMultiInk::loadExternalPlateTone(const QString& platePath,
                                             std::vector<uint8_t>& outTone,
                                             int width,
                                             int height) const
{
    QString localPath = platePath;
    if (localPath.startsWith("file:", Qt::CaseInsensitive))
        localPath = QUrl(localPath).toLocalFile();

    if (localPath.trimmed().isEmpty() || !QFileInfo::exists(localPath)) {
        qWarning() << "PrintJobMultiInk: external plate not found:" << platePath;
        return false;
    }

    try {
        Magick::Image plate;
        plate.read(localPath.toStdString());
        plate.colorSpace(Magick::GRAYColorspace);
        plate.type(Magick::GrayscaleType);

        if (static_cast<int>(plate.columns()) != width ||
            static_cast<int>(plate.rows()) != height) {
            const QString geom = QString("%1x%2!").arg(width).arg(height);
            plate.resize(Magick::Geometry(geom.toStdString()));
        }

        outTone.resize(size_t(width) * size_t(height));
        plate.write(0, 0, width, height, "I", Magick::CharPixel, outTone.data());
        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "PrintJobMultiInk: failed to load external plate:"
                   << platePath << "err:" << e.what();
        return false;
    }
}


std::array<Magick::Image, 4> PrintJobMultiInk::separateCMYK(Magick::Image& cmykImage)
{
    std::array<Magick::Image, 4> channels;

    const int width = static_cast<int>(cmykImage.columns());
    const int height = static_cast<int>(cmykImage.rows());

    std::vector<unsigned char> rawCMYK(width * height * 4);
    cmykImage.write(0, 0, width, height, "CMYK", Magick::CharPixel, rawCMYK.data());

    for (int ch = 0; ch < 4; ++ch) {
        std::vector<unsigned char> channelData(width * height);
        for (int i = 0; i < width * height; ++i)
            channelData[i] = rawCMYK[i * 4 + ch];

        channels[ch] = Magick::Image(Magick::Geometry(width, height), "white");
        channels[ch].depth(8);
        channels[ch].type(Magick::GrayscaleType);
        channels[ch].read(width, height, "I", Magick::CharPixel, channelData.data());
    }

    return channels;
}


QString PrintJobMultiInk::maskKeyForChannel(InkMode mode, int channelIndex) const
{
    QString whiteMaskKey = m_modeParams.value("whiteMaskKey", "w").toString().trimmed();
    if (whiteMaskKey.isEmpty()) whiteMaskKey = "w";

    QString varnishMaskKey = m_modeParams.value("varnishMaskKey", "v").toString().trimmed();
    if (varnishMaskKey.isEmpty()) varnishMaskKey = "v";

    switch (mode) {
    case InkMode::FourColor_YMCK:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        default: return "c";
        }

    case InkMode::FiveColor_YMCK_W:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        case 4: return whiteMaskKey;
        default: return "c";
        }

    case InkMode::SixColor_YMCK_Lm_Lc:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        case 4: return "lm";
        case 5: return "lc";
        default: return "c";
        }

    case InkMode::SevenColor_YMCK_Lm_Lc_W:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        case 4: return "lm";
        case 5: return "lc";
        case 6: return whiteMaskKey;
        default: return "c";
        }

    case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        case 4: return "lc";
        case 5: return "lm";
        case 6: return "lk";
        case 7: return "llk";
        default: return "c";
        }

    case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
        switch (channelIndex) {
        case 0: return "c";
        case 1: return "m";
        case 2: return "y";
        case 3: return "k";
        case 4: return "lc";
        case 5: return "lm";
        case 6: return "lk";
        case 7: return "llk";
        case 8: return whiteMaskKey;
        case 9: return varnishMaskKey;
        default: return "c";
        }
    }

    return "c";
}



// -----------------------------------------------------------------------------
// Color management
// -----------------------------------------------------------------------------

bool PrintJobMultiInk::applyICCConversion(const QString& inputProfile,
                                          const QString& outputProfile)
{
    try {
        std::ifstream inFile(inputProfile.toStdString(), std::ios::binary);
        std::ifstream outFile(outputProfile.toStdString(), std::ios::binary);

        if (!inFile || !outFile) {
            qWarning() << "PrintJobMultiInk: failed to load ICC profiles";
            return false;
        }

        inputImage.profile("icc", Magick::Blob());

        std::vector<char> inData((std::istreambuf_iterator<char>(inFile)), {});
        Magick::Blob inBlob(inData.data(), inData.size());
        inputImage.profile("icc", inBlob);

        std::vector<char> outData((std::istreambuf_iterator<char>(outFile)), {});
        Magick::Blob outBlob(outData.data(), outData.size());
        inputImage.profile("icc", outBlob);

        inputImage.colorSpace(Magick::CMYKColorspace);
        inputImage.type(Magick::ColorSeparationType);

        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "PrintJobMultiInk: ICC conversion failed:" << e.what();
        return false;
    }
}


void PrintJobMultiInk::enableDeviceLink(bool enabled)
{
    m_useDeviceLink = enabled;
    qDebug() << "PrintJobMultiInk: use device link =" << enabled;
}


bool PrintJobMultiInk::isDeviceLinkEnabled() const
{
    return m_useDeviceLink;
}


void PrintJobMultiInk::setDefaultDeviceLinkProfile(const QString& path)
{
    m_defaultDeviceLinkPath = path;
    qDebug() << "PrintJobMultiInk: default device link =" << path;
}


QString PrintJobMultiInk::getDefaultDeviceLinkProfile() const
{
    return m_defaultDeviceLinkPath;
}


void PrintJobMultiInk::addDeviceLinkProfile(const QString& name, const QString& path)
{
    for (const auto& p : m_availableDeviceLinkProfiles) {
        if (p.second == path) return;
    }
    m_availableDeviceLinkProfiles.append({name, path});
}


QVariantList PrintJobMultiInk::getAvailableDeviceLinkProfiles() const
{
    QVariantList list;
    for (const auto& pair : m_availableDeviceLinkProfiles) {
        QVariantMap entry;
        entry["name"] = pair.first;
        entry["path"] = pair.second;
        list.append(entry);
    }

    qDebug() << "PrintJobMultiInk: returning" << list.size() << "DeviceLink profiles.";
    return list;
}


bool PrintJobMultiInk::applyDeviceLinkConversion(const QString& deviceLinkPath)
{
    const QString dl = deviceLinkPath.startsWith("file:", Qt::CaseInsensitive)
                           ? QUrl(deviceLinkPath).toLocalFile()
                           : deviceLinkPath;

    if (!QFileInfo::exists(dl)) {
        qWarning() << "PrintJobMultiInk: DeviceLink not found:" << dl;
        return false;
    }

    cmsHPROFILE devLink = cmsOpenProfileFromFile(dl.toStdString().c_str(), "r");
    if (!devLink) {
        qWarning() << "PrintJobMultiInk: cmsOpenProfileFromFile failed for DeviceLink:" << dl;
        return false;
    }

    const int width = static_cast<int>(inputImage.columns());
    const int height = static_cast<int>(inputImage.rows());
    const int count = width * height;

    const bool magickIsCMYK = (inputImage.colorSpace() == Magick::CMYKColorspace);

    std::vector<unsigned char> inBuf;
    cmsUInt32Number inFormat = 0;

    if (magickIsCMYK) {
        inBuf.resize(size_t(count) * 4);
        inputImage.write(0, 0, width, height, "CMYK", Magick::CharPixel, inBuf.data());
        inFormat = TYPE_CMYK_8;
    } else {
        inBuf.resize(size_t(count) * 3);
        inputImage.write(0, 0, width, height, "RGB", Magick::CharPixel, inBuf.data());
        inFormat = TYPE_RGB_8;
    }

    std::vector<unsigned char> outBuf(size_t(count) * 4);
    cmsUInt32Number outFormat = TYPE_CMYK_8;

    const cmsUInt32Number flags = cmsFLAGS_NOOPTIMIZE;
    const int intent = INTENT_PERCEPTUAL;

    cmsHTRANSFORM xform = cmsCreateTransform(
        devLink, inFormat,
        devLink, outFormat,
        intent, flags
    );

    if (!xform) {
        qWarning() << "PrintJobMultiInk: cmsCreateTransform failed for DeviceLink:" << dl;
        cmsCloseProfile(devLink);
        return false;
    }

    cmsDoTransform(xform, inBuf.data(), outBuf.data(), count);

    cmsDeleteTransform(xform);
    cmsCloseProfile(devLink);

    Magick::Image outImg(Magick::Geometry(width, height), "white");
    outImg.depth(8);
    outImg.colorSpace(Magick::CMYKColorspace);
    outImg.type(Magick::ColorSeparationType);
    outImg.read(width, height, "CMYK", Magick::CharPixel, outBuf.data());

    inputImage = outImg;
    return true;
}



// -----------------------------------------------------------------------------
// Dot strategy / linearization
// -----------------------------------------------------------------------------

void PrintJobMultiInk::setDotStrategy(int minInkThreshold,
                                      int smallDotThreshold,
                                      int medDotThreshold,
                                      bool enablePromotion,
                                      uint8_t floorRangeCMY,
                                      uint8_t floorMaxCMY,
                                      uint8_t floorRangeK,
                                      uint8_t floorMaxK,
                                      bool enableDotSwap)
{
    dotStrategy.minInkThreshold = minInkThreshold;
    dotStrategy.smallDotThreshold = smallDotThreshold;
    dotStrategy.medDotThreshold = medDotThreshold;
    dotStrategy.enablePromotion = enablePromotion;
    dotStrategy.floorRangeCMY = floorRangeCMY;
    dotStrategy.floorMaxCMY = floorMaxCMY;
    dotStrategy.floorRangeK = floorRangeK;
    dotStrategy.floorMaxK = floorMaxK;
    dotStrategy.enableDotSwap = enableDotSwap;
}


void PrintJobMultiInk::enableLinearization(bool enabled)
{
    m_enableLinearization = enabled;
}


bool PrintJobMultiInk::isLinearizationEnabled() const
{
    return m_enableLinearization;
}


bool PrintJobMultiInk::reloadLinearizationFromManager()
{
    // Always start from a clean state so linearization is fully bypassed
    // unless we successfully load external curves below.
    m_linearization.clearExternalCurves();

    if (!m_enableLinearization) {
        qDebug() << "PrintJobMultiInk: linearization disabled, bypassing linearization entirely.";
        return true;
    }

    if (!m_colorManager) {
        qWarning() << "PrintJobMultiInk: linearization enabled but no ColorManagementManager is attached. Bypassing linearization.";
        return true;
    }

    QString selectedPrinter = m_colorManager->selectedPrinter().trimmed();
    QString xmlPath = m_colorManager->effectiveLinearizationPathForPrinterAndInkMode(
        selectedPrinter,
        static_cast<int>(m_inkMode)
    ).trimmed();

    if (xmlPath.startsWith("file:", Qt::CaseInsensitive)) {
        xmlPath = QUrl(xmlPath).toLocalFile();
    }

    if (xmlPath.isEmpty()) {
        qWarning() << "PrintJobMultiInk: linearization enabled but no XML file is resolved for printer"
                   << selectedPrinter
                   << "inkMode" << static_cast<int>(m_inkMode)
                   << ". Bypassing linearization.";
        return true;
    }

    if (!QFileInfo::exists(xmlPath)) {
        qWarning() << "PrintJobMultiInk: linearization XML does not exist:" << xmlPath
                   << "- bypassing linearization.";
        return true;
    }

    if (!m_linearization.loadTransferCurveXml(xmlPath)) {
        qWarning() << "PrintJobMultiInk: failed to load linearization XML:"
                   << xmlPath
                   << "error =" << m_linearization.lastError()
                   << "- bypassing linearization.";
        m_linearization.clearExternalCurves();
        return true;
    }

    qDebug() << "PrintJobMultiInk: loaded linearization XML:" << xmlPath;
    return true;
}



// -----------------------------------------------------------------------------
// PRN generation / writing
// -----------------------------------------------------------------------------

bool PrintJobMultiInk::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi)
{
    RasterPayload payload;
    if (!buildRasterPayload(xdpi, ydpi, payload))
        return false;

    return writePRNFile(payload, outputPath);
}


bool PrintJobMultiInk::buildRasterPayload(int xdpi, int ydpi, RasterPayload& payload)
{
    try {
        if (inputImage.colorSpace() != Magick::CMYKColorspace) {
            qWarning() << "PrintJobMultiInk: input not CMYK after ICC conversion.";
            return false;
        }

        xdpi = std::clamp(xdpi, 1, kBaseXDpi);
        ydpi = snapToMultiple(ydpi, kBaseYDpi);

        const double scaleX = static_cast<double>(xdpi) / double(kBaseYDpi);
        const double scaleY = static_cast<double>(ydpi) / double(kBaseYDpi);

        if (scaleX != 1.0 || scaleY != 1.0) {
            const int newWidth = int(std::lround(inputImage.columns() * scaleX));
            const int newHeight = int(std::lround(inputImage.rows() * scaleY));
            const QString geom = QString("%1x%2!").arg(newWidth).arg(newHeight);

            inputImage.resize(Magick::Geometry(geom.toStdString()));
            qDebug() << "PrintJobMultiInk: rescaled to"
                     << inputImage.columns() << "x" << inputImage.rows()
                     << "for DPI" << xdpi << "x" << ydpi;
        }

        auto cmykChannels = separateCMYK(inputImage);
        const int width = static_cast<int>(cmykChannels[0].columns());
        const int height = static_cast<int>(cmykChannels[0].rows());

		std::vector<std::vector<uint8_t>> toneChannels;

        MultiInkToneBuilder::BuildRequest toneReq;
        toneReq.cmykImages = &cmykChannels;
        toneReq.mode = static_cast<MultiInkToneBuilder::InkMode>(static_cast<int>(m_inkMode));
        toneReq.modeParams = m_modeParams;
        toneReq.whitePlatePath = m_whitePlatePath;
        toneReq.varnishPlatePath = m_varnishPlatePath;
        toneReq.linearization = &m_linearization;
		toneReq.enableLinearization = m_enableLinearization;

        const bool built = MultiInkToneBuilder::buildToneChannels(
            toneReq,
            toneChannels,
            [this](const QString& platePath, std::vector<uint8_t>& outTone, int w, int h) {
                return this->loadExternalPlateTone(platePath, outTone, w, h);
            });

        if (!built) {
            qWarning() << "PrintJobMultiInk: failed to build tone channels.";
            return false;
        }
        
		// applyLinearizationToToneChannels(toneChannels);

        const int numChannels = static_cast<int>(toneChannels.size());
        if (numChannels == 0) {
            qWarning() << "PrintJobMultiInk: no channels produced for ink mode.";
            return false;
        }

        const bool modeHasWhite =
            (m_inkMode == InkMode::FiveColor_YMCK_W ||
             m_inkMode == InkMode::SevenColor_YMCK_Lm_Lc_W ||
             m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V);

        const bool modeHasVarnish =
            (m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V);

        const bool whiteUseOwnDots =
            m_modeParams.value("whiteUseOwnDotStrategy", false).toBool();
        const int whiteSmallDot =
            std::clamp(m_modeParams.value("whiteSmallDotThreshold", 104).toInt(), 0, 255);
        const int whiteMedDot =
            std::clamp(m_modeParams.value("whiteMedDotThreshold", 168).toInt(), 0, 255);
        const bool whitePromo =
            m_modeParams.value("whiteEnablePromotion", false).toBool();

        const bool varnishUseOwnDots =
            m_modeParams.value("varnishUseOwnDotStrategy", false).toBool();
        const int varnishSmallDot =
            std::clamp(m_modeParams.value("varnishSmallDotThreshold", 104).toInt(), 0, 255);
        const int varnishMedDot =
            std::clamp(m_modeParams.value("varnishMedDotThreshold", 168).toInt(), 0, 255);
        const bool varnishPromo =
            m_modeParams.value("varnishEnablePromotion", false).toBool();

        auto isLightInkChannel = [&](int chIdx) -> bool {
            if (m_inkMode == InkMode::SixColor_YMCK_Lm_Lc ||
                m_inkMode == InkMode::SevenColor_YMCK_Lm_Lc_W) {
                return (chIdx == 4 || chIdx == 5);
            }
            if (m_inkMode == InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk ||
                m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V) {
                return (chIdx == 4 || chIdx == 5 || chIdx == 6 || chIdx == 7);
            }
            return false;
        };

        MultiInkScreenEngine::ScreenRequest screenReq;
        screenReq.width = width;
        screenReq.height = height;
        screenReq.screenSeed = screenSeed;
        screenReq.modeParams = m_modeParams;
        screenReq.dotStrategy = dotStrategy;

        screenReq.channels.reserve(numChannels);
        for (int ch = 0; ch < numChannels; ++ch) {
            MultiInkScreenEngine::ChannelRequest creq;
            creq.maskKey = maskKeyForChannel(m_inkMode, ch);
            creq.toneBytes = &toneChannels[ch];
            creq.isLightInk = isLightInkChannel(ch);

            const bool isWhiteChannel =
                modeHasWhite &&
                ((m_inkMode == InkMode::FiveColor_YMCK_W && ch == 4) ||
                 (m_inkMode == InkMode::SevenColor_YMCK_Lm_Lc_W && ch == 6) ||
                 (m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V && ch == 8));

            const bool isVarnishChannel =
                modeHasVarnish &&
                (m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V && ch == 9);

            if (isWhiteChannel && whiteUseOwnDots) {
                creq.useOwnDotStrategy = true;
                creq.ownSmallDotThreshold = whiteSmallDot;
                creq.ownMedDotThreshold = whiteMedDot;
                creq.ownEnablePromotion = whitePromo;
            }

            if (isVarnishChannel && varnishUseOwnDots) {
                creq.useOwnDotStrategy = true;
                creq.ownSmallDotThreshold = varnishSmallDot;
                creq.ownMedDotThreshold = varnishMedDot;
                creq.ownEnablePromotion = varnishPromo;
            }

            screenReq.channels.push_back(creq);
        }

        std::vector<std::vector<std::vector<uint8_t>>> allPacked;
        const bool screened = MultiInkScreenEngine::screenChannels(
            screenReq,
            [this](const QString& key, std::vector<uint8_t>& maskRaw, int& maskW, int& maskH) {
                return this->loadMaskRaw(key, maskRaw, maskW, maskH);
            },
            allPacked);

        if (!screened) {
            qWarning() << "PrintJobMultiInk: screening failed.";
            return false;
        }

        std::vector<int> channelOrder;
        switch (m_inkMode) {
        case InkMode::FourColor_YMCK:
            channelOrder = {2, 1, 0, 3};
            break;
        case InkMode::FiveColor_YMCK_W:
            channelOrder = {2, 1, 0, 3, 4};
            break;
        case InkMode::SixColor_YMCK_Lm_Lc:
            channelOrder = {2, 1, 0, 3, 4, 5};
            break;
        case InkMode::SevenColor_YMCK_Lm_Lc_W:
            channelOrder = {2, 1, 0, 3, 4, 5, 6};
            break;
        case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk:
            channelOrder = {2, 1, 0, 3, 5, 4, 6, 7};
            break;
        case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
            channelOrder = {2, 1, 0, 3, 5, 4, 6, 7, 8, 9};
            break;
        default:
            qWarning() << "PrintJobMultiInk: unexpected mode in channel order.";
            return false;
        }

        payload.packedLines = std::move(allPacked);
        payload.channelOrder = std::move(channelOrder);
        payload.width = width;
        payload.height = height;
        payload.xdpi = xdpi;
        payload.ydpi = ydpi;
        payload.bytesPerLine = payload.packedLines.empty() || payload.packedLines[0].empty()
            ? 0
            : static_cast<int>(payload.packedLines[0][0].size());

        return applySpecialtyBlanking(payload);

    } catch (const Magick::Exception& e) {
        qWarning() << "PrintJobMultiInk: raster payload generation failed:" << e.what();
        return false;
    }
}


bool PrintJobMultiInk::applySpecialtyBlanking(RasterPayload& payload) const
{
    if (payload.packedLines.empty() || payload.packedLines[0].empty()) {
        qWarning() << "PrintJobMultiInk: packedLines empty.";
        return false;
    }

    const int whiteMode = std::clamp(m_modeParams.value("whiteMode", 0).toInt(), 0, 3);
    const int varnishMode = std::clamp(m_modeParams.value("varnishMode", 0).toInt(), 0, 3);

    const bool whiteEnabled = (whiteMode != static_cast<int>(SpecialtyMode::Off));
    const bool varnishEnabled = (varnishMode != static_cast<int>(SpecialtyMode::Off));

    std::vector<uint8_t> blankRow(static_cast<size_t>(payload.bytesPerLine), 0);

    auto isWhiteChannel = [&](int ch) -> bool {
        switch (m_inkMode) {
        case InkMode::FiveColor_YMCK_W:
            return (ch == 4);
        case InkMode::SevenColor_YMCK_Lm_Lc_W:
            return (ch == 6);
        case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
            return (ch == 8);
        default:
            return false;
        }
    };

    auto isVarnishChannel = [&](int ch) -> bool {
        return (m_inkMode == InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V && ch == 9);
    };

    for (int row = 0; row < payload.height; ++row) {
        for (int ch : payload.channelOrder) {
            const bool forceBlank =
                (isWhiteChannel(ch) && !whiteEnabled) ||
                (isVarnishChannel(ch) && !varnishEnabled);

            if (forceBlank)
                payload.packedLines[ch][row] = blankRow;
        }
    }

    return true;
}


bool PrintJobMultiInk::writePRNFile(const RasterPayload& payload,
                                    const QString& outputPath)
{
    NocaiPrnWriter::MultiInkMode writerMode;
    switch (m_inkMode) {
    case InkMode::FourColor_YMCK:
        writerMode = NocaiPrnWriter::MultiInkMode::FourColorYMCK;
        break;
    case InkMode::FiveColor_YMCK_W:
        writerMode = NocaiPrnWriter::MultiInkMode::FiveColorYMCKW;
        break;
    case InkMode::SixColor_YMCK_Lm_Lc:
        writerMode = NocaiPrnWriter::MultiInkMode::SixColorYMCKLmLc;
        break;
    case InkMode::SevenColor_YMCK_Lm_Lc_W:
        writerMode = NocaiPrnWriter::MultiInkMode::SevenColorYMCKLmLcW;
        break;
    case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk:
        writerMode = NocaiPrnWriter::MultiInkMode::EightColorYMCKLmLcLkLLk;
        break;
    case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
        writerMode = NocaiPrnWriter::MultiInkMode::TenColorYMCKLmLcLkLLkWV;
        break;
    default:
        qWarning() << "PrintJobMultiInk: unsupported ink mode for PRN writer:" << inkMode();
        return false;
    }

    return NocaiPrnWriter::writeMultiInkPrn(
        payload.packedLines,
        payload.channelOrder,
        writerMode,
        payload.width,
        payload.height,
        payload.xdpi,
        payload.ydpi,
        payload.bytesPerLine,
        outputPath);
}


bool PrintJobMultiInk::sendDirectPrint(const RasterPayload& payload, const QVariantMap& jobMap)
{
    if (!m_directPrintClient) {
        qWarning() << "PrintJobMultiInk: direct print client is not attached.";
        return false;
    }

    DirectPrintRaster raster;
    raster.packedLines = &payload.packedLines;
    raster.channelOrder = payload.channelOrder;
    raster.width = payload.width;
    raster.height = payload.height;
    raster.xdpi = payload.xdpi;
    raster.ydpi = payload.ydpi;
    raster.bytesPerLine = payload.bytesPerLine;

    const DirectPrintSettings settings = directPrintSettingsFromJob(jobMap, payload);
    const bool ok = m_directPrintClient->submitPreparedJob(raster, settings);
    if (!ok) {
        qWarning() << "PrintJobMultiInk: direct print failed:"
                   << m_directPrintClient->lastError();
    }

    return ok;
}


DirectPrintSettings PrintJobMultiInk::directPrintSettingsFromJob(const QVariantMap& jobMap,
                                                                 const RasterPayload& payload) const
{
    QVariantMap settings;
    if (m_colorManager)
        settings = m_colorManager->directPrintSettings();

    const QVariantMap jobSettings = jobMap.value("directPrintSettings").toMap();
    for (auto it = jobSettings.begin(); it != jobSettings.end(); ++it)
        settings[it.key()] = it.value();

    auto value = [&](const QString& key, int fallback) -> int {
        return settings.value(key, fallback).toInt();
    };

    DirectPrintSettings out;
    out.printerIndex = value("selectedPrinterIndex", -1);
    out.printDirection = value("printDirection", 0);
    out.printSpeed = value("printSpeed", 1);
    out.wcSequence = value("wcSequence", 0);
    out.eclosionGrade = value("eclosionGrade", 0);
    out.headSelect = value("headSelect", 0);
    out.whiteInkPercent = value("whiteInkPercent", 0);
    out.whiteInkPassCount = value("whiteInkPassCount", 0);
    out.varnishInkPercent = value("varnishInkPercent", 0);
    out.varnishInkPassCount = value("varnishInkPassCount", 0);
    out.headVoltage = value("headVoltage", 512);
    out.disableUv0 = value("disableUv0", 0);
    out.disableUv1 = value("disableUv1", 0);
    out.disableUv2 = value("disableUv2", 0);
    out.disableUv3 = value("disableUv3", 0);
    out.disableUv4 = value("disableUv4", 0);
    out.disableUv5 = value("disableUv5", 0);
    out.carReset = value("carReset", 1);
    out.stripBlank = value("stripBlank", 1);
    out.blankDistance = value("blankDistance", 0);
    const QPoint printOffset = jobMap.value("offset", QPoint(0, 0)).toPoint();
    out.printOffsetXmm = std::max(0, printOffset.x());
    out.printOffsetYmm = std::max(0, printOffset.y());
    out.mediaHeightMm = std::clamp(
        jobMap.value("mediaHeightMm", -1.0).toDouble(), -1.0, 152.0);
    out.pass = value("pass", 0);
    out.vsdMode = value("vsdMode", 0);

    if (out.pass <= 0)
        out.pass = std::max(1, payload.ydpi / kBaseYDpi);

    return out;
}


// -----------------------------------------------------------------------------
// ICC profile state
// -----------------------------------------------------------------------------

void PrintJobMultiInk::setDefaultOutputICCProfile(const QString& outputProfile)
{
    defaultOutputICCPath = outputProfile;
    qDebug() << "PrintJobMultiInk: default output ICC =" << outputProfile;
}


QString PrintJobMultiInk::getDefaultOutputICCProfile() const
{
    return defaultOutputICCPath;
}


void PrintJobMultiInk::setDefaultInputCMYKProfile(const QString& inputProfilePath)
{
    defaultInputCMYKPath = inputProfilePath;
    qDebug() << "PrintJobMultiInk: default input CMYK ICC =" << inputProfilePath;
}


QString PrintJobMultiInk::getDefaultInputCMYKProfile() const
{
    return defaultInputCMYKPath;
}


void PrintJobMultiInk::enableDefaultInputCMYK(bool enabled)
{
    useDefaultInputCMYK = enabled;
    qDebug() << "PrintJobMultiInk: use default CMYK input profile =" << enabled;
}


bool PrintJobMultiInk::checkDefaultInputCMYK() const
{
    return useDefaultInputCMYK;
}


void PrintJobMultiInk::addICCProfile(const QString& name, const QString& path)
{
    for (const auto& pair : availableICCProfiles) {
        if (pair.second == path) {
            return;
        }
    }

    availableICCProfiles.append({name, path});
}


QVariantList PrintJobMultiInk::getAvailableICCProfiles() const
{
    QVariantList list;
    for (const auto& pair : availableICCProfiles) {
        QVariantMap entry;
        entry["name"] = pair.first;
        entry["path"] = pair.second;
        list.append(entry);
    }

    qDebug() << "PrintJobMultiInk: returning" << list.size() << "ICC profiles.";
    return list;
}


// -----------------------------------------------------------------------------
// Asset management / cleanup
// -----------------------------------------------------------------------------

bool PrintJobMultiInk::prepareAssets()
{
    if (!m_assetManager.initialize("runtime_assets")) {
        qWarning() << "PrintJobMultiInk: failed to initialize AssetManager.";
        return false;
    }

    if (m_assetsPrepared) {
        return true;
    }

    qDebug() << "PrintJobMultiInk::prepareAssets: base path =" << m_assetManager.rootPath();

    const QStringList bundledResourcePaths = {
        // Input / utility profiles
        ":/assets/sRGBProfile.icm",
        ":/assets/RIP_App_Generic_CMYK.icc",

        // New MultiInk production profiles
        ":/assets/RIP_App_1200_4C.icc",
        ":/assets/RIP_App_1200_8C.icc",

        // Linearization XMLs (bundled for convenience; still selected via ColorManager)
        ":/assets/RIP_App_Linearization_1200_4C.xml",
        ":/assets/RIP_App_Linearization_1200_8C.xml"
    };

    const QStringList bundledFileNames = {
        "sRGBProfile.icm",
        "RIP_App_Generic_CMYK.icc",

        "RIP_App_1200_4C.icc",
        "RIP_App_1200_8C.icc",

        "RIP_App_Linearization_1200_4C.xml",
        "RIP_App_Linearization_1200_8C.xml"
    };

    if (!m_assetManager.copyResourcesIfMissing(bundledResourcePaths, bundledFileNames)) {
        qWarning() << "PrintJobMultiInk: failed to copy one or more bundled runtime assets.";
        return false;
    }

    const QStringList maskKeys = {
        "c", "m", "y", "k", "lc", "lm", "lk", "llk", "w", "v"
    };

    for (const QString& key : maskKeys) {
        const QString resourcePath = QString(":/assets/blue_noise_mask_512_12000/mask_%1.tiff").arg(key);
        const QString fileName = QString("mask_512_%1.tiff").arg(key);
        if (m_assetManager.hasAsset(fileName))
            continue;
        if (QFile::exists(resourcePath)) {
            (void)m_assetManager.copyResourceIfMissing(resourcePath, fileName);
        } else {
            qWarning() << "PrintJobMultiInk: mask is not bundled and is missing from runtime assets:" << fileName;
        }
    }

    auto addProfile = [&](const QString& name, const QString& fileName) {
        const QString dest = m_assetManager.assetPath(fileName);
        addICCProfile(name, dest);
    };

    // Register profiles exposed to UI
    addProfile("PrintFlow 1200 4C", "RIP_App_1200_4C.icc");
    addProfile("PrintFlow 1200 8C/10C", "RIP_App_1200_8C.icc");
    addProfile("sRGB Input", "sRGBProfile.icm");
    addProfile("CMYK Input", "RIP_App_Generic_CMYK.icc");

    const QString profile4C = m_assetManager.assetPath("RIP_App_1200_4C.icc");
    const QString profile8C = m_assetManager.assetPath("RIP_App_1200_8C.icc");
    const QString inputCMYK = m_assetManager.assetPath("RIP_App_Generic_CMYK.icc");
    const QString lin4C = m_assetManager.assetPath("RIP_App_Linearization_1200_4C.xml");
    const QString lin8C = m_assetManager.assetPath("RIP_App_Linearization_1200_8C.xml");

    // Backend-local fallback display/default for current mode family
    if (defaultOutputICCPath.isEmpty()) {
        switch (m_inkMode) {
        case InkMode::FourColor_YMCK:
        case InkMode::FiveColor_YMCK_W:
            setDefaultOutputICCProfile(profile4C);
            break;

        case InkMode::SixColor_YMCK_Lm_Lc:
        case InkMode::SevenColor_YMCK_Lm_Lc_W:
        case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk:
        case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
            setDefaultOutputICCProfile(profile8C);
            break;

        default:
            setDefaultOutputICCProfile(profile4C);
            break;
        }
    }

    if (defaultInputCMYKPath.isEmpty()) {
        setDefaultInputCMYKProfile(inputCMYK);
    }

    // Seed ColorManager family defaults if not already configured
        if (m_colorManager) {
        if (m_colorManager->familyDefaultOutputProfile("A").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultOutputProfile("A", profile4C);
        }
        if (m_colorManager->familyDefaultOutputProfile("B").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultOutputProfile("B", profile8C); // temporary fallback
        }
        if (m_colorManager->familyDefaultOutputProfile("C").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultOutputProfile("C", profile8C);
        }

        if (m_colorManager->defaultInputProfile().trimmed().isEmpty()) {
            m_colorManager->setDefaultInputProfile(inputCMYK);
        }

        // Seed family-aware linearization defaults
        if (m_colorManager->familyDefaultLinearizationPath("A").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultLinearizationPath("A", lin4C);
        }
        if (m_colorManager->familyDefaultLinearizationPath("B").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultLinearizationPath("B", lin8C); // temporary fallback
        }
        if (m_colorManager->familyDefaultLinearizationPath("C").trimmed().isEmpty()) {
            m_colorManager->setFamilyDefaultLinearizationPath("C", lin8C);
        }

        // Keep legacy fallback populated only if empty, for backward compatibility
        if (m_colorManager->linearizationDataPath().trimmed().isEmpty()) {
            switch (m_inkMode) {
            case InkMode::FourColor_YMCK:
            case InkMode::FiveColor_YMCK_W:
                m_colorManager->setLinearizationDataPath(lin4C);
                break;

            case InkMode::SixColor_YMCK_Lm_Lc:
            case InkMode::SevenColor_YMCK_Lm_Lc_W:
            case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk:
            case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V:
                m_colorManager->setLinearizationDataPath(lin8C);
                break;

            default:
                m_colorManager->setLinearizationDataPath(lin4C);
                break;
            }
        }
    }

    m_assetsPrepared = true;

    qDebug() << "PrintJobMultiInk: assets prepared in" << m_assetManager.rootPath()
             << "profile4C =" << profile4C
             << "profile8C =" << profile8C
             << "inputCMYK =" << inputCMYK
             << "lin4C =" << lin4C
             << "lin8C =" << lin8C;

    return true;
}


bool PrintJobMultiInk::cleanupAssets()
{
    qDebug() << "PrintJobMultiInk: cleaning runtime assets in" << m_assetManager.rootPath();
    const bool ok = m_assetManager.cleanup();
    if (ok) {
        m_assetsPrepared = false;
        qDebug() << "PrintJobMultiInk: runtime assets cleaned.";
    }
    return ok;
}


void PrintJobMultiInk::cleanupTemporaryFiles(const QString& baseName,
                                             const QString& workingDir)
{
    qDebug() << "PrintJobMultiInk: cleaning intermediates for base" << baseName
             << "in" << workingDir;

    const QStringList suffixes = {
        "_c_1bit.tiff", "_m_1bit.tiff", "_y_1bit.tiff", "_k_1bit.tiff",
        "_c.tiff", "_m.tiff", "_y.tiff", "_k.tiff",
        "_cmyk.tiff",
        "_c_mask.tiff", "_m_mask.tiff", "_y_mask.tiff", "_k_mask.tiff",
        "_w.tiff", "_v.tiff", "_w_mask.tiff", "_v_mask.tiff"
    };

    for (const QString& s : suffixes) {
        const QString p = workingDir + "/" + baseName + s;
        if (QFile::exists(p)) {
            QFile::remove(p);
        }
    }

    QDir dir(workingDir);
    if (dir.exists()) {
        dir.removeRecursively();
        qDebug() << "PrintJobMultiInk: working dir removed:" << workingDir;
    }
}
