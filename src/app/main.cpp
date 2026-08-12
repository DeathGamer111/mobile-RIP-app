#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QImageReader>
#include <QIcon>
#include <QLockFile>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>

#include "PrintJobModel.h"
#include "AppStrings.h"
#include "ImageLoader.h"
#include "PrintJobOutput.h"
#include "PrintJobCMYK.h"
#include "PrintJobMultiInk.h"
#if defined(Q_OS_ANDROID)
#include "NocaiDirectPrintClient.h"
#else
#include "PrinterServiceClient.h"
#endif
#include "ImageEditor.h"
#include "ColorProfile.h"
#include "ColorManagementManager.h"
#include "ImageImportManager.h"
#include "PlatformCapabilities.h"
#include "ThemeManager.h"

#include <QQuickStyle>
#include <QQuickWindow>

#if defined(Q_OS_LINUX)
#include <csignal>
#if !defined(NDEBUG)
#include <execinfo.h>
#include <unistd.h>
#endif
#endif

namespace {
#if defined(Q_OS_LINUX)
volatile std::sig_atomic_t g_terminationRequested = 0;

void gracefulTerminationSignalHandler(int)
{
    // Assigning sig_atomic_t is async-signal-safe. The Qt event loop observes
    // it below and performs orderly object destruction outside signal context.
    g_terminationRequested = 1;
}

void installGracefulTerminationHandlers()
{
    struct sigaction action = {};
    action.sa_handler = gracefulTerminationSignalHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}
#endif

#if defined(Q_OS_LINUX) && !defined(NDEBUG)
void crashBacktraceHandler(int signalNumber)
{
    static constexpr char message[] =
        "\nPrintFlow: fatal signal in native code; backtrace follows:\n";
    ::write(STDERR_FILENO, message, sizeof(message) - 1);
    void* frames[64] = {};
    const int frameCount = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
    _exit(128 + signalNumber);
}

void installCrashBacktraceHandler()
{
    struct sigaction action = {};
    action.sa_handler = crashBacktraceHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
}
#endif

void migrateLegacyAppData()
{
    const QString newRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString oldRoot = QDir::home().absoluteFilePath(QStringLiteral(".local/share/appRIPPrinterApp"));

    if (newRoot.isEmpty() || !QDir(oldRoot).exists() || QDir(newRoot).exists())
        return;

    QDir().mkpath(QFileInfo(newRoot).absolutePath());
    if (!QDir().rename(oldRoot, newRoot)) {
        qWarning() << "PrintFlow: unable to migrate legacy app data from" << oldRoot << "to" << newRoot;
    }
}
}

/* Entry point for the RIP application.
 * - Sets a consistent Fusion style with a dark palette.
 * - Creates backend singletons and exposes them to QML.
 * - Sets an image allocation cap to avoid runaway memory use.
 * - Loads the main QML and starts the event loop.
 */
int main(int argc, char *argv[]) {

#if defined(Q_OS_LINUX) && !defined(NDEBUG)
    installCrashBacktraceHandler();
#endif

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PrintFlow"));
    app.setDesktopFileName(QStringLiteral("PrintFlow"));

#if defined(Q_OS_LINUX)
    installGracefulTerminationHandlers();
    QTimer terminationPoll;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app]() {
        if (g_terminationRequested != 0)
            app.quit();
    });
    terminationPoll.start(100);
#endif

    // Keep one desktop UI per user so duplicate windows cannot submit
    // conflicting jobs. Linux SDK ownership is enforced independently by the
    // persistent printer service, which may serve additional trusted clients.
    QString runtimeDirectory = QStandardPaths::writableLocation(
        QStandardPaths::RuntimeLocation);
    if (runtimeDirectory.isEmpty())
        runtimeDirectory = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
    QDir().mkpath(runtimeDirectory);
    QLockFile instanceLock(
        QDir(runtimeDirectory).absoluteFilePath(QStringLiteral("printflow.lock")));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock()) {
        qCritical() << "PrintFlow is already running; refusing to start a second GUI instance.";
        return 2;
    }

    migrateLegacyAppData();

    ThemeManager themeManager;
    themeManager.loadSelectedTheme();
    app.setWindowIcon(QIcon(themeManager.logoPath().startsWith(QStringLiteral("qrc:/"))
        ? QString(themeManager.logoPath()).replace(QStringLiteral("qrc:/"), QStringLiteral(":/"))
        : themeManager.logoPath()));
  
    // Optional: set Material theme + accent via env vars
    qputenv("QT_QUICK_CONTROLS_MATERIAL_PRIMARY", themeManager.primaryColor().name().toUtf8());
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", themeManager.accentColor().name().toUtf8());
    QQuickStyle::setStyle("Material");
    
    QQmlApplicationEngine engine;
    
    // Backend components (owned by main; lifetime = entire app).
    PrintJobModel jobModel;
    AppStrings appStrings;
    ImageLoader imageLoader;
    ImageEditor imageEditor;
    PrintJobOutput printJobOutput;
    PrintJobCMYK printJobCMYKOutput;
#if defined(Q_OS_ANDROID)
    NocaiDirectPrintClient nocaiDirectPrint;
#else
    PrinterServiceClient nocaiDirectPrint;
#endif
    PrintJobMultiInk printJobMultiInk;
    ColorProfile colorProfile;
    ColorManagementManager colorManager;
    ImageImportManager imageImportManager;
    PlatformCapabilities platformCapabilities;

    // Expose C++ objects to QML (context properties for convenient access).
    engine.rootContext()->setContextProperty("jobModel", &jobModel);
    engine.rootContext()->setContextProperty("strings", &appStrings);
    engine.rootContext()->setContextProperty("imageLoader", &imageLoader);
    engine.rootContext()->setContextProperty("imageEditor", &imageEditor);
    engine.rootContext()->setContextProperty("printJobOutput", &printJobOutput);
    engine.rootContext()->setContextProperty("printJobCMYK", &printJobCMYKOutput);
    engine.rootContext()->setContextProperty("nocaiDirectPrint", &nocaiDirectPrint);
    engine.rootContext()->setContextProperty("printJobMultiInk", &printJobMultiInk);
    engine.rootContext()->setContextProperty("colorProfile", &colorProfile);
    engine.rootContext()->setContextProperty("colorManager", &colorManager);
    engine.rootContext()->setContextProperty("imageImportManager", &imageImportManager);
    engine.rootContext()->setContextProperty("platformCapabilities", &platformCapabilities);
    engine.rootContext()->setContextProperty("themeManager", &themeManager);
    
    // load persisted settings early
    colorManager.load();

    // bind shared ColorManager to both backends
    printJobMultiInk.setColorManager(&colorManager);
    printJobMultiInk.setDirectPrintClient(&nocaiDirectPrint);
    printJobCMYKOutput.setColorManager(&colorManager);
    printJobCMYKOutput.setDirectPrintClient(&nocaiDirectPrint);
    
    // Cap decode allocations to reduce OOM risk with very large images (MB).
    // Set to 0 to disable the guard (not recommended).
    QImageReader::setAllocationLimit(1024);

    // Load QML UI and verify a root object was created.
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
