#include "BoundedRasterPipeline.h"
#include "MultiInkScreenEngine.h"
#include "MultiInkLinearization.h"
#include "MultiInkToneBuilder.h"
#include "PrintJobMultiInk.h"
#include "RasterAlphaMask.h"
#include "ColorManagementManager.h"
#include "ImagePhysicalSize.h"
#include "X33WhiteToneBuilder.h"
#include "RasterSpool.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <Magick++.h>

#include <algorithm>

class RipPipelineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void multiInkModeValidationFallsBackToFourColor();
    void toneBuilderRejectsMissingCmykInput();
    void toneBuilderBuildsTinySyntheticChannels();
    void x33FourColorLinearizationUsesSharedTransferCurves();
    void sourceAlphaMaskSuppressesHiddenColor();
    void x33WhiteToneBuilderSupportsJobStrategies();
    void screenEngineValidatesRequestsAndClampsParameters();
    void directPrintSdkFamiliesAreSeparated();
    void selectedPrinterPersistsWhenSetupIsSaved();
    void inputDensityPreservesPhysicalSize();
    void stripScreeningMatchesFullFrameAtAllTestHeights();
    void stripScreeningMatchesFullFrameWithPromotionAndSwapDisabled();
    void rasterSpoolDetectsBodyCorruption();
    void canceledSpoolNeverBecomesReady();
};

void RipPipelineTest::initTestCase()
{
    Magick::InitializeMagick(nullptr);
}

void RipPipelineTest::multiInkModeValidationFallsBackToFourColor()
{
    PrintJobMultiInk job;

    job.setInkMode(10);
    QCOMPARE(job.inkMode(), 10);

    job.setInkMode(9);
    QCOMPARE(job.inkMode(), 4);
}

void RipPipelineTest::toneBuilderRejectsMissingCmykInput()
{
    MultiInkToneBuilder::BuildRequest request;
    std::vector<std::vector<uint8_t>> tones;

    QVERIFY(!MultiInkToneBuilder::buildToneChannels(request, tones, {}));
    QVERIFY(tones.empty());
}

void RipPipelineTest::toneBuilderBuildsTinySyntheticChannels()
{
    std::array<Magick::Image, 4> cmyk = {
        Magick::Image(Magick::Geometry(2, 2), Magick::ColorGray(0.25)),
        Magick::Image(Magick::Geometry(2, 2), Magick::ColorGray(0.50)),
        Magick::Image(Magick::Geometry(2, 2), Magick::ColorGray(0.75)),
        Magick::Image(Magick::Geometry(2, 2), Magick::ColorGray(1.00)),
    };
    for (Magick::Image& image : cmyk)
        image.type(Magick::GrayscaleType);

    MultiInkToneBuilder::BuildRequest request;
    request.cmykImages = &cmyk;
    request.mode = MultiInkToneBuilder::InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V;
    request.modeParams = QVariantMap {
        { QStringLiteral("cLightStart"), -20 },
        { QStringLiteral("cLightEnd"), 999 },
        { QStringLiteral("mLightStart"), 200 },
        { QStringLiteral("mLightEnd"), 20 },
        { QStringLiteral("kT1Start"), -1 },
        { QStringLiteral("kT1End"), 300 },
        { QStringLiteral("kT2Start"), 300 },
        { QStringLiteral("kT2End"), -1 },
        { QStringLiteral("whiteMode"), 2 },
        { QStringLiteral("whiteDensity"), 300 },
        { QStringLiteral("varnishMode"), 3 },
        { QStringLiteral("varnishDensity"), 128 },
    };

    bool loaderCalled = false;
    std::vector<std::vector<uint8_t>> tones;
    QVERIFY(MultiInkToneBuilder::buildToneChannels(
        request,
        tones,
        [&](const QString&, std::vector<uint8_t>& outTone, int width, int height) {
            loaderCalled = true;
            outTone.assign(static_cast<size_t>(width * height), 64);
            return true;
        }));

    QCOMPARE(tones.size(), size_t(10));
    for (const std::vector<uint8_t>& channel : tones)
        QCOMPARE(channel.size(), size_t(4));
    QVERIFY(loaderCalled);
    QVERIFY(std::all_of(tones[8].begin(), tones[8].end(), [](uint8_t value) { return value == 255; }));
    QVERIFY(std::all_of(tones[9].begin(), tones[9].end(), [](uint8_t value) {
        return value > 0 && value <= 128;
    }));
}

