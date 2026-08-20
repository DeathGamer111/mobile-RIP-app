#pragma once

#include <QString>
#include <QVariantMap>
#include <array>
#include <vector>
#include <functional>
#include <Magick++.h>
#include "MultiInkLinearization.h"

class MultiInkToneBuilder
{
public:
    enum class InkMode {
        FourColor_YMCK = 4,
        FiveColor_YMCK_W = 5,
        SixColor_YMCK_Lm_Lc = 6,
        SevenColor_YMCK_Lm_Lc_W = 7,
        EightColor_YMCK_Lm_Lc_Lk_LLk = 8,
        TenColor_YMCK_Lm_Lc_Lk_LLk_W_V = 10
    };

    struct BuildRequest {
        std::array<Magick::Image, 4>* cmykImages = nullptr;
        InkMode mode = InkMode::FourColor_YMCK;
        QVariantMap modeParams;
        QString whitePlatePath;
        QString varnishPlatePath;

        // New: optional combo-lut linearization
        const MultiInkLinearization* linearization = nullptr;
        bool enableLinearization = false;
        bool logConfiguration = true;
    };

    using ExternalPlateLoader = std::function<bool(
        const QString& platePath,
        std::vector<uint8_t>& outTone,
        int width,
        int height)>;

    static int whiteModeOverrideFromJob(const QString& s, bool& hasOverride);
    static int varnishModeOverrideFromJob(const QString& s, bool& hasOverride);

    static bool buildToneChannels(
        const BuildRequest& req,
        std::vector<std::vector<uint8_t>>& tones,
        const ExternalPlateLoader& plateLoader);
};
