#include "X33WhiteToneBuilder.h"

#include <algorithm>
#include <limits>

namespace {
uint8_t applyThresholdAndDensity(uint8_t source, int threshold, int density)
{
    const int clampedThreshold = std::clamp(threshold, 0, 255);
    const int clampedDensity = std::clamp(density, 0, 255);
    if (clampedDensity == 0 || source <= clampedThreshold || clampedThreshold >= 255)
        return 0;

    const int normalized = (static_cast<int>(source) - clampedThreshold) * 255
        / (255 - clampedThreshold);
    return static_cast<uint8_t>(normalized * clampedDensity / 255);
}
}

X33WhiteToneBuilder::Mode X33WhiteToneBuilder::modeFromJob(
    const QString& strategy, bool* recognized)
{
    const QString value = strategy.trimmed().toLower();
    if (recognized)
        *recognized = true;

    if (value.isEmpty() || value == QStringLiteral("off") ||
        value == QStringLiteral("none")) {
        return Mode::Off;
    }
    if (value == QStringLiteral("auto underbase") ||
        value == QStringLiteral("auto")) {
        return Mode::AutoUnderbase;
    }
    if (value == QStringLiteral("flood"))
        return Mode::Flood;
    if (value == QStringLiteral("plate") ||
        value == QStringLiteral("white plate")) {
        return Mode::Plate;
    }

    if (recognized)
        *recognized = false;
    return Mode::Off;
}

bool X33WhiteToneBuilder::build(
    const BuildRequest& request,
    std::vector<uint8_t>& whiteTone,
    const ExternalPlateLoader& plateLoader)
{
    whiteTone.clear();
    if (request.width <= 0 || request.height <= 0)
        return false;

    const uint64_t pixelCount64 = static_cast<uint64_t>(request.width)
        * static_cast<uint64_t>(request.height);
    if (pixelCount64 > std::numeric_limits<size_t>::max())
        return false;
    const size_t pixelCount = static_cast<size_t>(pixelCount64);

    if (request.mode == Mode::Off) {
        whiteTone.assign(pixelCount, 0);
        return true;
    }

    if (!request.cmykTones)
        return false;
    for (const std::vector<uint8_t>& tone : *request.cmykTones) {
        if (tone.size() != pixelCount)
            return false;
    }

    const int density = std::clamp(request.density, 0, 255);
    if (request.mode == Mode::Flood) {
        whiteTone.assign(pixelCount, static_cast<uint8_t>(density));
        return true;
    }

    std::vector<uint8_t> candidate(pixelCount, 0);
    if (request.mode == Mode::AutoUnderbase) {
        const auto& cmyk = *request.cmykTones;
        for (size_t index = 0; index < pixelCount; ++index) {
            candidate[index] = std::max({
                cmyk[0][index], cmyk[1][index],
                cmyk[2][index], cmyk[3][index]
            });
        }
    } else if (request.mode == Mode::Plate) {
        if (!plateLoader ||
            !plateLoader(request.platePath, candidate, request.width, request.height) ||
            candidate.size() != pixelCount) {
            return false;
        }
    } else {
        return false;
    }

    whiteTone.resize(pixelCount);
    for (size_t index = 0; index < pixelCount; ++index) {
        whiteTone[index] = applyThresholdAndDensity(
            candidate[index], request.threshold, density);
    }
    return true;
}