void RipPipelineTest::x33FourColorLinearizationUsesSharedTransferCurves()
{
    QTemporaryFile xml;
    QVERIFY(xml.open());
    const QByteArray contents = R"xml(<?xml version="1.0"?>
<TransferCurveSet>
  <TransferCurve Separation="Cyan" Curve="0 0 1 0.5"/>
  <TransferCurve Separation="Magenta" Curve="0 0 1 0.25"/>
  <TransferCurve Separation="Yellow" Curve="0 0 1 0.75"/>
  <TransferCurve Separation="Black" Curve="0 0 1 0.125"/>
  <TransferCurve Separation="White" Curve="0 0 1 0.0625"/>
</TransferCurveSet>)xml";
    QCOMPARE(xml.write(contents), contents.size());
    QVERIFY(xml.flush());

    MultiInkLinearization linearization;
    QVERIFY(linearization.loadTransferCurveXml(xml.fileName()));
    QVERIFY(linearization.hasExternalCurves());

    std::array<std::vector<uint8_t>, 4> cmyk{{
        {0, 255},
        {0, 255},
        {0, 255},
        {0, 255},
    }};
    QVERIFY(linearization.applyFourColorTones(cmyk));
    QCOMPARE(cmyk[0], std::vector<uint8_t>({0, 128}));
    QCOMPARE(cmyk[1], std::vector<uint8_t>({0, 64}));
    QCOMPARE(cmyk[2], std::vector<uint8_t>({0, 191}));
    QCOMPARE(cmyk[3], std::vector<uint8_t>({0, 32}));

    std::vector<uint8_t> white{0, 255};
    QVERIFY(linearization.applyWhiteTone(white));
    QCOMPARE(white, std::vector<uint8_t>({0, 16}));

    std::array<std::vector<uint8_t>, 4> mismatched{{
        {0, 255}, {0}, {0, 255}, {0, 255}
    }};
    QVERIFY(!linearization.applyFourColorTones(mismatched));
}

void RipPipelineTest::sourceAlphaMaskSuppressesHiddenColor()
{
    const std::array<uint8_t, 8> rgba = {
        0, 0, 0, 0,
        20, 40, 60, 128,
    };
    Magick::Image source;
    source.read(2, 1, "RGBA", Magick::CharPixel, rgba.data());

    RasterAlphaMask mask;
    QVERIFY(mask.capture(source));
    QVERIFY(mask.isActive());
    QCOMPARE(mask.width(), 2);
    QCOMPARE(mask.height(), 1);

    std::vector<uint8_t> tone = {255, 200};
    QVERIFY(mask.applyTo(tone));
    QCOMPARE(tone[0], uint8_t(0));
    QCOMPARE(tone[1], uint8_t(100));

    std::array<uint8_t, 2> grayscaleBytes = {255, 200};
    Magick::Image grayscale(Magick::Geometry(2, 1), "black");
    grayscale.depth(8);
    grayscale.type(Magick::GrayscaleType);
    grayscale.read(2, 1, "I", Magick::CharPixel, grayscaleBytes.data());
    QVERIFY(mask.applyTo(grayscale));
    grayscale.write(0, 0, 2, 1,
                    "I", Magick::CharPixel, grayscaleBytes.data());
    QCOMPARE(grayscaleBytes[0], uint8_t(0));
    QCOMPARE(grayscaleBytes[1], uint8_t(100));

    QVERIFY(mask.resize(4, 2));
    QCOMPARE(mask.width(), 4);
    QCOMPARE(mask.height(), 2);
    std::vector<uint8_t> resizedTone(8, 255);
    QVERIFY(mask.applyTo(resizedTone));

    Magick::Image opaque(Magick::Geometry(2, 1), "black");
    opaque.type(Magick::TrueColorType);
    QVERIFY(mask.capture(opaque));
    QVERIFY(!mask.isActive());

    tone = {17, 93};
    QVERIFY(mask.applyTo(tone));
    QCOMPARE(tone, std::vector<uint8_t>({17, 93}));
}

