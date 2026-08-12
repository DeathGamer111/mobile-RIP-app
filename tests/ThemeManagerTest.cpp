#include "ThemeManager.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>

class ThemeManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void builtInThemesLoad_data();
    void builtInThemesLoad();
    void xanteThemeUsesIQueueProcessPalette();
    void selectedThemeFallsBackToDefaultForMalformedRuntimeTheme();
    void loadFromPathReportsMalformedTheme();
};

void ThemeManagerTest::builtInThemesLoad_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("appName");

    QTest::newRow("default") << QStringLiteral(":/themes/default/theme.json") << QStringLiteral("default") << QStringLiteral("PrintFlow");
    QTest::newRow("nocai") << QStringLiteral(":/themes/nocai/theme.json") << QStringLiteral("nocai") << QStringLiteral("Nocai PrintFlow");
    QTest::newRow("xante") << QStringLiteral(":/themes/xante/theme.json") << QStringLiteral("xante") << QStringLiteral("iQueue PrintFlow");
}

void ThemeManagerTest::builtInThemesLoad()
{
    QFETCH(QString, path);
    QFETCH(QString, id);
    QFETCH(QString, appName);

    ThemeManager manager;
    QVERIFY2(manager.loadFromPath(path), qPrintable(manager.lastError()));
    QCOMPARE(manager.id(), id);
    QCOMPARE(manager.appName(), appName);
    QVERIFY(manager.primaryColor().isValid());
    QVERIFY(manager.textColor().isValid());
    QVERIFY(!manager.logoPath().isEmpty());
}

void ThemeManagerTest::xanteThemeUsesIQueueProcessPalette()
{
    ThemeManager manager;
    QVERIFY2(manager.loadFromPath(QStringLiteral(":/themes/xante/theme.json")),
             qPrintable(manager.lastError()));

    // Match the process-color constants used by iQueue: cyan, magenta,
    // process black, and white. The remaining surfaces are deliberately
    // low-chroma tints so controls remain readable in both appearance modes.
    QCOMPARE(manager.primaryColor(), QColor(QStringLiteral("#00AEEF")));
    QCOMPARE(manager.secondaryColor(), QColor(QStringLiteral("#EC008C")));
    QCOMPARE(manager.accentColor(), QColor(QStringLiteral("#00AEEF")));

    QCOMPARE(manager.backgroundColor(), QColor(QStringLiteral("#171415")));
    QCOMPARE(manager.surfaceColor(), QColor(QStringLiteral("#231F1F")));
    QCOMPARE(manager.textColor(), QColor(QStringLiteral("#FFFFFF")));
    QCOMPARE(manager.subtextColor(), QColor(QStringLiteral("#D1D3D4")));

    QCOMPARE(manager.lightBackgroundColor(), QColor(QStringLiteral("#EAF7FB")));
    QCOMPARE(manager.lightSurfaceColor(), QColor(QStringLiteral("#FFFFFF")));
    QCOMPARE(manager.lightSurface2Color(), QColor(QStringLiteral("#DDF2F9")));
    QCOMPARE(manager.lightTextColor(), QColor(QStringLiteral("#231F1F")));
}

void ThemeManagerTest::selectedThemeFallsBackToDefaultForMalformedRuntimeTheme()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString badPath = dir.filePath(QStringLiteral("bad-theme.json"));
    QFile file(badPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"id\":\"broken\"}");
    file.close();

    qputenv("RIP_THEME_FILE", badPath.toUtf8());
    ThemeManager manager;
    QVERIFY(manager.loadSelectedTheme());
    QCOMPARE(manager.id(), QStringLiteral("default"));
    QVERIFY(manager.lastError().isEmpty());
    qunsetenv("RIP_THEME_FILE");
}

void ThemeManagerTest::loadFromPathReportsMalformedTheme()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString badPath = dir.filePath(QStringLiteral("bad-theme.json"));
    QFile file(badPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{");
    file.close();

    ThemeManager manager;
    QVERIFY(!manager.loadFromPath(badPath));
    QCOMPARE(manager.id(), QStringLiteral("default"));
    QVERIFY(manager.lastError().contains(QStringLiteral("Invalid theme JSON")));
}

QTEST_GUILESS_MAIN(ThemeManagerTest)
#include "ThemeManagerTest.moc"
