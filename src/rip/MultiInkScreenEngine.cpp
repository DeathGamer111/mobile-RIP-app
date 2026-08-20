#include "MultiInkScreenEngine.h"
#include "PrintJobMultiInk.h"

#include <algorithm>
#include <cmath>
#include <QDebug>

namespace {

static inline uint32_t pxhash(uint32_t x, uint32_t y, uint32_t ch, uint32_t seed = 0x9E3779B9u) {
    uint32_t h = x * 0x85EBCA6Bu ^ y * 0xC2B2AE35u ^ (ch + 1) * 0x27D4EB2Du ^ seed;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t w) {
    return static_cast<uint8_t>(((255 - w) * a + w * b) / 255);
}

static inline int vmInt(const QVariantMap& m, const char* k, int def) {
    auto it = m.constFind(k);
    return (it != m.constEnd()) ? it.value().toInt() : def;
}

static inline bool vmBool(const QVariantMap& m, const char* k, bool def) {
    auto it = m.constFind(k);
    return (it != m.constEnd()) ? it.value().toBool() : def;
}

} // namespace

bool MultiInkScreenEngine::screenChannels(
    const ScreenRequest& req,
    const MaskLoader& maskLoader,
    AllPackedLines& allPacked)
{
    allPacked.clear();

    if (req.width <= 0 || req.height <= 0 || req.channels.empty())
        return false;

    const int pixelCount = req.width * req.height;
    allPacked.resize(static_cast<int>(req.channels.size()));

    QString lastMaskKey;
    std::vector<uint8_t> lastMaskRaw;
    int lastMaskW = 0;
    int lastMaskH = 0;

    const bool useLightOverride = vmBool(req.modeParams, "useLightInkMinThresholdOverride", false);
    const int lightMinT = std::clamp(vmInt(req.modeParams, "lightInkMinThreshold", 2), 0, 254);

    for (int ch = 0; ch < static_cast<int>(req.channels.size()); ++ch) {
        if (req.canceled && req.canceled->load(std::memory_order_relaxed))
            return false;
        const auto& channelReq = req.channels[ch];
        if (!channelReq.toneBytes) {
            qWarning() << "MultiInkScreenEngine: null tone channel at index" << ch;
            return false;
        }

        const std::vector<uint8_t>& channelBytes = *channelReq.toneBytes;
        if (static_cast<int>(channelBytes.size()) != pixelCount) {
            qWarning() << "MultiInkScreenEngine: tone channel size mismatch at index" << ch;
            return false;
        }

        const std::vector<uint8_t>* maskRawPtr = nullptr;
        int maskW = 0, maskH = 0;

        if (channelReq.maskKey == lastMaskKey) {
            maskRawPtr = &lastMaskRaw;
            maskW = lastMaskW;
            maskH = lastMaskH;
        } else {
            std::vector<uint8_t> tmp;
            if (!maskLoader || !maskLoader(channelReq.maskKey, tmp, maskW, maskH)) {
                qWarning() << "MultiInkScreenEngine: failed to load mask:" << channelReq.maskKey;
                return false;
            }

            lastMaskKey = channelReq.maskKey;
            lastMaskRaw = std::move(tmp);
            lastMaskW = maskW;
            lastMaskH = maskH;
            maskRawPtr = &lastMaskRaw;
        }

        std::vector<uint8_t> effTone(pixelCount, 0);
        std::vector<uint8_t> dithered(pixelCount, 0);
        std::vector<uint8_t> classMask(pixelCount, 0);

        const bool inferredKLike =
            (channelReq.maskKey == "k" ||
             channelReq.maskKey == "lk" ||
             channelReq.maskKey == "llk" ||
             channelReq.maskKey == "w" ||
             channelReq.maskKey == "v");
        const bool isKLike = channelReq.floorClass < 0
            ? inferredKLike : channelReq.floorClass > 0;

        const uint8_t floorRange = isKLike ? req.dotStrategy.floorRangeK : req.dotStrategy.floorRangeCMY;
        const uint8_t floorMax   = isKLike ? req.dotStrategy.floorMaxK   : req.dotStrategy.floorMaxCMY;

        int minTch = std::clamp(req.dotStrategy.minInkThreshold, 0, 254);
        if (useLightOverride && channelReq.isLightInk) {
            minTch = lightMinT;
        }
        minTch = std::clamp(minTch, 0, 254);
        const int denom = 255 - minTch;

        const uint32_t h = pxhash(0xB, 0xC, static_cast<uint32_t>(ch), req.screenSeed);
        const int offXmask = static_cast<int>((h) & 0x7FFFFFFF) % maskW;
        const int offYmask = static_cast<int>((h >> 16) & 0x7FFFFFFF) % maskH;

        auto sampleMask = [&](int x, int y) -> uint8_t {
            int mx = x + offXmask; if (mx >= maskW) mx %= maskW;
            int my = y + offYmask; if (my >= maskH) my %= maskH;
            return (*maskRawPtr)[my * maskW + mx];
        };

        for (int y = 0; y < req.height; ++y) {
            if (req.canceled && req.canceled->load(std::memory_order_relaxed))
                return false;
            int rowBase = y * req.width;
            for (int x = 0; x < req.width; ++x) {
                int idx = rowBase + x;
                uint8_t u = channelBytes[idx];

                if (u <= minTch) {
                    dithered[idx] = 0;
                    effTone[idx] = 0;
                    classMask[idx] = 255;
                    continue;
                }

                uint16_t uEff = static_cast<uint16_t>(u - minTch) * 255 / denom;

                if (floorRange > 0 && floorMax > 0 && uEff < floorRange) {
                    const uint16_t bias =
                        static_cast<uint16_t>(floorMax) * (floorRange - uEff) / floorRange;
                    uEff = std::min<uint16_t>(255, uEff + bias);
                }

                uint8_t t = sampleMask(x, y);
                dithered[idx] = (uEff > t) ? 255 : 0;
                classMask[idx] = t;
                effTone[idx] = channelReq.useEffectiveTone
                    ? static_cast<uint8_t>(uEff) : u;
            }
        }

        MultiInkDotStrategy channelStrategy = req.dotStrategy;
        if (channelReq.useOwnDotStrategy) {
            channelStrategy.smallDotThreshold = channelReq.ownSmallDotThreshold;
            channelStrategy.medDotThreshold = channelReq.ownMedDotThreshold;
            channelStrategy.enablePromotion = channelReq.ownEnablePromotion;
        }

        auto dotMap = dotClassification(
            dithered,
            classMask,
            effTone,
            req.width,
            req.height,
            channelStrategy);

        if (channelStrategy.enablePromotion) {
            apply4x4Promotion(dotMap, effTone, req.width, req.height, req.modeParams);
        }

        allPacked[ch] = packTo2BPP(dotMap, req.width, req.height);
        if (req.progress)
            req.progress(ch + 1, qint64(req.channels.size()));
    }

    return true;
}

