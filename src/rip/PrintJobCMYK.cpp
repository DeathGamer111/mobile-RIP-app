#include "PrintJobCMYK.h"
#include "BoundedRasterPipeline.h"
#include "ImagePhysicalSize.h"
#include "NocaiPrnWriter.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QProcess>
#include <QScopeGuard>
#include <QDebug>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>


/* PrintJobCMYK.cpp
 * End-to-end PRN pipeline:
 *   loadInputImage -> (optional) applyICCConversion -> generateFinalPRN
 * Core stages (in generateFinalPRN):
 *   1) DPI scaling
 *   2) CMYK channel separation
 *   3) Optional X-33 CMYK transfer-curve linearization
 *   4) Blue-noise thresholding (FM screen) with highlight floor gating
 *   5) Dot classification (small/medium/large) using mask-relative thresholds
 *   6) Optional neighborhood-based “promotion” to reduce peppering
 *   7) 2bpp packing per channel and PRN write with simple header
 */
 
 
// Constructor; No-operation; state set by loader and setters.
PrintJobCMYK::PrintJobCMYK(QObject* parent) : QObject(parent) {}

#if defined(PRINTFLOW_LEGACY_RASTER_REFERENCE)
// Deterministic reference helpers compiled only into the legacy test target.
static inline uint32_t pxhash(
    uint32_t x, uint32_t y, uint32_t ch, uint32_t seed = 0x9E3779B9u)
{
    uint32_t h = x * 0x85EBCA6Bu ^ y * 0xC2B2AE35u ^
                 (ch + 1) * 0x27D4EB2Du ^ seed;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t weight)
{
    return static_cast<uint8_t>(((255 - weight) * a + weight * b) / 255);
}
#endif


/* === Async entry point from QML ===
 * - Sets dot thresholds from jobMap
 * - Loads image
 * - Applies ICC conversion (sRGB->printer if RGB; CMYK->printer if enabled)
 * - Seeds screening
 * - Calls generateFinalPRN and emits prnGenerationFinished
 */
void PrintJobCMYK::runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    (void) QtConcurrent::run([=]() {
        emit outputPhaseChanged(QStringLiteral("preprocessing"));
        int xdpi = 0;
        int ydpi = 0;
        bool success = const_cast<PrintJobCMYK*>(this)->prepareJobForOutput(
            jobMap, xdpi, ydpi, true);
        DirectPrintSpool spool;
        if (success)
            success = const_cast<PrintJobCMYK*>(this)->buildRasterSpool(
                xdpi, ydpi, spool, true);
        if (success) {
            emit outputPhaseChanged(QStringLiteral("generatingPrn"));
            success = const_cast<PrintJobCMYK*>(this)->writePRNFile(
                spool, outputPath);
        }
        PrintFlowRasterSpool::remove(spool);
        emit prnGenerationFinished(success);
    });
}

void PrintJobCMYK::runDirectPrint(const QVariantMap& jobMap) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    (void) QtConcurrent::run([=]() {
        emit outputPhaseChanged(QStringLiteral("preprocessing"));
        int xdpi = 0;
        int ydpi = 0;
        bool success = const_cast<PrintJobCMYK*>(this)->prepareJobForOutput(
            jobMap, xdpi, ydpi, false);
        DirectPrintSpool spool;
        if (success)
            success = const_cast<PrintJobCMYK*>(this)->buildRasterSpool(xdpi, ydpi, spool);
        if (success) {
            emit outputPhaseChanged(QStringLiteral("printing"));
            success = const_cast<PrintJobCMYK*>(this)->sendDirectPrint(spool, jobMap);
        }
        PrintFlowRasterSpool::remove(spool);
        emit prnGenerationFinished(success);
    });
}

