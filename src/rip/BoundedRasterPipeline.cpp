#include "BoundedRasterPipeline.h"
#include "ImagePhysicalSize.h"
#include "MagickCompatibility.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QSet>
#include <QStorageInfo>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>

namespace BoundedRasterPipeline {
namespace {

constexpr quint64 MiB = 1024ULL * 1024ULL;
constexpr quint64 BoundedNativeBufferBudget = 128ULL * MiB;
constexpr quint64 InMemoryRasterBudget = 4ULL * 1024ULL * MiB;

void setError(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}

bool isCanceled(std::atomic_bool* canceled)
{
    return canceled && canceled->load(std::memory_order_relaxed);
}

int boundedRowsForWidth(int width, quint64 estimatedBytesPerPixel,
                        quint64 usableBudget = BoundedNativeBufferBudget)
{
    if (width <= 0 || estimatedBytesPerPixel == 0)
        return 1;
    const quint64 bytesPerRow = quint64(width) * estimatedBytesPerPixel;
    if (bytesPerRow == 0)
        return 1;
    return std::clamp<int>(
        int(std::min<quint64>(MaximumStripRows,
                              std::max<quint64>(1, usableBudget / bytesPerRow))),
        1, MaximumStripRows);
}

static inline uint32_t pxhash(
    uint32_t x, uint32_t y, uint32_t ch, uint32_t seed = 0x9E3779B9u)
{
    uint32_t h = x * 0x85EBCA6Bu ^ y * 0xC2B2AE35u ^
                 (ch + 1) * 0x27D4EB2Du ^ seed;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, uint8_t weight)
{
    return static_cast<uint8_t>(
        ((255 - weight) * a + weight * b) / 255);
}

int mapInt(const QVariantMap& values, const char* key, int fallback)
{
    const auto found = values.constFind(QLatin1String(key));
    return found == values.constEnd() ? fallback : found.value().toInt();
}

class MaskRows
{
public:
    bool open(const QString& path, QString* errorMessage)
    {
        try {
            m_image.read(path.toStdString());
            m_width = static_cast<int>(m_image.columns());
            m_height = static_cast<int>(m_image.rows());
            if (m_width <= 0 || m_height <= 0) {
                setError(errorMessage, QStringLiteral("Blue-noise mask is empty: %1").arg(path));
                return false;
            }
            return true;
        } catch (const Magick::Exception& error) {
            setError(errorMessage, QStringLiteral("Could not load blue-noise mask %1: %2")
                                       .arg(path, QString::fromUtf8(error.what())));
            return false;
        }
    }

