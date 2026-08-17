#include "AppStrings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

class AppStringsTest : public QObject
{
    Q_OBJECT

private slots:
    void stringsJsonLoads();
    void knownKeysResolve();
    void localizedKeySetsMatch();
    void missingKeysUseVisibleFallback();
};

void AppStringsTest::stringsJsonLoads()
{
    qputenv("PRINTFLOW_LANGUAGE", "en");
    AppStrings strings;
    QCOMPARE(strings.language(), QStringLiteral("en"));
    QVERIFY(strings.availableLanguages().size() >= 2);
}

void AppStringsTest::knownKeysResolve()
{
    AppStrings strings;
    QVERIFY(strings.hasKey(QStringLiteral("app.title")));
    QCOMPARE(strings.trKey(QStringLiteral("app.title")), QStringLiteral("PrintFlow"));
}

void AppStringsTest::localizedKeySetsMatch()
{
    auto loadObject = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object();
    };

    const QJsonObject english = loadObject(QStringLiteral(":/i18n/strings.json"));
    const QJsonObject simplifiedChinese =
        loadObject(QStringLiteral(":/i18n/strings.zh-Hans.json"));

    QVERIFY(!english.isEmpty());
    QVERIFY(!simplifiedChinese.isEmpty());
    QCOMPARE(simplifiedChinese.keys(), english.keys());
}

void AppStringsTest::missingKeysUseVisibleFallback()
{
    AppStrings strings;
    const QString missing = strings.trKey(QStringLiteral("missing.test.key"));
    QCOMPARE(missing, QStringLiteral("[[missing.test.key]]"));
}

QTEST_GUILESS_MAIN(AppStringsTest)
#include "AppStringsTest.moc"