void RipPipelineTest::x33WhiteToneBuilderSupportsJobStrategies()
{
    bool recognized = false;
    QCOMPARE(
        X33WhiteToneBuilder::modeFromJob(QStringLiteral("Auto Underbase"), &recognized),
        X33WhiteToneBuilder::Mode::AutoUnderbase);
    QVERIFY(recognized);
    QCOMPARE(
        X33WhiteToneBuilder::modeFromJob(QStringLiteral("White Plate"), &recognized),
        X33WhiteToneBuilder::Mode::Plate);
    QVERIFY(recognized);

    const std::array<std::vector<uint8_t>, 4> cmyk = {{
        {0, 10, 20, 30},
        {0, 40, 15, 10},
        {0, 20, 80, 20},
        {0, 30, 25, 120}
    }};

    X33WhiteToneBuilder::BuildRequest request;
    request.cmykTones = &cmyk;
    request.width = 2;
    request.height = 2;
    request.mode = X33WhiteToneBuilder::Mode::AutoUnderbase;
    request.threshold = 0;
    request.density = 255;

    std::vector<uint8_t> white;
    QVERIFY(X33WhiteToneBuilder::build(request, white));
    QCOMPARE(white, std::vector<uint8_t>({0, 40, 80, 120}));

    request.mode = X33WhiteToneBuilder::Mode::Flood;
    request.density = 123;
    QVERIFY(X33WhiteToneBuilder::build(request, white));
    QCOMPARE(white, std::vector<uint8_t>({123, 123, 123, 123}));

    request.mode = X33WhiteToneBuilder::Mode::Plate;
    request.density = 255;
    request.platePath = QStringLiteral("plate.png");
    QVERIFY(X33WhiteToneBuilder::build(
        request,
        white,
        [](const QString& path, std::vector<uint8_t>& plate, int width, int height) {
            if (path != QStringLiteral("plate.png") || width != 2 || height != 2)
                return false;
            plate = {0, 64, 128, 255};
            return true;
        }));
    QCOMPARE(white, std::vector<uint8_t>({0, 64, 128, 255}));

    QVERIFY(!X33WhiteToneBuilder::build(request, white));
}

void RipPipelineTest::screenEngineValidatesRequestsAndClampsParameters()
{
    MultiInkScreenEngine::AllPackedLines packed;
    MultiInkScreenEngine::ScreenRequest invalid;
    QVERIFY(!MultiInkScreenEngine::screenChannels(invalid, {}, packed));

    const std::vector<uint8_t> tone = {0, 32, 128, 255};
    MultiInkScreenEngine::ScreenRequest request;
    request.width = 2;
    request.height = 2;
    request.screenSeed = 1234;
    request.dotStrategy.minInkThreshold = -100;
    request.dotStrategy.smallDotThreshold = 999;
    request.dotStrategy.medDotThreshold = -20;
    request.dotStrategy.floorRangeCMY = 255;
    request.dotStrategy.floorMaxCMY = 255;
    request.modeParams.insert(QStringLiteral("useLightInkMinThresholdOverride"), true);
    request.modeParams.insert(QStringLiteral("lightInkMinThreshold"), 999);

    MultiInkScreenEngine::ChannelRequest channel;
    channel.maskKey = QStringLiteral("c");
    channel.toneBytes = &tone;
    channel.isLightInk = true;
    channel.useOwnDotStrategy = true;
    channel.ownSmallDotThreshold = -1;
    channel.ownMedDotThreshold = 300;
    request.channels.push_back(channel);

    QVERIFY(MultiInkScreenEngine::screenChannels(
        request,
        [](const QString&, std::vector<uint8_t>& maskRaw, int& maskW, int& maskH) {
            maskW = 2;
            maskH = 2;
            maskRaw = {0, 85, 170, 255};
            return true;
        },
        packed));

    QCOMPARE(packed.size(), size_t(1));
    QCOMPARE(packed[0].size(), size_t(2));
    QCOMPARE(packed[0][0].size(), size_t(4));
    QCOMPARE(packed[0][1].size(), size_t(4));
}

