#include "PrintJobMultiInk.h"

#include <QDebug>

namespace {
bool unavailable(const char* operation)
{
    qWarning() << operation << "requires the Android RIP dependency build.";
    return false;
}
}

PrintJobMultiInk::PrintJobMultiInk(QObject* parent)
    : QObject(parent)
{
}

void PrintJobMultiInk::runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath)
{
    Q_UNUSED(jobMap)
    Q_UNUSED(outputPath)
    unavailable("Multi-ink PRN generation");
    emit prnGenerationFinished(false);
}

void PrintJobMultiInk::runDirectPrint(const QVariantMap& jobMap)
{
    Q_UNUSED(jobMap)
    if (m_directPrintClient && m_directPrintClient->isAvailable())
        qWarning() << "Direct-print SDK is available, but Android raster generation is not enabled yet.";
    else
        qWarning() << "Direct-print SDK unavailable:" << (m_directPrintClient ? m_directPrintClient->lastError() : QStringLiteral("client not configured"));
    emit prnGenerationFinished(false);
}

void PrintJobMultiInk::cancelOutput() {}

void PrintJobMultiInk::setColorManager(ColorManagementManager* mgr) { m_colorManager = mgr; }
void PrintJobMultiInk::setDirectPrintClient(IPrintOutputClient* client) { m_directPrintClient = client; }

bool PrintJobMultiInk::loadInputImage(const QString& imagePath) { Q_UNUSED(imagePath) return unavailable("Multi-ink image loading"); }
bool PrintJobMultiInk::applyICCConversion(const QString& inputProfile, const QString& outputProfile) { Q_UNUSED(inputProfile) Q_UNUSED(outputProfile) return unavailable("Multi-ink ICC conversion"); }
bool PrintJobMultiInk::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi) { Q_UNUSED(outputPath) Q_UNUSED(xdpi) Q_UNUSED(ydpi) return unavailable("Multi-ink final PRN generation"); }

void PrintJobMultiInk::setInkMode(int mode)
{
    switch (mode) {
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
        m_inkMode = mode;
        break;
    default:
        m_inkMode = 4;
        break;
    }
}

int PrintJobMultiInk::inkMode() const { return m_inkMode; }

bool PrintJobMultiInk::prepareAssets()
{
    if (m_profiles.isEmpty()) {
        addICCProfile(QStringLiteral("PrintFlow 1200 4C"), QStringLiteral(":/assets/RIP_App_1200_4C.icc"));
        addICCProfile(QStringLiteral("PrintFlow 1200 8C/10C"), QStringLiteral(":/assets/RIP_App_1200_8C.icc"));
        addICCProfile(QStringLiteral("sRGB Input"), QStringLiteral(":/assets/sRGBProfile.icm"));
        addICCProfile(QStringLiteral("CMYK Input"), QStringLiteral(":/assets/RIP_App_Generic_CMYK.icc"));
    }
    if (m_defaultOutputICCPath.isEmpty())
        m_defaultOutputICCPath = (m_inkMode <= 5)
            ? QStringLiteral(":/assets/RIP_App_1200_4C.icc")
            : QStringLiteral(":/assets/RIP_App_1200_8C.icc");
    if (m_defaultInputCMYKPath.isEmpty())
        m_defaultInputCMYKPath = QStringLiteral(":/assets/RIP_App_Generic_CMYK.icc");
    return true;
}

bool PrintJobMultiInk::cleanupAssets() { return true; }
void PrintJobMultiInk::cleanupTemporaryFiles(const QString& baseName, const QString& workingDir) { Q_UNUSED(baseName) Q_UNUSED(workingDir) }

void PrintJobMultiInk::enableDeviceLink(bool enabled) { m_useDeviceLink = enabled; }
bool PrintJobMultiInk::isDeviceLinkEnabled() const { return m_useDeviceLink; }
void PrintJobMultiInk::setDefaultDeviceLinkProfile(const QString& path) { m_defaultDeviceLinkPath = path; }
QString PrintJobMultiInk::getDefaultDeviceLinkProfile() const { return m_defaultDeviceLinkPath; }

void PrintJobMultiInk::addDeviceLinkProfile(const QString& name, const QString& path)
{
    QVariantMap profile;
    profile["name"] = name;
    profile["path"] = path;
    m_deviceLinks.append(profile);
}

QVariantList PrintJobMultiInk::getAvailableDeviceLinkProfiles() const { return m_deviceLinks; }
QVariantList PrintJobMultiInk::getAvailableICCProfiles() const { return m_profiles; }
QString PrintJobMultiInk::getDefaultOutputICCProfile() const { return m_defaultOutputICCPath; }
QString PrintJobMultiInk::getDefaultInputCMYKProfile() const { return m_defaultInputCMYKPath; }
void PrintJobMultiInk::setDefaultOutputICCProfile(const QString& outputProfile) { m_defaultOutputICCPath = outputProfile; }
void PrintJobMultiInk::setDefaultInputCMYKProfile(const QString& inputProfilePath) { m_defaultInputCMYKPath = inputProfilePath; }
void PrintJobMultiInk::enableDefaultInputCMYK(bool enabled) { m_useDefaultInputCMYK = enabled; }
bool PrintJobMultiInk::checkDefaultInputCMYK() const { return m_useDefaultInputCMYK; }

void PrintJobMultiInk::addICCProfile(const QString& name, const QString& path)
{
    QVariantMap profile;
    profile["name"] = name;
    profile["path"] = path;
    m_profiles.append(profile);
}

void PrintJobMultiInk::setDotStrategy(int minInkThreshold,
                                      int smallDotThreshold,
                                      int medDotThreshold,
                                      bool enablePromotion,
                                      uint8_t floorRangeCMY,
                                      uint8_t floorMaxCMY,
                                      uint8_t floorRangeK,
                                      uint8_t floorMaxK,
                                      bool enableDotSwap)
{
    Q_UNUSED(minInkThreshold)
    Q_UNUSED(smallDotThreshold)
    Q_UNUSED(medDotThreshold)
    Q_UNUSED(enablePromotion)
    Q_UNUSED(floorRangeCMY)
    Q_UNUSED(floorMaxCMY)
    Q_UNUSED(floorRangeK)
    Q_UNUSED(floorMaxK)
    Q_UNUSED(enableDotSwap)
}

void PrintJobMultiInk::enableLinearization(bool enabled) { m_enableLinearization = enabled; }
bool PrintJobMultiInk::isLinearizationEnabled() const { return m_enableLinearization; }