void PrintJobCMYK::cancelOutput()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool PrintJobCMYK::prepareJobForOutput(
    const QVariantMap& jobMap, int& xdpi, int& ydpi, bool includeFinalPrn) {
    BoundedRasterPipeline::configureImageMagickCache();
    prepareAssets();
    qDebug() << "PrintJobCMYK::prepareJobForOutput: assetsExtractPath =" << assetsExtractPath;

    const QString imagePath = jobMap.value("imagePath").toString();
    const QSize resolution = jobMap.value("resolution").toSize();
    xdpi = resolution.width();
    ydpi = resolution.height();
    if (xdpi <= 0 || ydpi <= 0) {
        qWarning() << "PrintJobCMYK: invalid output resolution" << resolution;
        return false;
    }

    setDotStrategy(
        m_colorManager ? m_colorManager->minInkThreshold() : dotStrategy.minInkThreshold,
        m_colorManager ? m_colorManager->smallDotThreshold() : dotStrategy.smallDotThreshold,
        m_colorManager ? m_colorManager->medDotThreshold() : dotStrategy.medDotThreshold,
        m_colorManager ? m_colorManager->enablePromotion() : dotStrategy.enablePromotion,
        static_cast<uint8_t>(std::clamp(m_colorManager ? m_colorManager->floorRangeCMY() : int(dotStrategy.floorRangeCMY), 0, 64)),
        static_cast<uint8_t>(std::clamp(m_colorManager ? m_colorManager->floorMaxCMY() : int(dotStrategy.floorMaxCMY), 0, 8)),
        static_cast<uint8_t>(std::clamp(m_colorManager ? m_colorManager->floorRangeK() : int(dotStrategy.floorRangeK), 0, 64)),
        static_cast<uint8_t>(std::clamp(m_colorManager ? m_colorManager->floorMaxK() : int(dotStrategy.floorMaxK), 0, 8)),
        m_colorManager ? m_colorManager->enableDotSwap() : dotStrategy.enableDotSwap);

    const QVariantMap whiteParams = m_colorManager
        ? m_colorManager->getMultiInkParams(5)
        : QVariantMap();
    const QString whiteStrategy = jobMap.value("whiteStrategy").toString().trimmed();
    bool recognizedWhiteStrategy = false;
    x33WhiteMode = X33WhiteToneBuilder::modeFromJob(whiteStrategy, &recognizedWhiteStrategy);
    if (whiteStrategy.compare(QStringLiteral("Use Global Setting"), Qt::CaseInsensitive) == 0) {
        x33WhiteMode = static_cast<X33WhiteToneBuilder::Mode>(
            std::clamp(whiteParams.value("whiteMode", 0).toInt(), 0, 3));
        recognizedWhiteStrategy = true;
    }
    if (!recognizedWhiteStrategy) {
        qWarning() << "PrintJobCMYK: unsupported X-33 white strategy"
                   << whiteStrategy << "- white disabled.";
        x33WhiteMode = X33WhiteToneBuilder::Mode::Off;
    }

    x33WhitePlatePath = jobMap.value("whitePlatePath").toString().trimmed();
    x33WhiteMaskKey = whiteParams.value("whiteMaskKey", "w").toString().trimmed();
    if (x33WhiteMaskKey.isEmpty())
        x33WhiteMaskKey = QStringLiteral("w");
    x33WhiteThreshold = std::clamp(
        whiteParams.value("whiteThreshold", 8).toInt(), 0, 255);
    x33WhiteDensity = std::clamp(
        whiteParams.value("whiteDensity", 255).toInt(), 0, 255);
    x33WhiteUseOwnDotStrategy = whiteParams.value(
        "whiteUseOwnDotStrategy", false).toBool();
    x33WhiteSmallDotThreshold = std::clamp(
        whiteParams.value("whiteSmallDotThreshold", 104).toInt(), 0, 255);
    x33WhiteMedDotThreshold = std::clamp(
        whiteParams.value("whiteMedDotThreshold", 168).toInt(), 0, 255);
    x33WhiteEnablePromotion = whiteParams.value(
        "whiteEnablePromotion", false).toBool();

    if (!reloadLinearizationFromManager())
        return false;

    auto normalizeLocalPath = [](const QString& value) {
        return value.startsWith("file:", Qt::CaseInsensitive) ? QUrl(value).toLocalFile() : value;
    };
    auto looksLikeIccPath = [&](const QString& value) {
        const QString path = normalizeLocalPath(value).trimmed();
        const QString lower = path.toLower();
        return !path.isEmpty()
            && (lower.endsWith(".icc") || lower.endsWith(".icm"))
            && QFileInfo::exists(path);
    };

    QString outputICC;
    const QString jobProfile = jobMap.value("colorProfile").toString().trimmed();
    if (looksLikeIccPath(jobProfile))
        outputICC = normalizeLocalPath(jobProfile);
    else if (m_colorManager) {
        const QString familyProfile =
            m_colorManager->effectiveOutputProfileForPrinterAndInkMode(
                QStringLiteral("X-33"), 4);
        if (looksLikeIccPath(familyProfile))
            outputICC = normalizeLocalPath(familyProfile);
        else if (looksLikeIccPath(m_colorManager->effectiveOutputProfile()))
            outputICC = normalizeLocalPath(m_colorManager->effectiveOutputProfile());
    }
    if (outputICC.isEmpty())
        outputICC = defaultOutputICCPath;

    const QString localImagePath = imagePath.startsWith("file:", Qt::CaseInsensitive)
        ? QUrl(imagePath).toLocalFile() : imagePath;
    BoundedRasterPipeline::SourceInfo sourceInfo;
    QString storageError;
    if (!BoundedRasterPipeline::inspectSource(
            localImagePath, xdpi, ydpi, 720.0, 720.0,
            &sourceInfo, &storageError)) {
        qWarning().noquote() << "PrintJobCMYK:" << storageError;
        return false;
    }
    const QSize preflightOutputSize = ImagePhysicalSize::outputPixelSize(
        size_t(sourceInfo.width), size_t(sourceInfo.height),
        {sourceInfo.xDpi, sourceInfo.yDpi, true}, xdpi, ydpi);
    const bool whiteEnabled = x33WhiteMode != X33WhiteToneBuilder::Mode::Off;
    QStringList maskPaths = {
        assetsExtractPath + "/mask_512_c.tiff",
        assetsExtractPath + "/mask_512_m.tiff",
        assetsExtractPath + "/mask_512_y.tiff",
        assetsExtractPath + "/mask_512_k.tiff"
    };
    if (whiteEnabled)
        maskPaths.append(assetsExtractPath + QString("/mask_512_%1.tiff").arg(x33WhiteMaskKey));
    quint64 maskCacheBytes = 0;
    if (!preflightOutputSize.isValid()
        || !BoundedRasterPipeline::estimateMaskCacheStorage(
            maskPaths, &maskCacheBytes, &storageError)
        || !BoundedRasterPipeline::preflightStorage(
            preflightOutputSize.width(), preflightOutputSize.height(),
            sourceInfo.width, sourceInfo.height, whiteEnabled ? 5 : 4,
            whiteEnabled ? 6 : 4, includeFinalPrn, sourceInfo.hasAlpha,
            whiteEnabled && x33WhiteMode == X33WhiteToneBuilder::Mode::Plate ? 1 : 0,
            maskCacheBytes, &storageError)) {
        if (storageError.isEmpty())
            storageError = QStringLiteral("Could not resolve raster dimensions for storage preflight.");
        qWarning().noquote() << "PrintJobCMYK:" << storageError;
        return false;
    }

    if (!loadInputImageForOutput(imagePath, xdpi, ydpi))
        return false;

    bool success = false;
    if (inputImage.colorSpace() != Magick::CMYKColorspace) {
        const QString inputICC = assetsExtractPath + "/sRGBProfile.icm";
        success = !outputICC.isEmpty() && applyICCConversion(inputICC, outputICC);
    } else if (!useDefaultInputCMYK) {
        qDebug() << "PrintJobCMYK: input CMYK — skipping ICC conversion.";
        success = true;
    } else {
        QString inputICC = defaultInputCMYKPath;
        if (m_colorManager && !m_colorManager->defaultInputProfile().trimmed().isEmpty()) {
            inputICC = m_colorManager->defaultInputProfile().trimmed();
        }
        success = (inputICC.isEmpty() || outputICC.isEmpty())
            ? true
            : applyICCConversion(inputICC, outputICC);
    }

    if (success)
        screenSeed = qHash(imagePath)
            ^ static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    return success;
}