    bool row(int row, const std::vector<uint8_t>** result, QString* errorMessage)
    {
        if (!result || m_width <= 0 || m_height <= 0)
            return false;
        int wrapped = row % m_height;
        if (wrapped < 0)
            wrapped += m_height;
        if (wrapped != m_cachedRow) {
            try {
                m_bytes.resize(static_cast<size_t>(m_width));
                m_image.write(0, wrapped, m_width, 1,
                              "I", Magick::CharPixel, m_bytes.data());
                m_cachedRow = wrapped;
            } catch (const Magick::Exception& error) {
                setError(errorMessage, QStringLiteral("Could not read a blue-noise mask row: %1")
                                           .arg(QString::fromUtf8(error.what())));
                return false;
            }
        }
        *result = &m_bytes;
        return true;
    }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    Magick::Image m_image;
    std::vector<uint8_t> m_bytes;
    int m_width = 0;
    int m_height = 0;
    int m_cachedRow = -1;
};

struct ScreenRow {
    int y = -1;
    std::vector<uint8_t> tone;
    std::vector<uint8_t> dots;
};

struct ScreenState {
    ScreenChannel config;
    MaskRows mask;
    int offsetX = 0;
    int offsetY = 0;
    std::deque<ScreenRow> rows;
};

std::vector<uint8_t> packRow(const std::vector<uint8_t>& dots, int width)
{
    const int unpaddedBytes = (width + 3) / 4;
    const int bytesPerLine = (unpaddedBytes + 3) & ~3;
    std::vector<uint8_t> packed(static_cast<size_t>(bytesPerLine), 0);
    for (int x = 0; x < width; ++x)
        packed[static_cast<size_t>(x / 4)] |=
            uint8_t((dots[static_cast<size_t>(x)] & 0x03u) <<
                    ((3 - (x % 4)) * 2));
    return packed;
}

void promoteCenterRow(ScreenState& state, int width, int height,
                      const QVariantMap& parameters)
{
    if (state.rows.size() != 4)
        return;
    ScreenRow& center = state.rows[1];
    if (center.y <= 0 || center.y >= height - 2)
        return;

    const uint8_t toneGate = static_cast<uint8_t>(
        std::clamp(mapInt(parameters, "promoToneGate", 112), 0, 255));
    const int medLo = std::clamp(mapInt(parameters, "promoMedLo", 18), 0, 255);
    const int medHi = std::clamp(mapInt(parameters, "promoMedHi", 26), 0, 255);
    const int largeLo = std::clamp(mapInt(parameters, "promoLrgLo", 28), 0, 255);
    const int largeHi = std::clamp(mapInt(parameters, "promoLrgHi", 36), 0, 255);
    const int flatEpsilon = std::clamp(mapInt(parameters, "promoFlatVarEps", 18), 0, 255);
    const int minimumInked = std::clamp(mapInt(parameters, "promoMinNeiInked", 8), 0, 255);
    const int kickBonus = std::clamp(mapInt(parameters, "promoKickBonus", 2), 0, 255);

    auto interpolate = [](int value, int low, int high) -> float {
        if (value <= low) return 0.0f;
        if (value >= high) return 1.0f;
        return float(value - low) / float(high - low);
    };

    for (int x = 1; x < width - 2; ++x) {
        uint8_t dot = center.dots[static_cast<size_t>(x)];
        if (dot == 0 || dot == 3)
            continue;
        const uint8_t tone = center.tone[static_cast<size_t>(x)];
        if (tone < toneGate)
            continue;

        int weighted = 0;
        int inked = 0;
        int deviation = 0;
        int deviationCount = 0;
        for (int dy = -1; dy <= 2; ++dy) {
            const ScreenRow& neighborRow = state.rows[static_cast<size_t>(dy + 1)];
            for (int dx = -1; dx <= 2; ++dx) {
                if (dx == 0 && dy == 0)
                    continue;
                const int nx = x + dx;
                const uint8_t neighborDot = neighborRow.dots[static_cast<size_t>(nx)];
                if (neighborDot > 0)
                    ++inked;
                weighted += int(neighborDot);
                deviation += std::abs(
                    int(neighborRow.tone[static_cast<size_t>(nx)]) - int(tone));
                ++deviationCount;
            }
        }
        if (inked < minimumInked ||
            (deviationCount && deviation / deviationCount > flatEpsilon))
            continue;

        weighted += kickBonus;
        const float toneFactor = float(tone) / 255.0f;
        const uint32_t random = pxhash(
            uint32_t(x), uint32_t(center.y), 0, 0x51F2F90Du) & 0xffffu;
        const float random01 = float(random) / 65535.0f;
        if (dot == 1) {
            const float probability = interpolate(weighted, medLo, medHi) * toneFactor;
            if (random01 < probability)
                center.dots[static_cast<size_t>(x)] = 2;
        } else if (dot == 2) {
            const float probability = interpolate(weighted, largeLo, largeHi) * toneFactor;
            if (random01 < probability)
                center.dots[static_cast<size_t>(x)] = 3;
        }
    }
}

bool emitFrontRow(ScreenState& state, int logicalChannel,
                  PrintFlowRasterSpool::Writer& writer, QString* errorMessage)
{
    if (state.rows.empty())
        return true;
    const ScreenRow& row = state.rows.front();
    const std::vector<uint8_t> packed = packRow(row.dots, int(row.dots.size()));
    if (!writer.writeLine(logicalChannel, row.y, packed.data(),
                          qsizetype(packed.size()), errorMessage))
        return false;
    state.rows.pop_front();
    return true;
}

} // namespace

QString scratchDirectory()
{
    return PrintFlowRasterSpool::scratchDirectory();
}

void configureImageMagickCache()
{
    const QString scratch = scratchDirectory();
    const QByteArray path = QFile::encodeName(scratch);
    qputenv("MAGICK_TMPDIR", path);
    qputenv("MAGICK_TEMPORARY_PATH", path);

    // Initialize once, after the application-controlled scratch path is in
    // the environment and before any Magick::Image instances are constructed.
    static std::once_flag initializeFlag;
    std::call_once(initializeFlag, []() { Magick::InitializeMagick(nullptr); });

    static std::once_flag cleanupFlag;
    std::call_once(cleanupFlag, [&]() {
        PrintFlowRasterSpool::removeStalePartials(scratch);
        QDir directory(scratch);
        for (const QFileInfo& stale : directory.entryInfoList(
                 {QStringLiteral("canonical-*.cmyk8.partial"),
                  QStringLiteral("alpha-*.gray8.partial"),
                  QStringLiteral("magick-*"),
                  QStringLiteral("printflow-*.pfrs")}, QDir::Files))
            QFile::remove(stale.absoluteFilePath());
    });
    Magick::ResourceLimits::memory(192ULL * MiB);
    Magick::ResourceLimits::map(128ULL * MiB);
    Magick::ResourceLimits::area(192ULL * MiB);
    // Pixel caches above the memory/map budgets must use disk. The storage
    // preflight remains the authoritative per-job limit. Reserving one sixth
    // of available storage matches the preflight's 20% safety margin.
    const QStorageInfo storage(scratch);
    const quint64 available = storage.isValid() && storage.isReady()
        ? quint64(storage.bytesAvailable()) : 0;
    const quint64 diskBudget = available > 0
        ? (available / 6ULL) * 5ULL : 1024ULL * MiB;
    Magick::ResourceLimits::disk(diskBudget);

    static std::once_flag logFlag;
    std::call_once(logFlag, [&]() {
        qInfo().noquote()
            << QStringLiteral("ImageMagick cache limits: memory %1 MiB; map %2 MiB; disk %3 MiB; scratch %4")
                   .arg(Magick::ResourceLimits::memory() / MiB)
                   .arg(Magick::ResourceLimits::map() / MiB)
                   .arg(Magick::ResourceLimits::disk() / MiB)
                   .arg(scratch);
    });
}

bool inspectSource(const QString& sourcePath,
                   int vectorReadXDpi, int vectorReadYDpi,
                   double fallbackXDpi, double fallbackYDpi,
                   SourceInfo* info, QString* errorMessage)
{
    if (!info || sourcePath.isEmpty()) {
        setError(errorMessage, QStringLiteral("Raster source path is empty."));
        return false;
    }
    *info = {};
    try {
        Magick::Image probe;
        ImagePhysicalSize::setVectorReadDensity(
            probe, sourcePath, vectorReadXDpi, vectorReadYDpi);
        probe.ping(sourcePath.toStdString());
        if (probe.columns() == 0 || probe.rows() == 0
            || probe.columns() > size_t(std::numeric_limits<int>::max())
            || probe.rows() > size_t(std::numeric_limits<int>::max())) {
            setError(errorMessage, QStringLiteral("Raster source dimensions are invalid."));
            return false;
        }
        const ImagePhysicalSize::Density density =
            ImagePhysicalSize::resolvedDensity(probe, fallbackXDpi, fallbackYDpi);
        info->width = static_cast<int>(probe.columns());
        info->height = static_cast<int>(probe.rows());
        info->xDpi = density.xDpi;
        info->yDpi = density.yDpi;
        info->hasPhysicalUnits = density.hasPhysicalUnits;
        info->hasAlpha = MagickCompatibility::hasAlphaChannel(probe);
        return true;
    } catch (const Magick::Exception& error) {
        setError(errorMessage,
                 QStringLiteral("Could not inspect raster source: %1")
                     .arg(QString::fromUtf8(error.what())));
        return false;
    }
}

bool estimateMaskCacheStorage(const QStringList& maskPaths, quint64* bytes,
                              QString* errorMessage)
{
    if (!bytes)
        return false;
    *bytes = 0;
    QSet<QString> uniquePaths;
    try {
        for (const QString& path : maskPaths) {
            if (path.isEmpty() || uniquePaths.contains(path))
                continue;
            uniquePaths.insert(path);
            Magick::Image mask;
            mask.ping(path.toStdString());
            const quint64 width = quint64(mask.columns());
            const quint64 height = quint64(mask.rows());
            constexpr quint64 cacheChannels = 5;
            if (width == 0 || height == 0 ||
                width > std::numeric_limits<quint64>::max() / height ||
                width * height > std::numeric_limits<quint64>::max() /
                    (cacheChannels * quint64(sizeof(Magick::Quantum)))) {
                setError(errorMessage,
                         QStringLiteral("Blue-noise mask dimensions are invalid: %1").arg(path));
                return false;
            }
            const quint64 cacheBytes = width * height * cacheChannels *
                quint64(sizeof(Magick::Quantum));
            if (*bytes > std::numeric_limits<quint64>::max() - cacheBytes) {
                setError(errorMessage, QStringLiteral("Blue-noise mask cache estimate overflowed."));
                return false;
            }
            *bytes += cacheBytes;
        }
    } catch (const Magick::Exception& error) {
        setError(errorMessage,
                 QStringLiteral("Could not inspect a blue-noise mask: %1")
                     .arg(QString::fromUtf8(error.what())));
        return false;
    }
    return !uniquePaths.isEmpty();
}

bool preflightStorage(int width, int height, int sourceWidth, int sourceHeight,
                      int logicalChannels,
                      int physicalChannels, bool includeFinalPrn,
                      bool includeAlphaCache, int externalPlateCaches,
                      quint64 maskCacheBytes,
                      QString* errorMessage)
{
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
        logicalChannels <= 0 || physicalChannels <= 0)
        return false;
    auto multiply = [](quint64 left, quint64 right, quint64* result) {
        if (!result || (right != 0 && left > std::numeric_limits<quint64>::max() / right))
            return false;
        *result = left * right;
        return true;
    };
    auto add = [](quint64 value, quint64* total) {
        if (!total || *total > std::numeric_limits<quint64>::max() - value)
            return false;
        *total += value;
        return true;
    };
    auto overflow = [&]() {
        setError(errorMessage, QStringLiteral("Raster storage estimate overflowed."));
        return false;
    };
    quint64 pixels = 0;
    quint64 sourcePixels = 0;
    if (!multiply(quint64(width), quint64(height), &pixels)
        || !multiply(quint64(sourceWidth), quint64(sourceHeight), &sourcePixels))
        return overflow();
    const quint64 unpaddedBytes = (quint64(width) + 3ULL) / 4ULL;
    const quint64 bytesPerLine = (unpaddedBytes + 3ULL) & ~3ULL;
    // CMYK canonical + output ImageMagick cache (five Q16/HDRI channels at
    // sizeof(Quantum)) + packed spool + optional exported PRN. ImageMagick's
    // resize filter temporarily keeps the source cache and a larger source-row
    // working cache; measured IM6 uses another 24 Quantum values per source
    // pixel. Scaling by sizeof(Quantum) also covers Android's HDRI build.
    quint64 canonical = 0;
    quint64 magickCache = 0;
    quint64 resizeSourceCaches = 0;
    quint64 alphaCaches = 0;
    quint64 externalCaches = 0;
    quint64 spool = 0;
    quint64 prn = 0;
    if (!multiply(pixels, 4ULL, &canonical)
        || !multiply(pixels, 5ULL * quint64(sizeof(Magick::Quantum)), &magickCache))
        return overflow();
    const bool resizeRequired = sourceWidth != width || sourceHeight != height;
    if ((resizeRequired
         && !multiply(sourcePixels, 29ULL * quint64(sizeof(Magick::Quantum)),
                      &resizeSourceCaches))
        || !multiply(bytesPerLine, quint64(height), &spool)
        || !multiply(spool, quint64(logicalChannels), &spool))
        return overflow();
    if (includeAlphaCache) {
        // Alpha capture briefly retains the source-size raw 8-bit file and a
        // grayscale ImageMagick cache. A resize can temporarily retain both
        // the source- and output-size grayscale caches.
        if (!multiply(sourcePixels,
                      1ULL + 5ULL * quint64(sizeof(Magick::Quantum)),
                      &alphaCaches))
            return overflow();
        if (resizeRequired) {
            quint64 resizedAlpha = 0;
            if (!multiply(pixels,
                          5ULL * quint64(sizeof(Magick::Quantum)),
                          &resizedAlpha)
                || !add(resizedAlpha, &alphaCaches))
                return overflow();
        }
    }
    if (!multiply(
            pixels,
            5ULL * quint64(sizeof(Magick::Quantum)) *
                quint64(std::clamp(externalPlateCaches, 0, 2)),
            &externalCaches))
        return overflow();
    if (includeFinalPrn) {
        if (!multiply(bytesPerLine, quint64(height), &prn)
            || !multiply(prn, quint64(physicalChannels), &prn))
            return overflow();
    }
    quint64 base = 0;
    if (!add(canonical, &base) || !add(magickCache, &base)
        || !add(resizeSourceCaches, &base) || !add(alphaCaches, &base)
        || !add(externalCaches, &base)
        || !add(maskCacheBytes, &base) || !add(spool, &base)
        || !add(prn, &base))
        return overflow();
    quint64 required = base;
    if (!add(base / 5ULL, &required))
        return overflow();
    const QStorageInfo storage(scratchDirectory());
    if (!storage.isValid() || !storage.isReady()) {
        setError(errorMessage, QStringLiteral("Raster scratch storage is not available."));
        return false;
    }
    const quint64 available = quint64(storage.bytesAvailable());
    qInfo().noquote()
        << QStringLiteral("Raster storage preflight: required %1 MiB; available %2 MiB.")
               .arg((required + MiB - 1) / MiB)
               .arg(available / MiB);
    if (available < required) {
        setError(errorMessage,
                 QStringLiteral("Not enough temporary storage to raster this job. Required: %1 MiB; available: %2 MiB.")
                     .arg((required + MiB - 1) / MiB)
                     .arg(available / MiB));
        return false;
    }
    return true;
}