void RipPipelineTest::directPrintSdkFamiliesAreSeparated()
{
    ColorManagementManager manager;
    QCOMPARE(manager.directPrintSdkFamilyForPrinter(QStringLiteral("X-33")),
             QStringLiteral("legacy-cmyk"));
    QCOMPARE(manager.directPrintSdkFamilyForPrinter(QStringLiteral("X-36 Studio")),
             QStringLiteral("multi-ink"));
    QCOMPARE(manager.directPrintSdkFamilyForPrinter(QStringLiteral("X-36NC (Photo Printer)")),
             QStringLiteral("multi-ink"));
    QVERIFY(manager.directPrintSdkFamilyForPrinter(QStringLiteral("Unknown")).isEmpty());
}

void RipPipelineTest::selectedPrinterPersistsWhenSetupIsSaved()
{
    QStandardPaths::setTestModeEnabled(true);

    ColorManagementManager writer;
    writer.resetToDefaults();
    QCOMPARE(writer.directPrintSetting(QStringLiteral("eclosionGrade")).toInt(), 2);
    writer.setDirectPrintSetting(QStringLiteral("eclosionGrade"), 0);
    QCOMPARE(writer.directPrintSetting(QStringLiteral("eclosionGrade")).toInt(), 2);
    // Old saved selections migrate to the new display name.
    writer.setSelectedPrinter(QStringLiteral("X-36NC (Photo Printer)"));
    writer.setDirectPrintSetting(QStringLiteral("selectedPrinterIndex"), 3);
    writer.setDirectPrintSetting(QStringLiteral("selectedPrinterName"),
                                 QStringLiteral("Studio SDK Printer"));
    QVERIFY(writer.save());

    ColorManagementManager reader;
    QVERIFY(reader.load());
    QCOMPARE(reader.selectedPrinter(), QStringLiteral("X-36 Studio"));
    QCOMPARE(reader.directPrintSetting(QStringLiteral("selectedPrinterIndex")).toInt(), 3);
    QCOMPARE(reader.directPrintSetting(QStringLiteral("selectedPrinterName")).toString(),
             QStringLiteral("Studio SDK Printer"));

    reader.resetToDefaults();
    QVERIFY(reader.save());
}