bool PrintJobCMYK::reloadLinearizationFromManager()
{
    x33Linearization.clearExternalCurves();
    x33LinearizationPath.clear();

    if (!m_colorManager) {
        qWarning() << "PrintJobCMYK: no ColorManagementManager is attached; bypassing X-33 linearization.";
        return true;
    }

    const bool enabled = m_colorManager->linearizationEnabled();
    qDebug() << "PrintJobCMYK: X-33 linearization enabled ="
             << (enabled ? "true" : "false");
    if (!enabled) {
        qDebug() << "PrintJobCMYK: X-33 linearization disabled; bypassing transfer curves.";
        return true;
    }

    QString xmlPath = m_colorManager
                          ->effectiveLinearizationPathForPrinterAndInkMode(
                              m_colorManager->selectedPrinter(), 4)
                          .trimmed();
    if (xmlPath.startsWith("file:", Qt::CaseInsensitive))
        xmlPath = QUrl(xmlPath).toLocalFile();

    if (xmlPath.isEmpty()) {
        qWarning() << "PrintJobCMYK: X-33 linearization is enabled but no XML file is resolved; bypassing transfer curves.";
        return true;
    }
    if (!QFileInfo::exists(xmlPath)) {
        qWarning() << "PrintJobCMYK: X-33 linearization XML does not exist:"
                   << xmlPath << "- bypassing transfer curves.";
        return true;
    }
    if (!x33Linearization.loadTransferCurveXml(xmlPath)) {
        qWarning() << "PrintJobCMYK: failed to load X-33 linearization XML:"
                   << xmlPath << "error =" << x33Linearization.lastError()
                   << "- bypassing transfer curves.";
        x33Linearization.clearExternalCurves();
        return true;
    }

    x33LinearizationPath = xmlPath;
    qDebug() << "PrintJobCMYK: loaded X-33 linearization XML:"
             << x33LinearizationPath;
    return true;
}


// Main PRN generation: separation -> threshold -> classify -> promote -> pack -> write.
bool PrintJobCMYK::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    DirectPrintSpool spool;
    const bool ok = buildRasterSpool(xdpi, ydpi, spool, true) &&
                    writePRNFile(spool, outputPath);
    PrintFlowRasterSpool::remove(spool);
    return ok;
}

