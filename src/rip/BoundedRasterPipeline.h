#pragma once

#include "RasterAlphaMask.h"
#include "RasterSpool.h"

#include <Magick++.h>

#include <QVariantMap>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace BoundedRasterPipeline {

constexpr int MaximumStripRows = 128;

enum class RasterStrategy {
    InMemory,
    Bounded
};

struct RasterStrategyDecision {
    RasterStrategy strategy = RasterStrategy::Bounded;
    quint64 estimatedNativeBytes = 0;
    quint64 nativeLimitBytes = 0;
};

struct SourceInfo {
    int width = 0;
    int height = 0;
    double xDpi = 0.0;
    double yDpi = 0.0;
    bool hasPhysicalUnits = false;
    bool hasAlpha = false;
};

QString scratchDirectory();
void configureImageMagickCache();
bool inspectSource(const QString& sourcePath,
                   int vectorReadXDpi, int vectorReadYDpi,
                   double fallbackXDpi, double fallbackYDpi,
                   SourceInfo* info, QString* errorMessage = nullptr);
bool estimateMaskCacheStorage(const QStringList& maskPaths, quint64* bytes,
                              QString* errorMessage = nullptr);
bool preflightStorage(int width, int height, int sourceWidth, int sourceHeight,
                      int logicalChannels,
                      int physicalChannels, bool includeFinalPrn,
                      bool includeAlphaCache = false,
                      int externalPlateCaches = 0,
                      quint64 maskCacheBytes = 0,
                      QString* errorMessage = nullptr);
RasterStrategyDecision selectRasterStrategy(
    int width, int height, int logicalChannels, bool multiInk);

class CanonicalCmykFile
{
public:
    ~CanonicalCmykFile() { remove(); }
    bool create(Magick::Image& image, const RasterAlphaMask& alpha,
                const QString& directory, std::atomic_bool* canceled,
                const std::function<void(qint64, qint64)>& progress,
                QString* errorMessage = nullptr);
    bool readRows(int firstRow, int rowCount, std::vector<uint8_t>& interleaved,
                  QString* errorMessage = nullptr);
    int width() const { return m_width; }
    int height() const { return m_height; }
    QString path() const { return m_path; }
    void remove();

private:
    QString m_path;
    int m_width = 0;
    int m_height = 0;
};

struct ScreenChannel {
    QString maskPath;
    int minInkThreshold = 8;
    int smallDotThreshold = 104;
    int medDotThreshold = 168;
    uint8_t floorRange = 0;
    uint8_t floorMax = 0;
    bool enableDotSwap = false;
    bool enablePromotion = false;
    // MultiInk classifies and promotes against the normalized effective tone;
    // X-33 intentionally uses the original post-linearization tone.
    bool useEffectiveTone = false;
};

using ToneProvider = std::function<bool(
    int firstRow, int rowCount,
    std::vector<std::vector<uint8_t>>& tones,
    QString* errorMessage)>;

bool screenToSpool(
    int width, int height, uint32_t screenSeed,
    const QVariantMap& promotionParameters,
    const std::vector<ScreenChannel>& channels,
    const ToneProvider& toneProvider,
    PrintFlowRasterSpool::Writer& writer,
    std::atomic_bool* canceled,
    const std::function<void(qint64, qint64)>& progress,
    QString* errorMessage = nullptr,
    int stripRows = MaximumStripRows);

} // namespace BoundedRasterPipeline