void RipPipelineTest::inputDensityPreservesPhysicalSize()
{
    const ImagePhysicalSize::Density sourceDensity {152.4, 152.4, true};
    QCOMPARE(
        ImagePhysicalSize::outputPixelSize(
            2509, 1299, sourceDensity, 720, 1440),
        QSize(11854, 12274));
    QCOMPARE(
        ImagePhysicalSize::outputPixelSize(
            2509, 1299, sourceDensity, 720, 1200),
        QSize(11854, 10228));

    Magick::Image centimeters(Magick::Geometry(10, 10), "white");
    centimeters.resolutionUnits(Magick::PixelsPerCentimeterResolution);
    centimeters.density(Magick::Geometry(60, 60));
    const ImagePhysicalSize::Density converted =
        ImagePhysicalSize::embeddedDensity(centimeters);
    QVERIFY(converted.hasPhysicalUnits);
    QVERIFY(std::abs(converted.xDpi - 152.4) < 0.01);
    QVERIFY(std::abs(converted.yDpi - 152.4) < 0.01);

    Magick::Image unspecified(Magick::Geometry(10, 10), "white");
    unspecified.resolutionUnits(Magick::UndefinedResolution);
    const ImagePhysicalSize::Density legacyX33 =
        ImagePhysicalSize::resolvedDensity(unspecified, 720.0, 720.0);
    QVERIFY(!legacyX33.hasPhysicalUnits);
    QCOMPARE(legacyX33.xDpi, 720.0);
    QCOMPARE(legacyX33.yDpi, 720.0);

    const ImagePhysicalSize::Density legacyX36 =
        ImagePhysicalSize::resolvedDensity(unspecified, 600.0, 600.0);
    QVERIFY(!legacyX36.hasPhysicalUnits);
    QCOMPARE(legacyX36.xDpi, 600.0);
    QCOMPARE(legacyX36.yDpi, 600.0);
}