bool PrintJobCMYK::buildRasterSpool(
    int xdpi, int ydpi, DirectPrintSpool& spool, bool includeFinalPrn) {
    try {
        if (inputImage.colorSpace() != Magick::CMYKColorspace) {
            qWarning() << "Input image is not in CMYK colorspace.";
            return false;
        }
        
        // Preserve source physical dimensions while converting its pixels to
        // the selected printer resolution.
        const QSize outputSize = ImagePhysicalSize::outputPixelSize(
            inputImage.columns(), inputImage.rows(),
            {inputXDpi, inputYDpi, true}, xdpi, ydpi);
        if (!outputSize.isValid()) {
            qWarning() << "PrintJobCMYK: could not resolve output raster dimensions.";
            return false;
        }

        const bool whiteEnabled = x33WhiteMode != X33WhiteToneBuilder::Mode::Off;
        const std::vector<int> nocaiOrder = whiteEnabled
            ? std::vector<int>({2, 1, 0, 3, 4, 4})
            : std::vector<int>({2, 1, 0, 3});
        const int logicalChannelCount = whiteEnabled ? 5 : 4;
        QString storageError;
        QStringList chKeys = {"c", "m", "y", "k"};
        if (whiteEnabled)
            chKeys.append(x33WhiteMaskKey);
        QStringList maskPaths;
        for (const QString& key : chKeys)
            maskPaths.append(assetsExtractPath +
                QString("/mask_512_%1.tiff").arg(key));
        quint64 maskCacheBytes = 0;
        if (!BoundedRasterPipeline::estimateMaskCacheStorage(
                maskPaths, &maskCacheBytes, &storageError)) {
            qWarning().noquote() << "PrintJobCMYK:" << storageError;
            return false;
        }
        if (!BoundedRasterPipeline::preflightStorage(
                outputSize.width(), outputSize.height(),
                static_cast<int>(inputImage.columns()),
                static_cast<int>(inputImage.rows()), logicalChannelCount,
                int(nocaiOrder.size()), includeFinalPrn,
                sourceAlphaMask.isActive(),
                whiteEnabled && x33WhiteMode == X33WhiteToneBuilder::Mode::Plate ? 1 : 0,
                maskCacheBytes,
                &storageError)) {
            qWarning().noquote() << "PrintJobCMYK:" << storageError;
            return false;
        }

        if (outputSize.width() != static_cast<int>(inputImage.columns()) ||
            outputSize.height() != static_cast<int>(inputImage.rows())) {
            const int newWidth = outputSize.width();
            const int newHeight = outputSize.height();

			// Apply Resize to fit output DPI
			QString resizeGeometry = QString("%1x%2!").arg(newWidth).arg(newHeight);
			inputImage.resize(Magick::Geometry(resizeGeometry.toStdString()));
            if (!sourceAlphaMask.resize(newWidth, newHeight)) {
                qWarning() << "PrintJobCMYK: failed to resize the source alpha mask.";
                return false;
            }

            qDebug() << "Rescaled image for output DPI:" << xdpi << "x" << ydpi << "→" << inputImage.columns() << "x" << inputImage.rows();
        }
        const int width = static_cast<int>(inputImage.columns());
        const int height = static_cast<int>(inputImage.rows());

        BoundedRasterPipeline::CanonicalCmykFile canonical;
        emit outputPhaseChanged(QStringLiteral("preprocessing"));
        QString error;
        if (!canonical.create(
                inputImage, sourceAlphaMask,
                BoundedRasterPipeline::scratchDirectory(), &m_cancelRequested,
                [this](qint64 completed, qint64 total) {
                    emit outputProgressChanged(completed, total);
                }, &error)) {
            qWarning().noquote() << "PrintJobCMYK:" << error;
            return false;
        }

        std::unique_ptr<Magick::Image> whitePlate;
        if (whiteEnabled && x33WhiteMode == X33WhiteToneBuilder::Mode::Plate) {
            QString platePath = x33WhitePlatePath.trimmed();
            if (platePath.startsWith("file:", Qt::CaseInsensitive))
                platePath = QUrl(platePath).toLocalFile();
            if (platePath.isEmpty() || !QFileInfo::exists(platePath)) {
                qWarning() << "PrintJobCMYK: X-33 white plate not found:" << x33WhitePlatePath;
                return false;
            }
            whitePlate = std::make_unique<Magick::Image>();
            whitePlate->read(platePath.toStdString());
            whitePlate->colorSpace(Magick::GRAYColorspace);
            whitePlate->type(Magick::GrayscaleType);
            if (static_cast<int>(whitePlate->columns()) != width ||
                static_cast<int>(whitePlate->rows()) != height) {
                whitePlate->resize(Magick::Geometry(
                    QString("%1x%2!").arg(width).arg(height).toStdString()));
            }
        }

        if (x33Linearization.hasExternalCurves()) {
            qDebug() << "PrintJobCMYK: applied X-33 linearization curves to CMYK tone channels:"
                     << x33LinearizationPath;
        }
        spool = {};
        spool.channelOrder = nocaiOrder;
        spool.logicalChannelCount = logicalChannelCount;
        spool.width = width;
        spool.height = height;
        spool.xdpi = xdpi;
        spool.ydpi = ydpi;
        spool.bytesPerLine = (((width + 3) / 4) + 3) & ~3;
        spool.format = DirectPrintRasterFormat::NocaiX33Standard;
        spool.canonicalHeader = NocaiPrnWriter::makeStandardX33Header(
            width, height, xdpi, ydpi, spool.bytesPerLine, int(nocaiOrder.size()));

        PrintFlowRasterSpool::Writer writer;
        if (!writer.create(BoundedRasterPipeline::scratchDirectory(), spool, &error)) {
            qWarning().noquote() << "PrintJobCMYK:" << error;
            return false;
        }

        std::vector<BoundedRasterPipeline::ScreenChannel> screenChannels;
        screenChannels.reserve(logicalChannelCount);
        bool skippedPromotion = false;
        for (int channel = 0; channel < logicalChannelCount; ++channel) {
            DotStrategy active = dotStrategy;
            if (channel == 4 && x33WhiteUseOwnDotStrategy) {
                active.smallDotThreshold = x33WhiteSmallDotThreshold;
                active.medDotThreshold = x33WhiteMedDotThreshold;
                active.enablePromotion = x33WhiteEnablePromotion;
            }
            BoundedRasterPipeline::ScreenChannel request;
            request.maskPath = assetsExtractPath +
                QString("/mask_512_%1.tiff").arg(chKeys[channel]);
            request.minInkThreshold = active.minInkThreshold;
            request.smallDotThreshold = active.smallDotThreshold;
            request.medDotThreshold = active.medDotThreshold;
            request.floorRange = channel == 3 ? active.floorRangeK : active.floorRangeCMY;
            request.floorMax = channel == 3 ? active.floorMaxK : active.floorMaxCMY;
            request.enableDotSwap = active.enableDotSwap;
            request.enablePromotion = active.enablePromotion;
            request.useEffectiveTone = false;
            skippedPromotion = skippedPromotion || !active.enablePromotion;
            screenChannels.push_back(std::move(request));
        }
        if (skippedPromotion)
            qDebug() << "Dot Promotion Disabled — skipping Dot Promotion.";

        emit outputPhaseChanged(QStringLiteral("rasterizing"));
        const auto toneProvider = [&](int firstRow, int rowCount,
                                      std::vector<std::vector<uint8_t>>& tones,
                                      QString* providerError) -> bool {
            std::vector<uint8_t> raw;
            if (!canonical.readRows(firstRow, rowCount, raw, providerError))
                return false;
            const size_t pixels = static_cast<size_t>(width) * rowCount;
            std::array<std::vector<uint8_t>, 4> cmyk;
            for (auto& channel : cmyk)
                channel.resize(pixels);
            for (size_t pixel = 0; pixel < pixels; ++pixel)
                for (int channel = 0; channel < 4; ++channel)
                    cmyk[channel][pixel] = raw[pixel * 4ULL + size_t(channel)];
            if (x33Linearization.hasExternalCurves() &&
                !x33Linearization.applyFourColorTones(cmyk)) {
                if (providerError)
                    *providerError = QStringLiteral("Could not apply X-33 linearization to a raster strip.");
                return false;
            }
            tones.assign(logicalChannelCount, {});
            if (whiteEnabled) {
                X33WhiteToneBuilder::BuildRequest request;
                request.cmykTones = &cmyk;
                request.width = width;
                request.height = rowCount;
                request.mode = x33WhiteMode;
                request.threshold = x33WhiteThreshold;
                request.density = x33WhiteDensity;
                request.platePath = x33WhitePlatePath;
                if (!X33WhiteToneBuilder::build(
                        request, tones[4],
                        [&](const QString&, std::vector<uint8_t>& plate,
                            int plateWidth, int plateHeight) {
                            if (!whitePlate || plateWidth != width || plateHeight != rowCount)
                                return false;
                            plate.resize(pixels);
                            whitePlate->write(0, firstRow, width, rowCount,
                                              "I", Magick::CharPixel, plate.data());
                            return true;
                        })) {
                    if (providerError)
                        *providerError = QStringLiteral("Could not build the X-33 white strip.");
                    return false;
                }
                if (x33Linearization.hasExternalCurves() &&
                    !x33Linearization.applyWhiteTone(tones[4]))
                    return false;
            }
            for (int channel = 0; channel < 4; ++channel)
                tones[channel] = std::move(cmyk[channel]);
            return true;
        };

        if (!BoundedRasterPipeline::screenToSpool(
                width, height, screenSeed, {}, screenChannels, toneProvider,
                writer, &m_cancelRequested,
                [this](qint64 completed, qint64 total) {
                    emit outputProgressChanged(completed, total);
                }, &error)) {
            qWarning().noquote() << "PrintJobCMYK:" << error;
            return false;
        }
        emit outputPhaseChanged(QStringLiteral("spooling"));
        if (!writer.finalize(
                &spool, &error,
                [this]() {
                    return m_cancelRequested.load(std::memory_order_relaxed);
                })) {
            qWarning().noquote() << "PrintJobCMYK:" << error;
            return false;
        }

        if (whiteEnabled) {
            qDebug() << "PrintJobCMYK: X-33 white enabled; prepared YMCKWW raster with"
                     << spool.channelOrder.size() << "physical planes.";
        }
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "PRN generation failed:" << e.what();
        return false;
    } catch (const std::bad_alloc&) {
        qWarning() << "PrintJobCMYK: bounded raster allocation failed.";
        return false;
    }
}

