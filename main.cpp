#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPalette>
#include <QStyleFactory>

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

    QApplication app(argc, argv);
    
     // Force Fusion style
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Optional: Set dark palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    app.setPalette(darkPalette);
    app.setStyleSheet("QToolTip { color: white; background-color: #2a82da; border: 1px solid white; padding: 6px; font-size: 14px; }");
    
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
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