std::vector<std::vector<uint8_t>> MultiInkScreenEngine::dotClassification(
    const std::vector<uint8_t>& dithered,
    const std::vector<uint8_t>& mask,
    const std::vector<uint8_t>& channel,
    int width, int height,
    const MultiInkDotStrategy& strategy)
{
    std::vector<std::vector<uint8_t>> dotMap(height, std::vector<uint8_t>(width, 0));

    const uint8_t smallBase = static_cast<uint8_t>(std::clamp(strategy.smallDotThreshold, 0, 255));
    const uint8_t medBase   = static_cast<uint8_t>(std::clamp(strategy.medDotThreshold,   0, 255));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;

            if (dithered[idx] == 0) continue;

            const uint8_t v = channel[idx];
            const uint8_t t = mask[idx];
            const uint8_t tRel = static_cast<uint8_t>((static_cast<uint16_t>(t) * v) / 255);

            const uint8_t smallCut = lerp_u8(64,  smallBase, v);
            const uint8_t medCut   = lerp_u8(130, medBase,  v);

            if (tRel <= smallCut)       dotMap[y][x] = 1;
            else if (tRel <= medCut)    dotMap[y][x] = 2;
            else                        dotMap[y][x] = 3;

            if (strategy.enableDotSwap) {
                const uint8_t lo = 96;
                const uint8_t hi = 160;

                if (v < lo) {
                    dotMap[y][x] = static_cast<uint8_t>(4 - dotMap[y][x]);
                } else if (v < hi) {
                    uint32_t h = pxhash(x, y, 0, 0x51F2F90Du);
                    uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(hi - v) * 255) / (hi - lo));
                    if ((h & 255) < p) {
                        dotMap[y][x] = static_cast<uint8_t>(4 - dotMap[y][x]);
                    }
                }
            }
        }
    }

    return dotMap;
}