RasterStrategyDecision selectRasterStrategy(
    int width, int height, int logicalChannels, bool multiInk)
{
    RasterStrategyDecision decision;
    decision.nativeLimitBytes = InMemoryRasterBudget;
    if (width <= 0 || height <= 0 || logicalChannels <= 0)
        return decision;

    const quint64 pixels = quint64(width) * quint64(height);
    if (pixels > quint64(std::numeric_limits<int>::max()))
        return decision;

    // The fast path holds complete tone and packed planes. Multi-ink tone
    // splitting temporarily retains more full-frame vectors than X-33. These
    // deliberately conservative estimates decide whether the job fits inside
    // the 4 GiB fast-path allowance. Larger jobs use bounded strips instead.
    const quint64 bytesPerPixel = multiInk
        ? 24ULL + 2ULL * quint64(logicalChannels)
        : 16ULL + (quint64(logicalChannels) + 3ULL) / 4ULL;
    constexpr quint64 fixedOverhead = 8ULL * MiB;
    if (pixels > (std::numeric_limits<quint64>::max() - fixedOverhead)
                     / bytesPerPixel)
        return decision;
    decision.estimatedNativeBytes = pixels * bytesPerPixel + fixedOverhead;

    const QByteArray forced = qgetenv("PRINTFLOW_FORCE_RASTER_STRATEGY")
                                  .trimmed().toLower();
    if (forced == "bounded")
        return decision;
    if (forced == "memory" ||
        decision.estimatedNativeBytes <= decision.nativeLimitBytes) {
        decision.strategy = RasterStrategy::InMemory;
    }
    return decision;
}