bool PrintJobCMYK::writePRNFile(const DirectPrintSpool& spool, const QString& outputPath) {
    return NocaiPrnWriter::writeStandardX33Prn(spool, outputPath);
}

bool PrintJobCMYK::sendDirectPrint(const DirectPrintSpool& spool, const QVariantMap& jobMap) {
    if (!m_directPrintClient) {
        qWarning() << "PrintJobCMYK: direct print client is not attached.";
        return false;
    }

    auto* spooledClient = dynamic_cast<ISpooledPrintOutputClient*>(m_directPrintClient);
    if (!spooledClient) {
        qWarning() << "PrintJobCMYK: direct print client does not support bounded raster spools.";
        return false;
    }
    spooledClient->setSpoolProgressCallback(
        [this](const QString& phase, qint64 completed, qint64 total) {
            emit outputPhaseChanged(phase);
            emit outputProgressChanged(completed, total);
        });
    const auto clearProgress = qScopeGuard([spooledClient]() {
        spooledClient->setSpoolProgressCallback({});
    });
    emit outputPhaseChanged(QStringLiteral("uploading"));
    emit outputProgressChanged(0, 1);
    const bool ok = spooledClient->submitSpooledJob(
        spool, directPrintSettingsFromJob(jobMap, spool));
    if (!ok)
        qWarning() << "PrintJobCMYK: direct print failed:" << m_directPrintClient->lastError();
    return ok;
}

DirectPrintSettings PrintJobCMYK::directPrintSettingsFromJob(
    const QVariantMap& jobMap, const DirectPrintSpool& spool) const {
    QVariantMap settings = m_colorManager ? m_colorManager->directPrintSettings() : QVariantMap();
    const QVariantMap overrides = jobMap.value("directPrintSettings").toMap();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        settings[it.key()] = it.value();

    auto value = [&](const QString& key, int fallback) {
        return settings.value(key, fallback).toInt();
    };

    DirectPrintSettings out;
    out.printerIndex = value("selectedPrinterIndex", -1);
    out.printDirection = value("printDirection", 0);
    out.printSpeed = value("printSpeed", 1);
    out.wcSequence = value("wcSequence", 0);
    out.eclosionGrade = value("eclosionGrade", 2);
    // CPrinter_Model_X33 starts with e2HeadConfig, which maps to HeadSelect=0.
    // Enforce it here so a value persisted during earlier integration tests
    // cannot select a different SDK swath layout.
    out.headSelect = 0;
    const bool hasWhite = spool.channelOrder
        == std::vector<int>({2, 1, 0, 3, 4, 4});
    // The X-33/iQueue integration clears these when no W plane exists. With
    // white enabled, pass the raw legacy SDK values selected in Printer Setup.
    out.whiteInkPercent = hasWhite ? value("whiteInkPercent", 0) : 0;
    out.whiteInkPassCount = hasWhite ? value("whiteInkPassCount", 0) : 0;
    out.headVoltage = value("headVoltage", 512);
    out.disableUv0 = value("disableUv0", 0);
    out.disableUv1 = value("disableUv1", 0);
    out.disableUv2 = value("disableUv2", 0);
    out.disableUv3 = value("disableUv3", 0);
    out.disableUv4 = value("disableUv4", 0);
    out.disableUv5 = value("disableUv5", 0);
    // The X-33 class maps the normal "carriage reset enabled" setting to 0.
    out.carReset = 0;
    // CPrinter_Model_X33 explicitly uses STRIP_BLANK_ALL (enum value 0).
    out.stripBlank = 0;
    out.blankDistance = value("blankDistance", 0);
    const QPoint printOffset = jobMap.value("offset", QPoint(0, 0)).toPoint();
    out.printOffsetXmm = std::max(0, printOffset.x());
    out.printOffsetYmm = std::max(0, printOffset.y());
    out.mediaHeightMm = std::clamp(
        jobMap.value("mediaHeightMm", -1.0).toDouble(), -1.0, 152.0);
    // The proven standard X-33 PRN header always uses Pass=1, including at
    // 720x1440 and 720x2160. The canonical header is submitted unchanged.
    out.pass = 1;
    out.vsdMode = value("vsdMode", 0);
    return out;
}

void PrintJobCMYK::setDirectPrintClient(IPrintOutputClient* client) {
    m_directPrintClient = client;
}


// Attach input ICC (source) then destination ICC (printer), then force CMYK storage.
bool PrintJobCMYK::applyICCConversion(const QString& inputProfile, const QString& outputProfile) {
    try {
		std::ifstream inFile(inputProfile.toStdString(), std::ios::binary);
		std::ifstream outFile(outputProfile.toStdString(), std::ios::binary);

        if (!inFile || !outFile) {
            qWarning() << "Failed to load one or both ICC profiles.";
            return false;
        }
        
        // Remove any embedded profiles before applying destination
        inputImage.profile("icc", Magick::Blob()); // Clear embedded profile

        // Load and apply input profile (e.g., sRGB)
        std::vector<char> inData((std::istreambuf_iterator<char>(inFile)), {});
        Magick::Blob inBlob(inData.data(), inData.size());
        inputImage.profile("icc", inBlob); // Attach source profile

        // Load and apply destination profile (e.g., CMYK printer)
        std::vector<char> outData((std::istreambuf_iterator<char>(outFile)), {});
        Magick::Blob outBlob(outData.data(), outData.size());
        inputImage.profile("icc", outBlob); // Apply new profile

        // Force Magick to convert to CMYK colorspace
		inputImage.colorSpace(Magick::CMYKColorspace);
		inputImage.type(Magick::ColorSeparationType);

        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "ICC conversion failed:" << e.what();
        return false;
    }
}


