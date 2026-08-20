#include "PrintJobCMYK.h"
#include "BoundedRasterPipeline.h"
#include "ColorManagementManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class PrintJobCMYKAssetManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void preparesBundledProfilesIntoRuntimePath();
    void inMemoryAndBoundedRastersMatch();
    void onyxEvaluationImageStaysBounded();
};

void PrintJobCMYKAssetManagerTest::preparesBundledProfilesIntoRuntimePath()
{
    QTemporaryDir dataHome;
    QVERIFY(dataHome.isValid());

    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());

    ColorManagementManager colorManager;
    PrintJobCMYK printJob;
    printJob.setColorManager(&colorManager);
    printJob.prepareAssets();

    const QString outputProfile = printJob.getDefaultOutputICCProfile();
    const QString inputProfile = printJob.getDefaultInputCMYKProfile();
    QVERIFY(!outputProfile.isEmpty());
    QVERIFY(!inputProfile.isEmpty());
    QVERIFY(QFileInfo::exists(outputProfile));
    QVERIFY(QFileInfo::exists(inputProfile));
    QCOMPARE(colorManager.printerFamilyOutputProfile("X-33", "A"),
             outputProfile);
    QCOMPARE(colorManager.defaultInputProfile(), inputProfile);
    const QString linearization =
        colorManager.printerFamilyLinearizationPath("X-33", "A");
    QVERIFY(linearization.endsWith(
        QStringLiteral("RIP_App_Linearization_1440_X-33.xml")));
    QVERIFY(QFileInfo::exists(linearization));

    QFile packagedProfile(QStringLiteral(":/assets/RIP_App_1440_Plain_Default.icc"));
    QFile stagedProfile(outputProfile);
    QVERIFY(packagedProfile.open(QIODevice::ReadOnly));
    QVERIFY(stagedProfile.open(QIODevice::ReadOnly));
    QCOMPARE(stagedProfile.readAll(), packagedProfile.readAll());
    stagedProfile.close();

    QVERIFY(stagedProfile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(stagedProfile.write("stale"), qint64(5));
    stagedProfile.close();
    PrintJobCMYK refreshedPrintJob;
    refreshedPrintJob.setColorManager(&colorManager);
    refreshedPrintJob.prepareAssets();
    QVERIFY(stagedProfile.open(QIODevice::ReadOnly));
    packagedProfile.seek(0);
    QCOMPARE(stagedProfile.readAll(), packagedProfile.readAll());

    ColorManagementManager lateBoundManager;
    PrintJobCMYK lateBoundPrintJob;
    lateBoundPrintJob.prepareAssets();
    lateBoundPrintJob.setColorManager(&lateBoundManager);
    QCOMPARE(lateBoundManager.printerFamilyOutputProfile("X-33", "A"),
             outputProfile);
    QCOMPARE(lateBoundManager.printerFamilyLinearizationPath("X-33", "A"),
             linearization);
    QCOMPARE(lateBoundManager.defaultInputProfile(), inputProfile);

    // Persisted paths that refer to a bundled default by name are migrated to
    // the staged runtime copy so dropdown selection and packaged installs use
    // the same canonical path.
    ColorManagementManager migratedManager;
    const QString sourceTreeProfile = QDir::current().absoluteFilePath(
        QStringLiteral("resources/assets/RIP_App_1440_Plain_Default.icc"));
    const QString sourceTreeInput = QDir::current().absoluteFilePath(
        QStringLiteral("resources/assets/RIP_App_Generic_CMYK.icc"));
    const QString legacyLinearization = dataHome.filePath(
        QStringLiteral("X-33_Linearization_v1.xml"));
    QFile legacyFile(legacyLinearization);
    QVERIFY(legacyFile.open(QIODevice::WriteOnly));
    QVERIFY(legacyFile.write("<Legacy/>") > 0);
    legacyFile.close();
    migratedManager.setPrinterFamilyOutputProfile(
        QStringLiteral("X-33"), QStringLiteral("A"), sourceTreeProfile);
    migratedManager.setDefaultInputProfile(sourceTreeInput);
    migratedManager.setPrinterFamilyLinearizationPath(
        QStringLiteral("X-33"), QStringLiteral("A"), legacyLinearization);
    PrintJobCMYK migratedPrintJob;
    migratedPrintJob.setColorManager(&migratedManager);
    migratedPrintJob.prepareAssets();
    QCOMPARE(migratedManager.printerFamilyOutputProfile("X-33", "A"),
             migratedPrintJob.getDefaultOutputICCProfile());
    QCOMPARE(migratedManager.defaultInputProfile(),
             migratedPrintJob.getDefaultInputCMYKProfile());
    QCOMPARE(migratedManager.printerFamilyLinearizationPath("X-33", "A"),
             QFileInfo(migratedPrintJob.getDefaultOutputICCProfile()).dir()
                 .filePath(QStringLiteral(
                     "RIP_App_Linearization_1440_X-33.xml")));

    const QVariantList profiles = printJob.getAvailableICCProfiles();
    QVERIFY(profiles.size() >= 4);

    bool sawSrgb = false;
    bool sawCmyk = false;
    for (const QVariant& value : profiles) {
        const QVariantMap profile = value.toMap();
        const QString path = profile.value(QStringLiteral("path")).toString();
        if (path.endsWith(QStringLiteral("sRGBProfile.icm")) && QFileInfo::exists(path))
            sawSrgb = true;
        if (path.endsWith(QStringLiteral("RIP_App_Generic_CMYK.icc")) && QFileInfo::exists(path))
            sawCmyk = true;
    }

    QVERIFY(sawSrgb);
    QVERIFY(sawCmyk);

    refreshedPrintJob.cleanupRuntimeAssets();
    QVERIFY(!QFileInfo::exists(outputProfile));
    QVERIFY(!QFileInfo::exists(inputProfile));
}

