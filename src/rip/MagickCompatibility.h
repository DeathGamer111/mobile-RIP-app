#pragma once

#include <Magick++.h>

#include <string>

namespace MagickCompatibility {

inline bool hasAlphaChannel(const Magick::Image& image)
{
#if MagickLibVersion >= 0x700
    return image.alpha();
#else
    return image.matte();
#endif
}

inline void setAlphaChannel(Magick::Image& image, bool enabled)
{
#if MagickLibVersion >= 0x700
    image.alpha(enabled);
#else
    image.matte(enabled);
#endif
}

inline void setDensity(Magick::Image& image, double xDpi, double yDpi)
{
#if MagickLibVersion >= 0x700
    image.density(Magick::Point(xDpi, yDpi));
#else
    image.density(Magick::Geometry(
        std::to_string(xDpi) + "x" + std::to_string(yDpi)));
#endif
}

} // namespace MagickCompatibility
