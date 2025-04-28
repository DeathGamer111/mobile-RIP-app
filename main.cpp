#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "PrintJobModel.h"
#include "ImageLoader.h"
#include "PrintJobOutput.h"
#include "ImageEditor.h"

int main(int argc, char *argv[]) {

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    PrintJobModel jobModel;
    ImageLoader imageLoader;
    ImageEditor imageEditor;
    PrintJobOutput printJobOutput;

    engine.rootContext()->setContextProperty("jobModel", &jobModel);
    engine.rootContext()->setContextProperty("imageLoader", &imageLoader);
    engine.rootContext()->setContextProperty("imageEditor", &imageEditor);
    engine.rootContext()->setContextProperty("printJobOutput", &printJobOutput);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
