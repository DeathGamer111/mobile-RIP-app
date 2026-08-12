#pragma once

#include <Magick++.h>

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

} // namespace MagickCompatibility
