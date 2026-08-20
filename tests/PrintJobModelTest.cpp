#include "PrintJobModel.h"

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

class PrintJobModelTest : public QObject
{
    Q_OBJECT

private slots:
    void createsEditsAndRemovesJobs();
    void persistsSelectedJobsToJson();
    void loadsJobsFromJson();
};

void PrintJobModelTest::createsEditsAndRemovesJobs()
{
    PrintJobModel model;
    QSignalSpy countSpy(&model, &PrintJobModel::countChanged);

    model.addJob(QStringLiteral("First job"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(countSpy.count(), 1);

    QVariantMap first = model.getJob(0);
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("First job"));
    QCOMPARE(first.value(QStringLiteral("paperSize")).toSize(), QSize(210, 297));
    QCOMPARE(first.value(QStringLiteral("resolution")).toSize(), QSize(720, 1440));
    QCOMPARE(first.value(QStringLiteral("mediaHeightMm")).toDouble(), -1.0);
    QCOMPARE(first.value(QStringLiteral("feathering")).toInt(), 2);

    QVariantMap update;
    update.insert(QStringLiteral("name"), QStringLiteral("Edited job"));
    update.insert(QStringLiteral("offset"), QPoint(12, 34));
    update.insert(QStringLiteral("whiteStrategy"), QStringLiteral("White Plate"));
    update.insert(QStringLiteral("varnishType"), QStringLiteral("Flood"));
    update.insert(QStringLiteral("feathering"), 3);
    model.updateJob(0, update);

    first = model.getJob(0);
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Edited job"));
    QCOMPARE(first.value(QStringLiteral("offset")).toPoint(), QPoint(12, 34));
    QCOMPARE(first.value(QStringLiteral("whiteStrategy")).toString(), QStringLiteral("White Plate"));
    QCOMPARE(first.value(QStringLiteral("varnishType")).toString(), QStringLiteral("Flood"));
    QCOMPARE(first.value(QStringLiteral("feathering")).toInt(), 3);

    model.removeJob(0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(countSpy.count(), 2);

    model.removeJob(42);
    QCOMPARE(model.rowCount(), 0);
}

void PrintJobModelTest::persistsSelectedJobsToJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    PrintJobModel model;
    model.addJob(QStringLiteral("A"));
    model.addJob(QStringLiteral("B"));
    model.updateJob(1, QVariantMap {
        { QStringLiteral("paperSize"), QSize(100, 200) },
        { QStringLiteral("resolution"), QSize(600, 1200) },
        { QStringLiteral("colorProfile"), QStringLiteral("Test CMYK") },
        { QStringLiteral("mediaHeightMm"), 12.3 },
        { QStringLiteral("feathering"), 1 },
    });

    const QString jsonPath = dir.filePath(QStringLiteral("jobs.json"));
    model.saveToJson(QUrl::fromLocalFile(jsonPath).toString(), {1});

    QFile file(jsonPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 1);

    const QJsonObject obj = doc.array().first().toObject();
    QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("B"));
    QCOMPARE(obj.value(QStringLiteral("paperSizeWidth")).toInt(), 100);
    QCOMPARE(obj.value(QStringLiteral("resolutionHeight")).toInt(), 1200);
    QCOMPARE(obj.value(QStringLiteral("colorProfile")).toString(), QStringLiteral("Test CMYK"));
    QCOMPARE(obj.value(QStringLiteral("mediaHeightMm")).toDouble(), 12.3);
    QCOMPARE(obj.value(QStringLiteral("feathering")).toInt(), 1);
}

void PrintJobModelTest::loadsJobsFromJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString jsonPath = dir.filePath(QStringLiteral("jobs.json"));
    QJsonArray jobs;
    jobs.append(QJsonObject {
        { QStringLiteral("id"), QStringLiteral("job-1") },
        { QStringLiteral("name"), QStringLiteral("Loaded") },
        { QStringLiteral("imagePath"), QStringLiteral("file:///tmp/input.png") },
        { QStringLiteral("paperSizeWidth"), 320 },
        { QStringLiteral("paperSizeHeight"), 240 },
        { QStringLiteral("resolutionWidth"), 720 },
        { QStringLiteral("resolutionHeight"), 600 },
        { QStringLiteral("mediaHeightMm"), 7.5 },
        { QStringLiteral("offsetX"), 5 },
        { QStringLiteral("offsetY"), 6 },
        { QStringLiteral("whiteStrategy"), QStringLiteral("None") },
        { QStringLiteral("varnishType"), QStringLiteral("None") },
        { QStringLiteral("colorProfile"), QStringLiteral("sRGB") },
        { QStringLiteral("createdAt"), QStringLiteral("2026-06-17T12:00:00") },
    });

    QFile file(jsonPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(jobs).toJson());
    file.close();

    PrintJobModel model;
    model.loadFromJson(QUrl::fromLocalFile(jsonPath).toString());

    QCOMPARE(model.rowCount(), 1);
    const QVariantMap job = model.getJob(0);
    QCOMPARE(job.value(QStringLiteral("id")).toString(), QStringLiteral("job-1"));
    QCOMPARE(job.value(QStringLiteral("name")).toString(), QStringLiteral("Loaded"));
    QCOMPARE(job.value(QStringLiteral("paperSize")).toSize(), QSize(320, 240));
    QCOMPARE(job.value(QStringLiteral("offset")).toPoint(), QPoint(5, 6));
    QCOMPARE(job.value(QStringLiteral("mediaHeightMm")).toDouble(), 7.5);
    // Older saved jobs did not contain this field and must migrate to Medium.
    QCOMPARE(job.value(QStringLiteral("feathering")).toInt(), 2);
    QVERIFY(job.value(QStringLiteral("createdAt")).toDateTime().isValid());
}

QTEST_GUILESS_MAIN(PrintJobModelTest)
#include "PrintJobModelTest.moc"