void RipPipelineTest::stripScreeningMatchesFullFrameAtAllTestHeights()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    constexpr int width = 13;
    constexpr int height = 143;
    constexpr int maskWidth = 11;
    constexpr int maskHeight = 9;
    std::vector<uint8_t> maskBytes(maskWidth * maskHeight);
    for (size_t index = 0; index < maskBytes.size(); ++index)
        maskBytes[index] = uint8_t((index * 53u + index / maskWidth * 17u) & 0xffu);

    const QString maskPath = directory.filePath(QStringLiteral("mask.tif"));
    Magick::Image mask;
    mask.read(maskWidth, maskHeight, "I", Magick::CharPixel, maskBytes.data());
    mask.type(Magick::GrayscaleType);
    mask.depth(8);
    mask.write(maskPath.toStdString());

    std::array<std::vector<uint8_t>, 2> sourceTones;
    for (int channel = 0; channel < int(sourceTones.size()); ++channel) {
        sourceTones[size_t(channel)].resize(width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                sourceTones[size_t(channel)][size_t(y * width + x)] =
                    uint8_t((x * 31 + y * 19 + channel * 47 + (x * y) % 23) & 0xff);
            }
        }
    }

    QVariantMap promotionParameters{
        {QStringLiteral("promoToneGate"), 91},
        {QStringLiteral("promoMedLo"), 14},
        {QStringLiteral("promoMedHi"), 29},
        {QStringLiteral("promoLrgLo"), 24},
        {QStringLiteral("promoLrgHi"), 39},
        {QStringLiteral("promoFlatVarEps"), 41},
        {QStringLiteral("promoMinNeiInked"), 6},
        {QStringLiteral("promoKickBonus"), 3},
    };

    MultiInkScreenEngine::ScreenRequest referenceRequest;
    referenceRequest.width = width;
    referenceRequest.height = height;
    referenceRequest.screenSeed = 0x1234abcd;
    referenceRequest.modeParams = promotionParameters;
    referenceRequest.dotStrategy.minInkThreshold = 7;
    referenceRequest.dotStrategy.smallDotThreshold = 99;
    referenceRequest.dotStrategy.medDotThreshold = 171;
    referenceRequest.dotStrategy.floorRangeCMY = 22;
    referenceRequest.dotStrategy.floorMaxCMY = 3;
    referenceRequest.dotStrategy.enableDotSwap = true;
    referenceRequest.dotStrategy.enablePromotion = true;
    for (int channel = 0; channel < int(sourceTones.size()); ++channel) {
        MultiInkScreenEngine::ChannelRequest request;
        request.maskKey = QStringLiteral("mask");
        request.toneBytes = &sourceTones[size_t(channel)];
        if (channel == 1) {
            request.useOwnDotStrategy = true;
            request.ownSmallDotThreshold = 113;
            request.ownMedDotThreshold = 188;
            request.ownEnablePromotion = true;
        }
        referenceRequest.channels.push_back(request);
    }

    MultiInkScreenEngine::AllPackedLines reference;
    QVERIFY(MultiInkScreenEngine::screenChannels(
        referenceRequest,
        [&](const QString& key, std::vector<uint8_t>& bytes, int& maskW, int& maskH) {
            if (key != QLatin1String("mask"))
                return false;
            Magick::Image loaded(maskPath.toStdString());
            maskW = int(loaded.columns());
            maskH = int(loaded.rows());
            bytes.resize(size_t(maskW * maskH));
            loaded.write(0, 0, maskW, maskH, "I", Magick::CharPixel, bytes.data());
            return true;
        },
        reference));

    std::vector<BoundedRasterPipeline::ScreenChannel> channels(2);
    for (auto& channel : channels) {
        channel.maskPath = maskPath;
        channel.minInkThreshold = 7;
        channel.smallDotThreshold = 99;
        channel.medDotThreshold = 171;
        channel.floorRange = 22;
        channel.floorMax = 3;
        channel.enableDotSwap = true;
        channel.enablePromotion = true;
        channel.useEffectiveTone = true;
    }
    channels[1].smallDotThreshold = 113;
    channels[1].medDotThreshold = 188;

    const int bytesPerLine = ((width + 3) / 4 + 3) & ~3;
    for (const int stripRows : {1, 17, 128}) {
        DirectPrintSpool metadata;
        metadata.logicalChannelCount = int(channels.size());
        metadata.channelOrder = {0, 1};
        metadata.width = width;
        metadata.height = height;
        metadata.xdpi = 720;
        metadata.ydpi = 1440;
        metadata.bytesPerLine = bytesPerLine;

        PrintFlowRasterSpool::Writer writer;
        QString error;
        QVERIFY2(writer.create(directory.path(), metadata, &error), qPrintable(error));
        const auto toneProvider = [&](
            int firstRow, int rowCount,
            std::vector<std::vector<uint8_t>>& tones,
            QString*) {
            tones.resize(sourceTones.size());
            const size_t first = size_t(firstRow * width);
            const size_t count = size_t(rowCount * width);
            for (size_t channel = 0; channel < sourceTones.size(); ++channel) {
                tones[channel].assign(sourceTones[channel].begin() + first,
                                      sourceTones[channel].begin() + first + count);
            }
            return true;
        };
        QVERIFY2(BoundedRasterPipeline::screenToSpool(
                     width, height, referenceRequest.screenSeed,
                     promotionParameters, channels, toneProvider, writer,
                     nullptr, {}, &error, stripRows),
                 qPrintable(error));

        DirectPrintSpool spool;
        QVERIFY2(writer.finalize(&spool, &error), qPrintable(error));
        PrintFlowRasterSpool::Reader reader;
        QVERIFY2(reader.open(spool.path, true, &error), qPrintable(error));
        for (int channel = 0; channel < int(channels.size()); ++channel) {
            for (int row = 0; row < height; ++row) {
                QByteArray actual;
                QVERIFY2(reader.readLine(channel, row, &actual, &error), qPrintable(error));
                const auto& expected = reference[size_t(channel)][size_t(row)];
                QCOMPARE(actual, QByteArray(
                    reinterpret_cast<const char*>(expected.data()),
                    qsizetype(expected.size())));
            }
        }
        PrintFlowRasterSpool::remove(spool);
    }
}