void PrintJobCMYKAssetManagerTest::inMemoryAndBoundedRastersMatch()
{
    QTemporaryDir dataHome;
    QVERIFY(dataHome.isValid());
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());

    constexpr int width = 37;
    constexpr int height = 29;
    std::vector<uint8_t> pixels(size_t(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t base = size_t(y * width + x) * 4;
            pixels[base] = uint8_t((x * 17 + y * 3) & 0xff);
            pixels[base + 1] = uint8_t((x * 5 + y * 19) & 0xff);
            pixels[base + 2] = uint8_t((x * 11 + y * 7) & 0xff);
            pixels[base + 3] = uint8_t((x * y + y * 13) & 0xff);
        }
    }
    const QString inputPath = dataHome.filePath(QStringLiteral("equivalence.tif"));
    Magick::Image image;
    image.read(width, height, "CMYK", Magick::CharPixel, pixels.data());
    image.depth(8);
    image.colorSpace(Magick::CMYKColorspace);
    image.type(Magick::ColorSeparationType);
    image.resolutionUnits(Magick::PixelsPerInchResolution);
    image.density(Magick::Geometry(720, 720));
    image.write(inputPath.toStdString());

    ColorManagementManager manager;
    PrintJobCMYK memoryJob;
    memoryJob.setColorManager(&manager);
    memoryJob.prepareAssets();
    const QString assetDirectory =
        QFileInfo(memoryJob.getDefaultOutputICCProfile()).absolutePath();
    const QDir sourceMasks(QStringLiteral(PRINTFLOW_TEST_MASK_DIRECTORY));
    for (const QString& channel : {QStringLiteral("c"), QStringLiteral("m"),
                                   QStringLiteral("y"), QStringLiteral("k")}) {
        const QString source = sourceMasks.filePath(
            QStringLiteral("mask_%1.tiff").arg(channel));
        const QString destination = QDir(assetDirectory).filePath(
            QStringLiteral("mask_512_%1.tiff").arg(channel));
        QVERIFY2(QFileInfo::exists(source), qPrintable(source));
        QVERIFY2(QFile::copy(source, destination), qPrintable(destination));
    }

    const QString memoryPath = dataHome.filePath(QStringLiteral("memory.prn"));
    qputenv("PRINTFLOW_FORCE_RASTER_STRATEGY", "memory");
    QVERIFY(memoryJob.loadInputImage(QUrl::fromLocalFile(inputPath).toString()));
    QVERIFY(memoryJob.generateFinalPRN(memoryPath, 720, 720));

    PrintJobCMYK boundedJob;
    boundedJob.setColorManager(&manager);
    boundedJob.prepareAssets();
    const QString boundedPath = dataHome.filePath(QStringLiteral("bounded.prn"));
    qputenv("PRINTFLOW_FORCE_RASTER_STRATEGY", "bounded");
    QVERIFY(boundedJob.loadInputImage(QUrl::fromLocalFile(inputPath).toString()));
    QVERIFY(boundedJob.generateFinalPRN(boundedPath, 720, 720));
    qunsetenv("PRINTFLOW_FORCE_RASTER_STRATEGY");

    QFile memoryFile(memoryPath);
    QFile boundedFile(boundedPath);
    QVERIFY(memoryFile.open(QIODevice::ReadOnly));
    QVERIFY(boundedFile.open(QIODevice::ReadOnly));
    QCOMPARE(memoryFile.readAll(), boundedFile.readAll());
}

void PrintJobCMYKAssetManagerTest::onyxEvaluationImageStaysBounded()
{
    const QString inputPath = qEnvironmentVariable("PRINTFLOW_ONYX_EVAL_IMAGE");
    if (inputPath.isEmpty())
        QSKIP("Set PRINTFLOW_ONYX_EVAL_IMAGE to run the large-raster acceptance test.");
    QVERIFY2(QFileInfo::exists(inputPath), qPrintable(inputPath));

    QTemporaryDir dataHome;
    QVERIFY(dataHome.isValid());
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());

    BoundedRasterPipeline::configureImageMagickCache();
    PrintJobCMYK printJob;
    printJob.prepareAssets();

    const QString assetDirectory =
        QFileInfo(printJob.getDefaultOutputICCProfile()).absolutePath();
    const QDir sourceMasks(QStringLiteral(PRINTFLOW_TEST_MASK_DIRECTORY));
    for (const QString& channel : {QStringLiteral("c"), QStringLiteral("m"),
                                   QStringLiteral("y"), QStringLiteral("k")}) {
        const QString source = sourceMasks.filePath(
            QStringLiteral("mask_%1.tiff").arg(channel));
        const QString destination = QDir(assetDirectory).filePath(
            QStringLiteral("mask_512_%1.tiff").arg(channel));
        QVERIFY2(QFileInfo::exists(source), qPrintable(source));
        QVERIFY2(QFile::copy(source, destination), qPrintable(destination));
    }

    printJob.enableDefaultInputCMYK(false);
    QVERIFY(printJob.loadInputImage(QUrl::fromLocalFile(inputPath).toString()));
    // Keep the destination in the populated data-home tree. Some CI/runtime
    // temp janitors remove empty QTemporaryDir folders while this deliberately
    // long test is still rasterizing.
    const QString outputPath = dataHome.filePath(QStringLiteral("onyx-x33.prn"));
    QVERIFY(printJob.generateFinalPRN(outputPath, 720, 1440));
    QVERIFY(QFileInfo(outputPath).size() > 4096);

    printJob.cleanupRuntimeAssets();
}

QTEST_GUILESS_MAIN(PrintJobCMYKAssetManagerTest)
#include "PrintJobCMYKAssetManagerTest.moc"
