#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "PrintJobModel.h"
#include "ImageLoader.h"
#include "PrintJobOutput.h"
#include "PrintJobNocai.h"
#include "ImageEditor.h"
#include "ColorProfile.h"


/****************************************************************************
    Entry point for the RIP application.
        - Initializes the QML engine, registers C++ backend objects for QML,
        and starts the event loop.
****************************************************************************/
int main(int argc, char *argv[]) {

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // Instantiate core backend components
    PrintJobModel jobModel;
    ImageLoader imageLoader;
    ImageEditor imageEditor;
    PrintJobOutput printJobOutput;
    PrintJobNocai printJobNocaiOutput;
    ColorProfile colorProfile;

    // Expose C++ objects to QML context
    engine.rootContext()->setContextProperty("jobModel", &jobModel);
    engine.rootContext()->setContextProperty("imageLoader", &imageLoader);
    engine.rootContext()->setContextProperty("imageEditor", &imageEditor);
    engine.rootContext()->setContextProperty("printJobOutput", &printJobOutput);
    engine.rootContext()->setContextProperty("printJobNocai", &printJobNocaiOutput);
    engine.rootContext()->setContextProperty("colorProfile", &colorProfile);

    // Load the main QML UI
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