void PrintJobCMYK::setColorManager(ColorManagementManager* mgr) {
    m_colorManager = mgr;
    if (assetsPrepared)
        seedX33DefaultAssets();
}



// Set all ink dot thresholds and promotion toggle at once.
void PrintJobCMYK::setDotStrategy(int minInkThreshold, int smallDotThreshold, int medDotThreshold, bool enablePromotion, uint8_t floorRangeCMY, uint8_t floorMaxCMY, uint8_t floorRangeK, uint8_t floorMaxK, bool enableDotSwap) {
    dotStrategy.minInkThreshold 	= minInkThreshold;
    dotStrategy.smallDotThreshold 	= smallDotThreshold;
    dotStrategy.medDotThreshold 	= medDotThreshold;
    dotStrategy.enablePromotion 	= enablePromotion;
    dotStrategy.floorRangeCMY 		= floorRangeCMY;
    dotStrategy.floorMaxCMY   		= floorMaxCMY;
    dotStrategy.floorRangeK   		= floorRangeK;
    dotStrategy.floorMaxK     		= floorMaxK;
    dotStrategy.enableDotSwap 		= enableDotSwap;
}


#if defined(PRINTFLOW_LEGACY_RASTER_REFERENCE)
/* Dot classification
 * Input:
 *   dithered: result of FM screen (0/255)
 *   mask:     mask sample used for this pixel
 *   channel:  tone 0..255 (higher = more ink)
 *   thresholds: small/med cuts are “base” values adjusted by tone
 * Behavior:
 *   - Only pixels that passed dithered are classified.
 *   - tRel rescales the mask to 0..255 in the range that could pass at this tone.
 *   - small/med cuts are lerped against tone to keep highlight dots physically smaller.
 *   - Optional probabilistic swap small<->large in low tones to soften boundaries.
 */
std::vector<std::vector<uint8_t>> PrintJobCMYK::dotClassification(
    const std::vector<uint8_t>& dithered,
    const std::vector<uint8_t>& mask,
    const std::vector<uint8_t>& channel,
    int width, int height,
    const DotStrategy& strategy)
{
    std::vector<std::vector<uint8_t>> dotMap(height, std::vector<uint8_t>(width, 0));

    // Base (ascending) cuts on the tone-normalized mask (tRel)
	const uint8_t smallBase = static_cast<uint8_t>(std::clamp(strategy.smallDotThreshold, 0, 255));
	const uint8_t medBase   = static_cast<uint8_t>(std::clamp(strategy.medDotThreshold,   0, 255));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;

            if (dithered[idx] == 0) continue;

            const uint8_t v = channel[idx]; // ink tone 0..255 (higher = more ink)
            const uint8_t t = mask[idx];    // mask 0..255
            
            // Normalize mask to the *passed* tone range: tRel ~ [0..255] regardless of v
            // When v is small, only low t values pass; this rescales those to full 0..255.
            const uint8_t tRel = uint8_t((uint16_t(t) * v) / 255);

            // Bias the cuts by tone so highlights tend toward larger dots (your original intent),
            // but without forcing *everything* to large at all tones.
            // At v=0 (very light): cuts are lower → relatively more medium/large than small.
            // At v=255 (dark): cuts revert to smallBase/medBase.
            const uint8_t smallCut = lerp_u8(64,  smallBase, v);  // 64 → smallBase across tone
            const uint8_t medCut   = lerp_u8(130, medBase,  v);   // 130 → medBase across tone

            if (tRel <= smallCut)           dotMap[y][x] = 1; // small
            else if (tRel <= medCut)        dotMap[y][x] = 2; // medium
            else                            dotMap[y][x] = 3; // large
            
			// Optional: swap small<->large in low tones (softly blended), medium stays medium
            if (strategy.enableDotSwap) {
                const uint8_t lo = 96;   // start of full-swap band
                const uint8_t hi = 160;  // end of probabilistic blend band

                if (v < lo) {
                    // Full swap (1 <-> 3)
                    dotMap[y][x] = static_cast<uint8_t>(4 - dotMap[y][x]);
                } else if (v < hi) {
                    // Probabilistic swap to avoid a hard contour
                    uint32_t h = pxhash(x, y, 0, 0x51F2F90Du);
                    uint8_t  p = static_cast<uint8_t>((uint16_t)(hi - v) * 255 / (hi - lo)); // 255..0
                    if ((h & 255) < p) {
                        dotMap[y][x] = static_cast<uint8_t>(4 - dotMap[y][x]);
                    }
                }
        	}
        }
    }
    return dotMap;
}





