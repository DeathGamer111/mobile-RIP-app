#pragma once

#include <QString>

#include <array>
#include <cstdint>
#include <vector>

class NocaiPrnWriter
{
public:
    using StandardX33Header = std::array<uint32_t, 12>;
    using StandardCmykHeader = StandardX33Header;

    enum class MultiInkMode {
        FourColorYMCK = 4,
        FiveColorYMCKW = 5,
        SixColorYMCKLmLc = 6,
        SevenColorYMCKLmLcW = 7,
        EightColorYMCKLmLcLkLLk = 8,
        TenColorYMCKLmLcLkLLkWV = 10
    };

    static std::vector<std::vector<uint8_t>> packTo2Bpp(
        const std::vector<std::vector<uint8_t>>& dotMap,
        int width,
        int height);

    // Canonical 48-byte header used by the legacy X-33 PRN format. Keep file
    // output and direct SDK submission sourced from this one definition.
    static StandardCmykHeader makeStandardCmykHeader(
        int width,
        int height,
        int xdpi,
        int ydpi,
        int bytesPerLine);

    static StandardX33Header makeStandardX33Header(
        int width,
        int height,
        int xdpi,
        int ydpi,
        int bytesPerLine,
        int colors);

    static bool writeStandardCmykPrn(
        const std::vector<std::vector<std::vector<uint8_t>>>& packedLines,
        const std::vector<int>& channelOrder,
        int width,
        int height,
        int xdpi,
        int ydpi,
        const QString& outputPath);

    static bool writeStandardX33Prn(
        const std::vector<std::vector<std::vector<uint8_t>>>& packedLines,
        const std::vector<int>& channelOrder,
        int width,
        int height,
        int xdpi,
        int ydpi,
        const QString& outputPath);

    static bool writeMultiInkPrn(
        const std::vector<std::vector<std::vector<uint8_t>>>& packedLines,
        const std::vector<int>& channelOrder,
        MultiInkMode mode,
        int width,
        int height,
        int xdpi,
        int ydpi,
        int bytesPerLine,
        const QString& outputPath);
};