void MultiInkScreenEngine::apply4x4Promotion(
    std::vector<std::vector<uint8_t>>& dotMap,
    const std::vector<uint8_t>& tone,
    int width, int height,
    const QVariantMap& modeParams)
{
    const uint8_t TONE_GATE = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "promoToneGate", 112), 0, 255));
    const int MED_LO        = std::clamp(vmInt(modeParams, "promoMedLo", 18), 0, 255);
    const int MED_HI        = std::clamp(vmInt(modeParams, "promoMedHi", 26), 0, 255);
    const int LRG_LO        = std::clamp(vmInt(modeParams, "promoLrgLo", 28), 0, 255);
    const int LRG_HI        = std::clamp(vmInt(modeParams, "promoLrgHi", 36), 0, 255);
    const int FLAT_VAR_EPS  = std::clamp(vmInt(modeParams, "promoFlatVarEps", 18), 0, 255);
    const int MIN_NEI_INKED = std::clamp(vmInt(modeParams, "promoMinNeiInked", 8), 0, 255);
    const int KICK_BONUS    = std::clamp(vmInt(modeParams, "promoKickBonus", 2), 0, 255);

    auto localMAD = [&](int cx, int cy) -> int {
        int sum = 0, cnt = 0;
        const int idxC = cy * width + cx;
        const int vC = tone[idxC];

        for (int dy = -1; dy <= 2; ++dy) {
            int y = cy + dy; if (y < 0 || y >= height) continue;
            for (int dx = -1; dx <= 2; ++dx) {
                int x = cx + dx; if (x < 0 || x >= width) continue;
                if (dx == 0 && dy == 0) continue;
                sum += std::abs(int(tone[y * width + x]) - vC);
                ++cnt;
            }
        }
        return cnt ? (sum / cnt) : 0;
    };

    auto lerp01 = [](int v, int lo, int hi) -> float {
        if (v <= lo) return 0.f;
        if (v >= hi) return 1.f;
        return float(v - lo) / float(hi - lo);
    };

    for (int y = 1; y < height - 2; ++y) {
        for (int x = 1; x < width - 2; ++x) {
            uint8_t cls = dotMap[y][x];
            if (cls == 0 || cls == 3) continue;

            const int idx = y * width + x;
            const uint8_t v = tone[idx];
            if (v < TONE_GATE) continue;

            int weighted = 0;
            int countAny = 0;
            for (int dy = -1; dy <= 2; ++dy) {
                for (int dx = -1; dx <= 2; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    uint8_t ncls = dotMap[y + dy][x + dx];
                    if (ncls > 0) ++countAny;
                    weighted += int(ncls);
                }
            }

            if (countAny < MIN_NEI_INKED) continue;
            if (localMAD(x, y) > FLAT_VAR_EPS) continue;

            weighted += KICK_BONUS;

            const float toneF = float(v) / 255.0f;
            const uint32_t rnd = pxhash(x, y, 0, 0x51F2F90Du) & 0xFFFF;
            const float r01 = float(rnd) / 65535.0f;

            if (cls == 1) {
                float p = lerp01(weighted, MED_LO, MED_HI) * toneF;
                if (r01 < p) dotMap[y][x] = 2;
            } else if (cls == 2) {
                float p = lerp01(weighted, LRG_LO, LRG_HI) * toneF;
                if (r01 < p) dotMap[y][x] = 3;
            }
        }
    }
}

MultiInkScreenEngine::PackedLines MultiInkScreenEngine::packTo2BPP(
    const std::vector<std::vector<uint8_t>>& dotMap,
    int width, int height)
{
    const int bytesPerLine = (width + 3) / 4;
    PackedLines packedLines(height);

    for (int y = 0; y < height; ++y) {
        std::vector<uint8_t>& line = packedLines[y];
        line.reserve(bytesPerLine);

        uint8_t byte = 0;
        int idx = 0;

        for (int x = 0; x < width; ++x) {
            uint8_t level = dotMap[y][x] & 0x03;
            byte |= level << ((3 - (idx % 4)) * 2);
            ++idx;

            if (idx % 4 == 0) {
                line.push_back(byte);
                byte = 0;
            }
        }

        if (idx % 4 != 0) {
            line.push_back(byte);
        }

        while (line.size() % 4 != 0) {
            line.push_back(0);
        }
    }

    return packedLines;
}