// Gradual, one-step-only promotion with soft probability bands.
// - Small -> Medium in a lower band
// - Medium -> Large in a higher band
// - No Small -> Large jump
// - Tone-gated; avoids highlights and edges
void PrintJobCMYK::apply4x4Promotion(std::vector<std::vector<uint8_t>>& dotMap,
                                      const std::vector<uint8_t>& tone,
                                      int width, int height)
{
    // ---- Tunables (gentle defaults) ----
    const uint8_t  TONE_GATE        = 112;  // don’t promote in highlights
    const int      MED_LO           = 18;   // weighted sum lower bound to *start* small->med
    const int      MED_HI           = 26;   // upper bound where small->med becomes likely
    const int      LRG_LO           = 28;   // weighted sum lower bound to *start* med->large
    const int      LRG_HI           = 36;   // upper bound where med->large becomes likely
    const int      FLAT_VAR_EPS     = 18;   // require fairly flat local tone (lower = stricter)
    const int      MIN_NEI_INKED    = 8;    // need at least N/15 neighbors inked before considering
    const int      KICK_BONUS       = 2;    // small, gentle bias (instead of big bias/override)

    auto localMAD = [&](int cx, int cy) -> int {
        int sum = 0, cnt = 0;
        const int idxC = cy * width + cx;
        const int vC = tone[idxC];
        for (int dy = -1; dy <= 2; ++dy) {
            int y = cy + dy; if (y < 0 || y >= height) continue;
            for (int dx = -1; dx <= 2; ++dx) {
                int x = cx + dx; if (x < 0 || x >= width) continue;
                if (dx == 0 && dy == 0) continue;
                sum += std::abs(int(tone[y*width + x]) - vC);
                ++cnt;
            }
        }
        return cnt ? (sum / cnt) : 0;
    };

    auto lerp01 = [](int v, int lo, int hi) -> float {
        if (v <= lo) return 0.f;
        if (v >= hi) return 1.f;
        return float(v - lo) / float(hi - lo);
    };

    for (int y = 1; y < height - 2; ++y) {
        for (int x = 1; x < width - 2; ++x) {

            uint8_t cls = dotMap[y][x];          // 0..3
            if (cls == 0 || cls == 3) continue;  // only consider small(1) or medium(2)
            const int idx = y * width + x;
            const uint8_t v = tone[idx];
            if (v < TONE_GATE) continue;         // skip highlights

            // Neighborhood stats (4×4, no center)
            int weighted = 0;
            int countAny = 0;
            for (int dy = -1; dy <= 2; ++dy) {
                for (int dx = -1; dx <= 2; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    uint8_t ncls = dotMap[y + dy][x + dx]; // 0..3
                    if (ncls > 0) ++countAny;
                    weighted += int(ncls);
                }
            }

            if (countAny < MIN_NEI_INKED) continue;  // too sparse → no promotion
            if (localMAD(x, y) > FLAT_VAR_EPS) continue; // edges/textures → no promotion

            // Small gentle bias (replaces big bonuses/overrides)
            weighted += KICK_BONUS;

            // Tone factor (0..1): higher tone should be more willing to coarsen
            const float toneF = float(v) / 255.0f;

            // Deterministic RNG from coords (no flicker between runs)
            const uint32_t rnd = pxhash(x, y, 0, 0x51F2F90Du) & 0xFFFF;
            const float r01 = float(rnd) / 65535.0f;

            // Decide the band and compute probability
            if (cls == 1) {
                // small -> medium
                float p = lerp01(weighted, MED_LO, MED_HI) * toneF; // soften by tone
                if (r01 < p) dotMap[y][x] = 2;
            } else if (cls == 2) {
                // medium -> large
                float p = lerp01(weighted, LRG_LO, LRG_HI) * toneF;
                if (r01 < p) dotMap[y][x] = 3;
            }
        }
    }
}
#endif





// Read image and stage a temp file to work from (avoids touching originals).
bool PrintJobCMYK::loadInputImage(const QString& imagePath) {
    return loadInputImageForOutput(imagePath, 720, 720);
}

bool PrintJobCMYK::loadInputImageForOutput(
    const QString& imagePath, int xdpi, int ydpi)
{
    try {
        sourceAlphaMask.reset();
        QString localPath = QUrl(imagePath).toLocalFile();
        inputImage = Magick::Image();
        ImagePhysicalSize::setVectorReadDensity(inputImage, localPath, xdpi, ydpi);
        inputImage.read(localPath.toStdString());
        const ImagePhysicalSize::Density density =
            ImagePhysicalSize::resolvedDensity(inputImage, 720.0, 720.0);
        inputXDpi = density.xDpi;
        inputYDpi = density.yDpi;
        qDebug() << "PrintJobCMYK: source density" << inputXDpi << "x"
                 << inputYDpi << "DPI"
                 << (density.hasPhysicalUnits ? "(file/document)" : "(720 DPI fallback)");
        if (!sourceAlphaMask.capture(inputImage, &m_cancelRequested)) {
            qWarning() << "PrintJobCMYK: failed to preserve the source alpha channel.";
            return false;
        }

        qDebug() << "Loaded input image from:" << localPath;
        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "Image load failed:" << e.what();
        return false;
    }
}


// Read an ICC file into a Magick::Blob.
Magick::Blob PrintJobCMYK::loadICCProfile(const QString& path) {
    std::ifstream file(path.toStdString(), std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(file)), {});
    return Magick::Blob(buf.data(), buf.size());
}


// Default output ICC path setter and getter
void PrintJobCMYK::setDefaultOutputICCProfile(const QString& outputProfile) {
    defaultOutputICCPath = outputProfile;
    qDebug() << "Default output ICC profile set to:" << outputProfile;
}

QString PrintJobCMYK::getDefaultOutputICCProfile() const { return defaultOutputICCPath; }


// Default input CMYK path setter and getter
void PrintJobCMYK::setDefaultInputCMYKProfile(const QString& inputProfilePath) {
    defaultInputCMYKPath = inputProfilePath;
    qDebug() << "Default input CMYK profile set to:" << inputProfilePath;
}

QString PrintJobCMYK::getDefaultInputCMYKProfile() const { return defaultInputCMYKPath; }


// Global toggle for CMYK->printer conversion (when source is already CMYK).
void PrintJobCMYK::enableDefaultInputCMYK(bool enabled) {
    useDefaultInputCMYK = enabled;
    qDebug() << "Use default CMYK input profile:" << enabled;
}

bool PrintJobCMYK::checkDefaultInputCMYK() const { return useDefaultInputCMYK; }


// Add a named ICC profile to the in-memory list.
void PrintJobCMYK::addICCProfile(const QString& name, const QString& path) {
    // Avoid duplicates: if this path is already in the list, skip adding.
    for (const auto& pair : availableICCProfiles) {
        if (pair.second == path) {
            qDebug() << "PrintJobCMYK::addICCProfile: skipping duplicate ICC profile at"
                     << path;
            return;
        }
    }
    
    availableICCProfiles.append({name, path});
}


// Return available ICC profiles as [{name,path}, ...].
QVariantList PrintJobCMYK::getAvailableICCProfiles() const {
    QVariantList list;
    for (const auto& pair : availableICCProfiles) {
        QVariantMap entry;
        entry["name"] = pair.first;
        entry["path"] = pair.second;
        list.append(entry);
    }
    qDebug() << "Returning" << list.size() << "available ICC profiles.";
    return list;
}


