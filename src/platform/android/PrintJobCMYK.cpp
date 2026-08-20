#include "PrintJobCMYK.h"

#include <QDebug>
#include <QDir>

namespace {
bool unavailable(const char* operation)
{
    qWarning() << operation << "requires the Android RIP dependency build.";
    return false;
}
}

PrintJobCMYK::PrintJobCMYK(QObject* parent)
    : QObject(parent)
{
}

void PrintJobCMYK::setColorManager(ColorManagementManager* mgr)
{
    m_colorManager = mgr;
}

void PrintJobCMYK::setDirectPrintClient(IPrintOutputClient* client)
{
    Q_UNUSED(client)
}

void PrintJobCMYK::runPRNGeneration(const QVariantMap& jobMap, const QString& outputPath)
{
    Q_UNUSED(jobMap)
    Q_UNUSED(outputPath)
    unavailable("CMYK PRN generation");
    emit prnGenerationFinished(false);
}

void PrintJobCMYK::runDirectPrint(const QVariantMap& jobMap)
{
    Q_UNUSED(jobMap)
    unavailable("CMYK direct print");
    emit prnGenerationFinished(false);
}

void PrintJobCMYK::cancelOutput() {}

bool PrintJobCMYK::loadInputImage(const QString& imagePath) { Q_UNUSED(imagePath) return unavailable("Image loading for PRN"); }
bool PrintJobCMYK::applyICCConversion(const QString& inputProfile, const QString& outputProfile) { Q_UNUSED(inputProfile) Q_UNUSED(outputProfile) return unavailable("ICC conversion for PRN"); }
bool PrintJobCMYK::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi) { Q_UNUSED(outputPath) Q_UNUSED(xdpi) Q_UNUSED(ydpi) return unavailable("Final PRN generation"); }

void PrintJobCMYK::prepareAssets()
{
    if (!m_profiles.isEmpty())
        return;

    addICCProfile(QStringLiteral("Default - Plain Paper (1440DPI)"), QStringLiteral(":/assets/RIP_App_1440_Plain_Default.icc"));
    addICCProfile(QStringLiteral("Neutral Profile - Plain Paper (1440DPI)"), QStringLiteral(":/assets/RIP_App_1440_Plain_Neutral.icc"));
    addICCProfile(QStringLiteral("sRGB Input"), QStringLiteral(":/assets/sRGBProfile.icm"));
    addICCProfile(QStringLiteral("CMYK Input"), QStringLiteral(":/assets/RIP_App_Generic_CMYK.icc"));
    if (m_defaultOutputICCPath.isEmpty())
        m_defaultOutputICCPath = QStringLiteral(":/assets/RIP_App_1440_Plain_Default.icc");
    if (m_defaultInputCMYKPath.isEmpty())
        m_defaultInputCMYKPath = QStringLiteral(":/assets/RIP_App_Generic_CMYK.icc");
}

void PrintJobCMYK::cleanupTemporaryFiles(const QString& baseName, const QString& workingDir)
{
    Q_UNUSED(baseName)
    Q_UNUSED(workingDir)
}

void PrintJobCMYK::cleanupRuntimeAssets()
{
}

QVariantList PrintJobCMYK::getAvailableICCProfiles() const { return m_profiles; }
QString PrintJobCMYK::getDefaultOutputICCProfile() const { return m_defaultOutputICCPath; }
QString PrintJobCMYK::getDefaultInputCMYKProfile() const { return m_defaultInputCMYKPath; }
void PrintJobCMYK::setDefaultOutputICCProfile(const QString& outputProfile) { m_defaultOutputICCPath = outputProfile; }
void PrintJobCMYK::setDefaultInputCMYKProfile(const QString& inputProfilePath) { m_defaultInputCMYKPath = inputProfilePath; }
void PrintJobCMYK::enableDefaultInputCMYK(bool enabled) { m_useDefaultInputCMYK = enabled; }
bool PrintJobCMYK::checkDefaultInputCMYK() const { return m_useDefaultInputCMYK; }

void PrintJobCMYK::addICCProfile(const QString& name, const QString& path)
{
    QVariantMap profile;
    profile["name"] = name;
    profile["path"] = path;
    m_profiles.append(profile);
}

void PrintJobCMYK::setDotStrategy(int minInkThreshold,
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
