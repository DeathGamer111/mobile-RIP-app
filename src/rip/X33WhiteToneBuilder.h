#pragma once

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Builds the one logical white tone plane used by the legacy X-33 raster
// path. The X-33 SDK receives this plane twice (YMCKWW); that physical-plane
// duplication is deliberately handled by the PRN/direct-print layer.
class X33WhiteToneBuilder
{
public:
    enum class Mode {
        Off = 0,
        AutoUnderbase = 1,
        Flood = 2,
        Plate = 3
    };

    struct BuildRequest {
        const std::array<std::vector<uint8_t>, 4>* cmykTones = nullptr;
        int width = 0;
        int height = 0;
        Mode mode = Mode::Off;
        int threshold = 8;
        int density = 255;
        QString platePath;
    };

    using ExternalPlateLoader = std::function<bool(
        const QString& platePath,
        std::vector<uint8_t>& outTone,
        int width,
        int height)>;

    static Mode modeFromJob(const QString& strategy, bool* recognized = nullptr);

    static bool build(
        const BuildRequest& request,
        std::vector<uint8_t>& whiteTone,
        const ExternalPlateLoader& plateLoader = {});
};