void RipPipelineTest::stripScreeningMatchesFullFrameWithPromotionAndSwapDisabled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    constexpr int width = 15;
    constexpr int height = 130;
    constexpr int maskWidth = 7;
    constexpr int maskHeight = 5;
    std::vector<uint8_t> maskBytes(maskWidth * maskHeight);
    for (size_t index = 0; index < maskBytes.size(); ++index)
        maskBytes[index] = uint8_t((index * 71u + 13u) & 0xffu);
    const QString maskPath = directory.filePath(QStringLiteral("mask-off.tif"));
    Magick::Image mask;
    mask.read(maskWidth, maskHeight, "I", Magick::CharPixel, maskBytes.data());
    mask.type(Magick::GrayscaleType);
    mask.depth(8);
    mask.write(maskPath.toStdString());

    std::vector<uint8_t> sourceTone(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            sourceTone[size_t(y * width + x)] = uint8_t((x * 43 + y * 29) & 0xff);
    }

    MultiInkScreenEngine::ScreenRequest referenceRequest;
    referenceRequest.width = width;
    referenceRequest.height = height;
    referenceRequest.screenSeed = 0x72a4u;
    referenceRequest.dotStrategy.enablePromotion = false;
    referenceRequest.dotStrategy.enableDotSwap = false;
    MultiInkScreenEngine::ChannelRequest referenceChannel;
    referenceChannel.maskKey = QStringLiteral("mask");
    referenceChannel.toneBytes = &sourceTone;
    referenceRequest.channels.push_back(referenceChannel);
    MultiInkScreenEngine::AllPackedLines reference;
    QVERIFY(MultiInkScreenEngine::screenChannels(
        referenceRequest,
        [&](const QString&, std::vector<uint8_t>& bytes, int& maskW, int& maskH) {
            bytes = maskBytes;
            maskW = maskWidth;
            maskH = maskHeight;
            return true;
        },
        reference));

    BoundedRasterPipeline::ScreenChannel channel;
    channel.maskPath = maskPath;
    channel.floorRange = referenceRequest.dotStrategy.floorRangeCMY;
    channel.floorMax = referenceRequest.dotStrategy.floorMaxCMY;
    channel.enablePromotion = false;
    channel.enableDotSwap = false;
    channel.useEffectiveTone = true;
    const int bytesPerLine = ((width + 3) / 4 + 3) & ~3;
    for (const int stripRows : {1, 17, 128}) {
        DirectPrintSpool metadata;
        metadata.logicalChannelCount = 1;
        metadata.channelOrder = {0};
        metadata.width = width;
        metadata.height = height;
        metadata.xdpi = 720;
        metadata.ydpi = 1440;
        metadata.bytesPerLine = bytesPerLine;
        PrintFlowRasterSpool::Writer writer;
        QString error;
        QVERIFY2(writer.create(directory.path(), metadata, &error), qPrintable(error));
        const auto provider = [&](int firstRow, int rowCount,
                                  std::vector<std::vector<uint8_t>>& tones,
                                  QString*) {
            const size_t first = size_t(firstRow * width);
            const size_t count = size_t(rowCount * width);
            tones = {std::vector<uint8_t>(sourceTone.begin() + first,
                                          sourceTone.begin() + first + count)};
            return true;
        };
        QVERIFY2(BoundedRasterPipeline::screenToSpool(
                     width, height, referenceRequest.screenSeed, {}, {channel},
                     provider, writer, nullptr, {}, &error, stripRows),
                 qPrintable(error));
        DirectPrintSpool spool;
        QVERIFY2(writer.finalize(&spool, &error), qPrintable(error));
        PrintFlowRasterSpool::Reader reader;
        QVERIFY2(reader.open(spool.path, true, &error), qPrintable(error));
        for (int row = 0; row < height; ++row) {
            QByteArray actual;
            QVERIFY2(reader.readLine(0, row, &actual, &error), qPrintable(error));
            const auto& expected = reference[0][size_t(row)];
            QCOMPARE(actual, QByteArray(reinterpret_cast<const char*>(expected.data()),
                                        qsizetype(expected.size())));
        }
        PrintFlowRasterSpool::remove(spool);
    }
}