bool CanonicalCmykFile::create(
    Magick::Image& image, const RasterAlphaMask& alpha,
    const QString& directory, std::atomic_bool* canceled,
    const std::function<void(qint64, qint64)>& progress,
    QString* errorMessage)
{
    remove();
    m_width = static_cast<int>(image.columns());
    m_height = static_cast<int>(image.rows());
    if (m_width <= 0 || m_height <= 0) {
        setError(errorMessage, QStringLiteral("Canonical CMYK image is empty."));
        return false;
    }
    m_path = QDir(directory).filePath(
        QStringLiteral("canonical-%1.cmyk8.partial")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorMessage, QStringLiteral("Could not create the canonical CMYK cache: %1")
                                   .arg(file.errorString()));
        remove();
        return false;
    }
    try {
        const int stripRows = boundedRowsForWidth(
            m_width, alpha.isActive() ? 5 : 4, 64ULL * MiB);
        for (int firstRow = 0; firstRow < m_height; firstRow += stripRows) {
            if (isCanceled(canceled)) {
                setError(errorMessage, QStringLiteral("Rasterization canceled."));
                file.close();
                remove();
                return false;
            }
            const int rowCount = std::min(stripRows, m_height - firstRow);
            const size_t pixels = static_cast<size_t>(m_width) * rowCount;
            std::vector<uint8_t> bytes(pixels * 4ULL);
            image.write(0, firstRow, m_width, rowCount,
                        "CMYK", Magick::CharPixel, bytes.data());
            if (alpha.isActive()) {
                std::vector<uint8_t> alphaRows;
                if (!alpha.readRows(firstRow, rowCount, alphaRows) ||
                    alphaRows.size() != pixels) {
                    setError(errorMessage, QStringLiteral("Could not read the resized alpha mask."));
                    file.close();
                    remove();
                    return false;
                }
                for (size_t pixel = 0; pixel < pixels; ++pixel) {
                    for (int channel = 0; channel < 4; ++channel) {
                        uint8_t& tone = bytes[pixel * 4ULL + size_t(channel)];
                        tone = static_cast<uint8_t>(
                            (static_cast<uint16_t>(tone) * alphaRows[pixel] + 127u) / 255u);
                    }
                }
            }
            const qint64 byteCount = qint64(bytes.size());
            if (file.write(reinterpret_cast<const char*>(bytes.data()), byteCount) != byteCount) {
                setError(errorMessage, QStringLiteral("Could not write the canonical CMYK cache: %1")
                                           .arg(file.errorString()));
                file.close();
                remove();
                return false;
            }
            if (progress)
                progress(firstRow + rowCount, m_height);
        }
    } catch (const Magick::Exception& error) {
        setError(errorMessage, QStringLiteral("Could not export canonical CMYK rows: %1")
                                   .arg(QString::fromUtf8(error.what())));
        file.close();
        remove();
        return false;
    } catch (const std::bad_alloc&) {
        setError(errorMessage, QStringLiteral("The bounded raster buffer could not be allocated."));
        file.close();
        remove();
        return false;
    }
    file.close();
    return true;
}

