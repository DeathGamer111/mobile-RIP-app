#pragma once

#include <Magick++.h>

#include <cstdint>
#include <vector>

// Keeps source transparency independent from ICC/DeviceLink conversions, which
// may legitimately replace the working image with an opaque CMYK image.
class RasterAlphaMask
{
public:
    void reset();
    bool capture(Magick::Image& source);
    bool resize(int width, int height);

    bool isActive() const noexcept;
    int width() const noexcept;
    int height() const noexcept;

    bool applyTo(std::vector<uint8_t>& tone) const;
    bool applyTo(Magick::Image& grayscaleTone) const;

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<uint8_t> m_alpha;
};
