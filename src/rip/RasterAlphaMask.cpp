#include "RasterAlphaMask.h"

#include <limits>
#include <string>

namespace {

bool pixelCountFor(int width, int height, size_t& pixelCount)
{
    if (width <= 0 || height <= 0)
        return false;

    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h)
        return false;

    pixelCount = w * h;
    return true;
}

} // namespace

void RasterAlphaMask::reset()
{
    m_width = 0;
    m_height = 0;
    m_alpha.clear();
}

bool RasterAlphaMask::capture(Magick::Image& source)
{
    reset();

    // Images without an alpha channel bypass the mask completely. This keeps
    // their existing CMYK values byte-for-byte unchanged.
    if (!source.matte())
        return true;

    const int sourceWidth = static_cast<int>(source.columns());
    const int sourceHeight = static_cast<int>(source.rows());
    size_t pixelCount = 0;
    if (!pixelCountFor(sourceWidth, sourceHeight, pixelCount))
        return false;

    try {
        std::vector<uint8_t> alpha(pixelCount);
        source.write(0, 0, sourceWidth, sourceHeight,
                     "A", Magick::CharPixel, alpha.data());

        m_width = sourceWidth;
        m_height = sourceHeight;
        m_alpha = std::move(alpha);
        return true;
    } catch (const Magick::Exception&) {
        reset();
        return false;
    }
}

bool RasterAlphaMask::resize(int width, int height)
{
    if (!isActive())
        return true;
    if (width == m_width && height == m_height)
        return true;

    size_t pixelCount = 0;
    if (!pixelCountFor(width, height, pixelCount))
        return false;

    try {
        Magick::Image mask(Magick::Geometry(m_width, m_height), "black");
        mask.depth(8);
        mask.type(Magick::GrayscaleType);
        mask.read(m_width, m_height, "I", Magick::CharPixel, m_alpha.data());

        const std::string geometry = std::to_string(width) + "x"
            + std::to_string(height) + "!";
        mask.resize(Magick::Geometry(geometry));

        std::vector<uint8_t> resized(pixelCount);
        mask.write(0, 0, width, height,
                   "I", Magick::CharPixel, resized.data());

        m_width = width;
        m_height = height;
        m_alpha = std::move(resized);
        return true;
    } catch (const Magick::Exception&) {
        return false;
    }
}

bool RasterAlphaMask::isActive() const noexcept
{
    return !m_alpha.empty();
}

int RasterAlphaMask::width() const noexcept
{
    return m_width;
}

int RasterAlphaMask::height() const noexcept
{
    return m_height;
}

bool RasterAlphaMask::applyTo(std::vector<uint8_t>& tone) const
{
    if (!isActive())
        return true;
    if (tone.size() != m_alpha.size())
        return false;

    for (size_t i = 0; i < tone.size(); ++i) {
        // Rounded straight-alpha multiplication. Fully opaque pixels remain
        // exact; fully transparent pixels can never emit CMYK ink.
        tone[i] = static_cast<uint8_t>(
            (static_cast<uint16_t>(tone[i]) * m_alpha[i] + 127u) / 255u);
    }
    return true;
}

bool RasterAlphaMask::applyTo(Magick::Image& grayscaleTone) const
{
    if (!isActive())
        return true;
    if (static_cast<int>(grayscaleTone.columns()) != m_width
        || static_cast<int>(grayscaleTone.rows()) != m_height) {
        return false;
    }

    try {
        std::vector<uint8_t> tone(m_alpha.size());
        grayscaleTone.write(0, 0, m_width, m_height,
                            "I", Magick::CharPixel, tone.data());
        if (!applyTo(tone))
            return false;
        grayscaleTone.read(m_width, m_height,
                           "I", Magick::CharPixel, tone.data());
        return true;
    } catch (const Magick::Exception&) {
        return false;
    }
}
