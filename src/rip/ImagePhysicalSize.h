#pragma once

#include <Magick++.h>

#include "MagickCompatibility.h"

#include <QFileInfo>
#include <QSize>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ImagePhysicalSize {

struct Density {
    double xDpi = 0.0;
    double yDpi = 0.0;
    bool hasPhysicalUnits = false;
};

inline bool isUsableDpi(double value)
{
    return std::isfinite(value) && value >= 10.0 && value <= 9600.0;
}

inline Density embeddedDensity(const Magick::Image& image)
{
    double xDpi = image.xResolution();
    double yDpi = image.yResolution();

    const Magick::ResolutionType units = image.resolutionUnits();
    if (units == Magick::PixelsPerCentimeterResolution) {
        xDpi *= 2.54;
        yDpi *= 2.54;
    } else if (units != Magick::PixelsPerInchResolution) {
        return {};
    }

    const bool validX = isUsableDpi(xDpi);
    const bool validY = isUsableDpi(yDpi);
    if (!validX && !validY)
        return {};
    if (!validX)
        xDpi = yDpi;
    if (!validY)
        yDpi = xDpi;

    return {xDpi, yDpi, true};
}

inline Density resolvedDensity(const Magick::Image& image,
                               double fallbackXDpi,
                               double fallbackYDpi)
{
    const Density embedded = embeddedDensity(image);
    if (embedded.hasPhysicalUnits)
        return embedded;

    const double safeX = isUsableDpi(fallbackXDpi) ? fallbackXDpi : 720.0;
    const double safeY = isUsableDpi(fallbackYDpi) ? fallbackYDpi : safeX;
    return {safeX, safeY, false};
}

inline QSize outputPixelSize(size_t inputWidth,
                             size_t inputHeight,
                             const Density& inputDensity,
                             int outputXDpi,
                             int outputYDpi)
{
    if (inputWidth == 0 || inputHeight == 0 ||
        !isUsableDpi(inputDensity.xDpi) ||
        !isUsableDpi(inputDensity.yDpi) ||
        outputXDpi <= 0 || outputYDpi <= 0) {
        return {};
    }

    const auto scaledDimension = [](size_t pixels, double scale) {
        const double scaled = std::round(static_cast<double>(pixels) * scale);
        return static_cast<int>(std::clamp(
            scaled, 1.0, static_cast<double>(std::numeric_limits<int>::max())));
    };

    return QSize(
        scaledDimension(inputWidth, static_cast<double>(outputXDpi) / inputDensity.xDpi),
        scaledDimension(inputHeight, static_cast<double>(outputYDpi) / inputDensity.yDpi));
}

inline bool isVectorDocument(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("pdf") || suffix == QLatin1String("svg");
}

inline void setVectorReadDensity(Magick::Image& image,
                                 const QString& path,
                                 int xDpi,
                                 int yDpi)
{
    if (!isVectorDocument(path) || xDpi <= 0 || yDpi <= 0)
        return;

    image.resolutionUnits(Magick::PixelsPerInchResolution);
    MagickCompatibility::setDensity(image, xDpi, yDpi);
}

} // namespace ImagePhysicalSize