bool CanonicalCmykFile::readRows(
    int firstRow, int rowCount, std::vector<uint8_t>& interleaved,
    QString* errorMessage)
{
    if (firstRow < 0 || rowCount <= 0 || firstRow + rowCount > m_height) {
        setError(errorMessage, QStringLiteral("Canonical CMYK row request is invalid."));
        return false;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    const quint64 offset = quint64(firstRow) * quint64(m_width) * 4ULL;
    const quint64 count = quint64(rowCount) * quint64(m_width) * 4ULL;
    if (!file.seek(qint64(offset)) || count > quint64(std::numeric_limits<qsizetype>::max())) {
        setError(errorMessage, QStringLiteral("Could not seek within the canonical CMYK cache."));
        return false;
    }
    const QByteArray bytes = file.read(qsizetype(count));
    if (quint64(bytes.size()) != count) {
        setError(errorMessage, QStringLiteral("Canonical CMYK cache is truncated."));
        return false;
    }
    interleaved.assign(
        reinterpret_cast<const uint8_t*>(bytes.constData()),
        reinterpret_cast<const uint8_t*>(bytes.constData()) + bytes.size());
    return true;
}

void CanonicalCmykFile::remove()
{
    if (!m_path.isEmpty())
        QFile::remove(m_path);
    m_path.clear();
    m_width = 0;
    m_height = 0;
}

bool screenToSpool(
    int width, int height, uint32_t screenSeed,
    const QVariantMap& promotionParameters,
    const std::vector<ScreenChannel>& channels,
    const ToneProvider& toneProvider,
    PrintFlowRasterSpool::Writer& writer,
    std::atomic_bool* canceled,
    const std::function<void(qint64, qint64)>& progress,
    QString* errorMessage,
    int stripRows)
{
    if (width <= 0 || height <= 0 || channels.empty() || !toneProvider) {
        setError(errorMessage, QStringLiteral("Streaming screen request is invalid."));
        return false;
    }
    try {
        std::vector<ScreenState> states(channels.size());
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            states[channel].config = channels[channel];
            if (!states[channel].mask.open(channels[channel].maskPath, errorMessage))
                return false;
            const uint32_t hash = pxhash(0xB, 0xC, uint32_t(channel), screenSeed);
            states[channel].offsetX = int(hash & 0x7fffffffu) % states[channel].mask.width();
            states[channel].offsetY = int((hash >> 16) & 0x7fffffffu) % states[channel].mask.height();
        }

        // Leave 32 MiB of the 128 MiB native allowance for rolling promotion
        // rows, mask rows, packing, and provider-specific channel math.
        const quint64 estimatedBytesPerPixel =
            16ULL + 2ULL * quint64(channels.size());
        stripRows = std::min(
            std::clamp(stripRows, 1, MaximumStripRows),
            boundedRowsForWidth(width, estimatedBytesPerPixel, 96ULL * MiB));
        const qint64 totalWork = qint64(height) * qint64(channels.size());
        qint64 completed = 0;
        for (int firstRow = 0; firstRow < height; firstRow += stripRows) {
            if (isCanceled(canceled)) {
                setError(errorMessage, QStringLiteral("Rasterization canceled."));
                return false;
            }
            const int rowCount = std::min(stripRows, height - firstRow);
            std::vector<std::vector<uint8_t>> tones;
            if (!toneProvider(firstRow, rowCount, tones, errorMessage) ||
                tones.size() != channels.size())
                return false;
            const size_t expectedToneBytes = static_cast<size_t>(width) * rowCount;
            for (const auto& tone : tones) {
                if (tone.size() != expectedToneBytes) {
                    setError(errorMessage, QStringLiteral("Streaming tone strip size is invalid."));
                    return false;
                }
            }

            for (int localRow = 0; localRow < rowCount; ++localRow) {
                const int y = firstRow + localRow;
                for (size_t channel = 0; channel < channels.size(); ++channel) {
                    if (isCanceled(canceled)) {
                        setError(errorMessage, QStringLiteral("Rasterization canceled."));
                        return false;
                    }
                    ScreenState& state = states[channel];
                    const ScreenChannel& config = state.config;
                    const std::vector<uint8_t>* maskRow = nullptr;
                    if (!state.mask.row(y + state.offsetY, &maskRow, errorMessage))
                        return false;

                    ScreenRow output;
                    output.y = y;
                    output.tone.resize(static_cast<size_t>(width));
                    output.dots.assign(static_cast<size_t>(width), 0);
                    const int minTone = std::clamp(config.minInkThreshold, 0, 254);
                    const int denominator = 255 - minTone;
                    const uint8_t smallBase = static_cast<uint8_t>(
                        std::clamp(config.smallDotThreshold, 0, 255));
                    const uint8_t mediumBase = static_cast<uint8_t>(
                        std::clamp(config.medDotThreshold, 0, 255));
                    const size_t toneBase = static_cast<size_t>(localRow) * width;
                    for (int x = 0; x < width; ++x) {
                        const uint8_t originalTone = tones[channel][toneBase + size_t(x)];
                        if (originalTone <= minTone) {
                            output.tone[size_t(x)] = 0;
                            continue;
                        }
                        uint16_t effectiveTone =
                            static_cast<uint16_t>(originalTone - minTone) * 255 / denominator;
                        if (config.floorRange > 0 && config.floorMax > 0 &&
                            effectiveTone < config.floorRange) {
                            const uint16_t bias = static_cast<uint16_t>(config.floorMax) *
                                (config.floorRange - effectiveTone) / config.floorRange;
                            effectiveTone = std::min<uint16_t>(255, effectiveTone + bias);
                        }
                        int maskX = x + state.offsetX;
                        if (maskX >= state.mask.width())
                            maskX %= state.mask.width();
                        const uint8_t threshold = (*maskRow)[size_t(maskX)];
                        const uint8_t classificationTone = config.useEffectiveTone
                            ? static_cast<uint8_t>(effectiveTone) : originalTone;
                        output.tone[size_t(x)] = classificationTone;
                        if (effectiveTone <= threshold)
                            continue;
                        const uint8_t relative = static_cast<uint8_t>(
                            static_cast<uint16_t>(threshold) * classificationTone / 255);
                        const uint8_t smallCut = lerpU8(64, smallBase, classificationTone);
                        const uint8_t mediumCut = lerpU8(130, mediumBase, classificationTone);
                        uint8_t dot = relative <= smallCut ? 1
                            : (relative <= mediumCut ? 2 : 3);
                        if (config.enableDotSwap) {
                            constexpr uint8_t low = 96;
                            constexpr uint8_t high = 160;
                            if (classificationTone < low) {
                                dot = uint8_t(4 - dot);
                            } else if (classificationTone < high) {
                                const uint8_t probability = static_cast<uint8_t>(
                                    static_cast<uint16_t>(high - classificationTone) *
                                    255 / (high - low));
                                if ((pxhash(uint32_t(x), uint32_t(y), 0,
                                            0x51F2F90Du) & 255u) < probability)
                                    dot = uint8_t(4 - dot);
                            }
                        }
                        output.dots[size_t(x)] = dot;
                    }
                    state.rows.push_back(std::move(output));
                    if (!config.enablePromotion) {
                        if (!emitFrontRow(state, int(channel), writer, errorMessage))
                            return false;
                    } else if (state.rows.size() == 4) {
                        promoteCenterRow(state, width, height, promotionParameters);
                        if (!emitFrontRow(state, int(channel), writer, errorMessage))
                            return false;
                    }
                    ++completed;
                }
                if (progress)
                    progress(completed, totalWork);
            }
        }
        for (size_t channel = 0; channel < states.size(); ++channel) {
            while (!states[channel].rows.empty()) {
                if (!emitFrontRow(states[channel], int(channel), writer, errorMessage))
                    return false;
            }
        }
        return true;
    } catch (const std::bad_alloc&) {
        setError(errorMessage, QStringLiteral("The bounded raster buffer could not be allocated."));
        return false;
    }
}

} // namespace BoundedRasterPipeline
