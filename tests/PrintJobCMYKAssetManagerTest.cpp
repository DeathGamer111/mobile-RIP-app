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
