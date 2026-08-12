#pragma once

#include <QString>

#include <array>
#include <cstdint>
#include <vector>

enum class DirectPrintRasterFormat : uint8_t
{
    Unspecified = 0,
    NocaiX33Standard = 1
};

struct DirectPrintSettings
{
    int printerIndex = -1;
    int printDirection = 0;
    int printSpeed = 1;
    int wcSequence = 0;
    int eclosionGrade = 0;
    // CPrinter_Model_X33 initializes as the generic two-head configuration,
    // which its legacy settings translation maps to selection 0.
    int headSelect = 0;
    int whiteInkPercent = 0;
    int whiteInkPassCount = 0;
    int varnishInkPercent = 0;
    int varnishInkPassCount = 0;
    int headVoltage = 512;
    int disableUv0 = 0;
    int disableUv1 = 0;
    int disableUv2 = 0;
    int disableUv3 = 0;
    int disableUv4 = 0;
    int disableUv5 = 0;
    int carReset = 0;
    int stripBlank = 0;
    int blankDistance = 0;
    // Per-job print origin in whole millimeters. Job Details and Imposition
    // both persist this same job offset. The X-33 adapter converts it to the
    // controller's uint32 hundredths-of-a-millimeter representation and must
    // restore the controller's persistent origin to 0,0 after the job.
    int printOffsetXmm = 0;
    int printOffsetYmm = 0;
    double mediaHeightMm = -1.0;
    int pass = 0;
    int vsdMode = 0;
};

struct DirectPrintRaster
{
    const std::vector<std::vector<std::vector<uint8_t>>>* packedLines = nullptr;
    std::vector<int> channelOrder;
    int width = 0;
    int height = 0;
    int xdpi = 0;
    int ydpi = 0;
    int bytesPerLine = 0;
    DirectPrintRasterFormat format = DirectPrintRasterFormat::Unspecified;
    // Populated only for NocaiX33Standard. These are the exact 48 bytes that
    // the working legacy PRN writer would place before the same packed lines.
    std::array<uint32_t, 12> canonicalHeader{};
};

class IPrintOutputClient
{
public:
    virtual ~IPrintOutputClient() = default;

    virtual bool isAvailable() = 0;
    virtual QString vendorName() const = 0;
    virtual QString lastError() const = 0;
    virtual bool submitPreparedJob(const DirectPrintRaster& raster,
                                   const DirectPrintSettings& settings) = 0;
};
