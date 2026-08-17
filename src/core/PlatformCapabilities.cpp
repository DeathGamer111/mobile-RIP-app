#include "PlatformCapabilities.h"

PlatformCapabilities::PlatformCapabilities(QObject* parent)
    : QObject(parent)
{
}

QString PlatformCapabilities::platformName() const
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("desktop");
#endif
}

bool PlatformCapabilities::supportsCupsPrinting() const
{
#if defined(Q_OS_ANDROID)
    return false;
#else
    return true;
#endif
}

bool PlatformCapabilities::supportsRipProcessing() const
{
#if defined(Q_OS_ANDROID) && !defined(RIP_ANDROID_ENABLE_RIP_PROCESSING)
    return false;
#else
    return true;
#endif
}

bool PlatformCapabilities::supportsDirectPrint() const
{
#if defined(Q_OS_ANDROID) && !defined(RIP_DIRECT_PRINT_SDK_BUNDLED) && \
    !defined(RIP_ANDROID_PRINTER_BRIDGE)
    return false;
#else
    return true;
#endif
}