// Copy runtime assets (profiles and masks) out of resources to a writeable temp location.
// Also initialize defaults and register profiles for UI selection.
void PrintJobCMYK::prepareAssets() {
    if (!m_assetManager.initialize("runtime_assets")) {
        qWarning() << "PrintJobCMYK: failed to initialize AssetManager.";
        return;
    }

    assetsExtractPath = m_assetManager.rootPath();

    // If assets have been moved and directory still exists, skip all the work.
    if (assetsPrepared && !assetsExtractPath.isEmpty() && QDir(assetsExtractPath).exists()) {
        seedX33DefaultAssets();
        qDebug() << "PrintJobCMYK: assets already prepared in" << assetsExtractPath;
        return;
    }

    const QStringList bundledResourcePaths = {
        ":/assets/sRGBProfile.icm",
        ":/assets/RIP_App_1440_Plain_Default.icc",
        ":/assets/RIP_App_1440_Plain_Neutral.icc",
        ":/assets/RIP_App_Generic_CMYK.icc",
        ":/assets/RIP_App_Linearization_1440_X-33.xml"
    };

    const QStringList bundledFileNames = {
        "sRGBProfile.icm",
        "RIP_App_1440_Plain_Default.icc",
        "RIP_App_1440_Plain_Neutral.icc",
        "RIP_App_Generic_CMYK.icc",
        "RIP_App_Linearization_1440_X-33.xml"
    };

    if (!m_assetManager.syncResources(bundledResourcePaths, bundledFileNames)) {
        qWarning() << "PrintJobCMYK: failed to stage one or more bundled runtime assets.";
        return;
    }

    const QStringList maskKeys = {"c", "m", "y", "k", "w"};
    for (const QString& key : maskKeys) {
        const QString resourcePath = QString(":/assets/blue_noise_mask_512_12000/mask_%1.tiff").arg(key);
        const QString fileName = QString("mask_512_%1.tiff").arg(key);
        if (m_assetManager.hasAsset(fileName))
            continue;
        if (QFile::exists(resourcePath)) {
            (void)m_assetManager.copyResourceIfMissing(resourcePath, fileName);
        } else {
            qWarning() << "PrintJobCMYK: mask is not bundled and is missing from runtime assets:" << fileName;
        }
    }
    
	auto addProfile = [&](const QString& name, const QString& qrcPath, const QString& fileName) {
        if (!m_assetManager.copyResourceIfMissing(qrcPath, fileName)) {
            qWarning() << "PrintJobCMYK: failed to copy ICC profile:" << qrcPath;
            return;
        }
        addICCProfile(name, m_assetManager.assetPath(fileName));
	};

    // Register ICC Profiles for UI and set defaults.
    addProfile("Default - Plain Paper (1440DPI)",  ":/assets/RIP_App_1440_Plain_Default.icc", "RIP_App_1440_Plain_Default.icc");
    addProfile("Neutral Profile - Plain Paper (1440DPI)",  ":/assets/RIP_App_1440_Plain_Neutral.icc", "RIP_App_1440_Plain_Neutral.icc");
    addProfile("sRGB Input", ":/assets/sRGBProfile.icm", "sRGBProfile.icm");
    addProfile("CMYK Input", ":/assets/RIP_App_Generic_CMYK.icc", "RIP_App_Generic_CMYK.icc");
    setDefaultOutputICCProfile(m_assetManager.assetPath("RIP_App_1440_Plain_Default.icc"));
    setDefaultInputCMYKProfile(m_assetManager.assetPath("RIP_App_Generic_CMYK.icc"));

    seedX33DefaultAssets();

    assetsPrepared = true;

    qDebug() << "PrintJobCMYK assets prepared in:" << assetsExtractPath;
}

void PrintJobCMYK::seedX33DefaultAssets()
{
    if (!m_colorManager || assetsExtractPath.isEmpty())
        return;
    const auto configuredPathExists = [](const QString& value) {
        const QString path = value.startsWith("file:", Qt::CaseInsensitive)
            ? QUrl(value).toLocalFile() : value;
        return !path.trimmed().isEmpty() && QFileInfo::exists(path);
    };
    const QString x33Profile =
        m_assetManager.assetPath("RIP_App_1440_Plain_Default.icc");
    const QString x33Linearization =
        m_assetManager.assetPath("RIP_App_Linearization_1440_X-33.xml");
    const QString configuredProfile =
        m_colorManager->printerFamilyOutputProfile("X-33", "A");
    if (configuredProfile.trimmed().isEmpty()
        || !configuredPathExists(configuredProfile)) {
        m_colorManager->setPrinterFamilyOutputProfile(
            "X-33", "A", x33Profile);
    }
    const QString configuredLinearization =
        m_colorManager->printerFamilyLinearizationPath("X-33", "A");
    if (configuredLinearization.trimmed().isEmpty()
        || !configuredPathExists(configuredLinearization)) {
        m_colorManager->setPrinterFamilyLinearizationPath(
            "X-33", "A", x33Linearization);
    }
}


// Remove intermediate files for a given job (and optionally its working directory).
void PrintJobCMYK::cleanupTemporaryFiles(const QString& baseName, const QString& workingDir) {
    qDebug() << "Cleaning intermediate files for base:" << baseName << "in dir:" << workingDir;
    QStringList suffixes = {
        "_c_1bit.tiff", "_m_1bit.tiff", "_y_1bit.tiff", "_k_1bit.tiff",
        "_c.tiff", "_m.tiff", "_y.tiff", "_k.tiff",
        "_cmyk.tiff",
        "_c_mask.tiff", "_m_mask.tiff", "_y_mask.tiff", "_k_mask.tiff"
    };

    for (const QString& suffix : suffixes) {
        QString path = workingDir + "/" + baseName + suffix;
        if (QFile::exists(path)) {
            QFile::remove(path);
        }
    }
    
    // Optionally remove entire working dir
    QDir dir(workingDir);
    if (dir.exists()) {
        dir.removeRecursively();
        qDebug() << "Working directory removed:" << workingDir;
    }
}


// Remove all runtime assets from the app data location.
void PrintJobCMYK::cleanupRuntimeAssets() {
    qDebug() << "Cleaning runtime assets in:" << assetsExtractPath;
    if (m_assetManager.cleanup()) {
        assetsPrepared = false;
        qDebug() << "Runtime assets cleaned.";
    } else {
        qWarning() << "Failed to clean runtime assets.";
    }
}