void RipPipelineTest::rasterSpoolDetectsBodyCorruption()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DirectPrintSpool metadata;
    metadata.logicalChannelCount = 2;
    metadata.channelOrder = {1, 0, 1};
    metadata.width = 9;
    metadata.height = 3;
    metadata.xdpi = 720;
    metadata.ydpi = 1440;
    metadata.bytesPerLine = 4;

    PrintFlowRasterSpool::Writer writer;
    QString error;
    QVERIFY2(writer.create(directory.path(), metadata, &error), qPrintable(error));
    for (int channel = 0; channel < metadata.logicalChannelCount; ++channel) {
        for (int row = 0; row < metadata.height; ++row) {
            const QByteArray line(metadata.bytesPerLine, char(channel * 16 + row));
            QVERIFY2(writer.writeLine(channel, row,
                                      reinterpret_cast<const uint8_t*>(line.constData()),
                                      line.size(), &error),
                     qPrintable(error));
        }
    }
    DirectPrintSpool spool;
    QVERIFY2(writer.finalize(&spool, &error), qPrintable(error));
    QVERIFY2(PrintFlowRasterSpool::verify(spool, &error), qPrintable(error));

    QFile file(spool.path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    // xdpi is the first fixed-width field after magic, version, header size,
    // width, and height. Altering its low byte keeps metadata structurally
    // valid and confirms the checksum also covers the spool header.
    constexpr qint64 xDpiLowByteOffset = 25;
    QVERIFY(file.seek(xDpiLowByteOffset));
    char metadataByte = 0;
    QCOMPARE(file.read(&metadataByte, 1), qint64(1));
    QVERIFY(file.seek(xDpiLowByteOffset));
    const char changedMetadataByte = metadataByte ^ char(0x01);
    QCOMPARE(file.write(&changedMetadataByte, 1), qint64(1));
    file.flush();
    error.clear();
    QVERIFY(!PrintFlowRasterSpool::verify(spool, &error));
    QVERIFY(error.contains(QStringLiteral("checksum")));
    QVERIFY(file.seek(xDpiLowByteOffset));
    QCOMPARE(file.write(&metadataByte, 1), qint64(1));
    file.flush();
    error.clear();
    QVERIFY2(PrintFlowRasterSpool::verify(spool, &error), qPrintable(error));

    QVERIFY(file.seek(qint64(spool.bodyOffset + 1)));
    char byte = 0;
    QCOMPARE(file.read(&byte, 1), qint64(1));
    QVERIFY(file.seek(qint64(spool.bodyOffset + 1)));
    byte ^= char(0x40);
    QCOMPARE(file.write(&byte, 1), qint64(1));
    file.close();

    error.clear();
    QVERIFY(!PrintFlowRasterSpool::verify(spool, &error));
    QVERIFY(error.contains(QStringLiteral("checksum")));
    PrintFlowRasterSpool::remove(spool);
}

void RipPipelineTest::canceledSpoolNeverBecomesReady()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DirectPrintSpool metadata;
    metadata.logicalChannelCount = 1;
    metadata.channelOrder = {0};
    metadata.width = 4;
    metadata.height = 1;
    metadata.xdpi = 720;
    metadata.ydpi = 1440;
    metadata.bytesPerLine = 4;

    PrintFlowRasterSpool::Writer writer;
    QString error;
    QVERIFY2(writer.create(directory.path(), metadata, &error), qPrintable(error));
    const QByteArray line(4, '\x2a');
    QVERIFY(writer.writeLine(0, 0,
        reinterpret_cast<const uint8_t*>(line.constData()), line.size(), &error));
    const QString partialPath = writer.partialPath();
    QVERIFY(QFileInfo::exists(partialPath));
    DirectPrintSpool spool;
    QVERIFY(!writer.finalize(&spool, &error, []() { return true; }));
    QVERIFY(spool.path.isEmpty());
    writer.cancel();
    QVERIFY(!QFileInfo::exists(partialPath));
}

QTEST_GUILESS_MAIN(RipPipelineTest)
#include "RipPipelineTest.moc"
