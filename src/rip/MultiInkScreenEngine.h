#pragma once

#include <QString>
#include <QVariantMap>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include "MultiInkTypes.h"

class MultiInkScreenEngine
{
public:
    struct ChannelRequest {
        QString maskKey;
        const std::vector<uint8_t>* toneBytes = nullptr;
        bool isLightInk = false;
        bool useOwnDotStrategy = false;
        int ownSmallDotThreshold = 104;
        int ownMedDotThreshold = 168;
        bool ownEnablePromotion = false;
        // Multi-ink uses the normalized post-gate tone for classification;
        // X-33 intentionally retains the original post-linearization tone.
        bool useEffectiveTone = true;
        // -1 infers from the mask key, 0 uses CMY floor values, 1 uses K.
        int floorClass = -1;
    };

    struct ScreenRequest {
        int width = 0;
        int height = 0;
        uint32_t screenSeed = 0;
        QVariantMap modeParams;
        MultiInkDotStrategy dotStrategy;
        std::vector<ChannelRequest> channels;
        std::atomic_bool* canceled = nullptr;
        std::function<void(qint64, qint64)> progress;
    };

    using MaskLoader = std::function<bool(
        const QString& key,
        std::vector<uint8_t>& maskRaw,
        int& maskW,
        int& maskH)>;

    using PackedLines = std::vector<std::vector<uint8_t>>;
    using AllPackedLines = std::vector<PackedLines>;

    static bool screenChannels(
        const ScreenRequest& req,
        const MaskLoader& maskLoader,
        AllPackedLines& allPacked);

private:
    static std::vector<std::vector<uint8_t>> dotClassification(
        const std::vector<uint8_t>& dithered,
        const std::vector<uint8_t>& mask,
        const std::vector<uint8_t>& channel,
        int width, int height,
        const MultiInkDotStrategy& strategy);

    static void apply4x4Promotion(
        std::vector<std::vector<uint8_t>>& dotMap,
        const std::vector<uint8_t>& tone,
        int width, int height,
        const QVariantMap& modeParams);

    static PackedLines packTo2BPP(
        const std::vector<std::vector<uint8_t>>& dotMap,
        int width, int height);
};
