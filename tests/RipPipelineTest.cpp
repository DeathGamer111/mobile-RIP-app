#include "MultiInkScreenEngine.h"
#include "MultiInkToneBuilder.h"
#include "PrintJobMultiInk.h"
#include "ColorManagementManager.h"
#include "X33WhiteToneBuilder.h"

#include <QtTest/QtTest>
#include <QStandardPaths>

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
    void x33WhiteToneBuilderSupportsJobStrategies();
    void screenEngineValidatesRequestsAndClampsParameters();
    void directPrintSdkFamiliesAreSeparated();
    void selectedPrinterPersistsWhenSetupIsSaved();
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
    QCOMPARE(manager.directPrintSdkFamilyForPrinter(QStringLiteral("X-36NC (Photo Printer)")),
             QStringLiteral("multi-ink"));
    QVERIFY(manager.directPrintSdkFamilyForPrinter(QStringLiteral("Unknown")).isEmpty());
}

void RipPipelineTest::selectedPrinterPersistsWhenSetupIsSaved()
{
    QStandardPaths::setTestModeEnabled(true);

    ColorManagementManager writer;
    writer.resetToDefaults();
    writer.setSelectedPrinter(QStringLiteral("X-36NC (Photo Printer)"));
    QVERIFY(writer.save());

    ColorManagementManager reader;
    QVERIFY(reader.load());
    QCOMPARE(reader.selectedPrinter(), QStringLiteral("X-36NC (Photo Printer)"));

    reader.resetToDefaults();
    QVERIFY(reader.save());
}

QTEST_GUILESS_MAIN(RipPipelineTest)
#include "RipPipelineTest.moc"
