#include "RasterAlphaMask.h"
#include "MagickCompatibility.h"
#include "RasterSpool.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <algorithm>
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
    m_mask.reset();
}

bool RasterAlphaMask::capture(Magick::Image& source,
                              std::atomic_bool* canceled)
{
    reset();

    // Images without an alpha channel bypass the mask completely. This keeps
    // their existing CMYK values byte-for-byte unchanged.
    if (!MagickCompatibility::hasAlphaChannel(source))
        return true;

    const int sourceWidth = static_cast<int>(source.columns());
    const int sourceHeight = static_cast<int>(source.rows());
    size_t pixelCount = 0;
    if (!pixelCountFor(sourceWidth, sourceHeight, pixelCount))
        return false;

    try {
        // Export alpha in bounded rows before importing it into a disk-backed
        // ImageMagick grayscale cache. This preserves the legacy 8-bit alpha
        // quantization without allocating one native byte per source pixel.
        QTemporaryFile rawAlpha(QDir(PrintFlowRasterSpool::scratchDirectory())
            .filePath(QStringLiteral("alpha-XXXXXX.gray8.partial")));
        rawAlpha.setAutoRemove(true);
        if (!rawAlpha.open())
            return false;
        constexpr int stripRows = 128;
        for (int firstRow = 0; firstRow < sourceHeight; firstRow += stripRows) {
            if (canceled && canceled->load(std::memory_order_relaxed))
                return false;
            const int rowCount = std::min(stripRows, sourceHeight - firstRow);
            std::vector<uint8_t> alpha(
                static_cast<size_t>(sourceWidth) * size_t(rowCount));
            source.write(0, firstRow, sourceWidth, rowCount,
                         "A", Magick::CharPixel, alpha.data());
            const qint64 byteCount = qint64(alpha.size());
            if (rawAlpha.write(
                    reinterpret_cast<const char*>(alpha.data()), byteCount)
                != byteCount) {
                return false;
            }
        }
        rawAlpha.flush();
        rawAlpha.close();

        auto mask = std::make_unique<Magick::Image>();
        mask->size(Magick::Geometry(sourceWidth, sourceHeight));
        mask->depth(8);
        mask->colorSpace(Magick::GRAYColorspace);
        mask->type(Magick::GrayscaleType);
        mask->read((QStringLiteral("GRAY:") + rawAlpha.fileName()).toStdString());
        m_width = sourceWidth;
        m_height = sourceHeight;
        m_mask = std::move(mask);
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
        const std::string geometry = std::to_string(width) + "x"
            + std::to_string(height) + "!";
        m_mask->resize(Magick::Geometry(geometry));

        m_width = width;
        m_height = height;
        return true;
    } catch (const Magick::Exception&) {
        return false;
    }
}

bool RasterAlphaMask::isActive() const noexcept
{
    return bool(m_mask);
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
    size_t pixelCount = 0;
    if (!pixelCountFor(m_width, m_height, pixelCount) ||
        tone.size() != pixelCount)
        return false;
    return applyToRows(0, m_height, tone);
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
        size_t pixelCount = 0;
        if (!pixelCountFor(m_width, m_height, pixelCount))
            return false;
        std::vector<uint8_t> tone(pixelCount);
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

bool RasterAlphaMask::readRows(
    int firstRow, int rowCount, std::vector<uint8_t>& alpha) const
{
    alpha.clear();
    if (!isActive())
        return true;
    if (firstRow < 0 || rowCount < 0 || firstRow + rowCount > m_height)
        return false;
    const size_t count = static_cast<size_t>(m_width) * rowCount;
    try {
        alpha.resize(count);
        m_mask->write(0, firstRow, m_width, rowCount,
                      "I", Magick::CharPixel, alpha.data());
        return true;
    } catch (const Magick::Exception&) {
        alpha.clear();
        return false;
    }
}

bool RasterAlphaMask::applyToRows(
    int firstRow, int rowCount, std::vector<uint8_t>& tone) const
{
    if (!isActive())
        return true;
    const size_t count = static_cast<size_t>(m_width) * rowCount;
    if (tone.size() != count)
        return false;
    std::vector<uint8_t> alpha;
    if (!readRows(firstRow, rowCount, alpha) || alpha.size() != count)
        return false;
    for (size_t i = 0; i < count; ++i) {
        tone[i] = static_cast<uint8_t>(
            (static_cast<uint16_t>(tone[i]) * alpha[i] + 127u) / 255u);
    }
    return true;
}
