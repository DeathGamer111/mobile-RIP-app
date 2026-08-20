#pragma once

#include <Magick++.h>

#include <cstdint>
#include <memory>
#include <atomic>
#include <vector>

// Keeps source transparency independent from ICC/DeviceLink conversions, which
// may legitimately replace the working image with an opaque CMYK image.
class RasterAlphaMask
{
public:
    void reset();
    bool capture(Magick::Image& source,
                 std::atomic_bool* canceled = nullptr);
    bool resize(int width, int height);

    bool isActive() const noexcept;
    int width() const noexcept;
    int height() const noexcept;

    bool applyTo(std::vector<uint8_t>& tone) const;
    bool applyTo(Magick::Image& grayscaleTone) const;
    bool readRows(int firstRow, int rowCount, std::vector<uint8_t>& alpha) const;
    bool applyToRows(int firstRow, int rowCount,
                     std::vector<uint8_t>& tone) const;

private:
    int m_width = 0;
    int m_height = 0;
    std::unique_ptr<Magick::Image> m_mask;
};
