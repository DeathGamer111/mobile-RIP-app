#include "MultiInkToneBuilder.h"

#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {

struct GcrParams {
    uint8_t neutralGate;
    uint8_t gcrMaxTone;
    uint8_t gcrStrength;
    uint8_t kGain;
    uint8_t kMinInNeutral;
    uint8_t lcFadePctInNeutral;
    uint8_t lmFadePctInNeutral;
    bool enabled = false;
};

struct SpecialtyInkConfig {
    int mode = 0;
    uint8_t threshold = 8;
    uint8_t density = 255;

    bool useOwnDotStrategy = false;
    int smallDotThreshold = 104;
    int medDotThreshold = 168;
    bool enablePromotion = false;

    QString maskKey;
};

static inline int vmInt(const QVariantMap& m, const char* k, int def) {
    auto it = m.constFind(k);
    return (it != m.constEnd()) ? it.value().toInt() : def;
}

static inline bool vmBool(const QVariantMap& m, const char* k, bool def) {
    auto it = m.constFind(k);
    return (it != m.constEnd()) ? it.value().toBool() : def;
}

static inline GcrParams getGcrParams(const QVariantMap& modeParams) {
    GcrParams p{};
    p.neutralGate        = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "neutralGate", 14), 0, 255));
    p.gcrMaxTone         = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "gcrMaxTone", 180), 0, 255));
    p.gcrStrength        = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "gcrStrength", 160), 0, 255));
    p.kGain              = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "kGain", 220), 0, 255));
    p.kMinInNeutral      = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "kMinInNeutral", 6), 0, 255));
    p.lcFadePctInNeutral = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "lcFadePctInNeutral", 70), 0, 100));
    p.lmFadePctInNeutral = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "lmFadePctInNeutral", 50), 0, 100));
    p.enabled = vmBool(modeParams, "gcrEnabled", false);
    return p;
}

static inline SpecialtyInkConfig getWhiteConfig(const QVariantMap& modeParams) {
    SpecialtyInkConfig c;
    c.mode = std::clamp(vmInt(modeParams, "whiteMode", 0), 0, 3);
    c.threshold = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "whiteThreshold", 8), 0, 255));
    c.density = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "whiteDensity", 255), 0, 255));
    c.useOwnDotStrategy = vmBool(modeParams, "whiteUseOwnDotStrategy", false);
    c.smallDotThreshold = std::clamp(vmInt(modeParams, "whiteSmallDotThreshold", 104), 0, 255);
    c.medDotThreshold = std::clamp(vmInt(modeParams, "whiteMedDotThreshold", 168), 0, 255);
    c.enablePromotion = vmBool(modeParams, "whiteEnablePromotion", false);
    c.maskKey = modeParams.value("whiteMaskKey", "w").toString().trimmed();
    if (c.maskKey.isEmpty()) c.maskKey = "w";
    return c;
}

static inline SpecialtyInkConfig getVarnishConfig(const QVariantMap& modeParams) {
    SpecialtyInkConfig c;
    c.mode = std::clamp(vmInt(modeParams, "varnishMode", 0), 0, 3);
    c.threshold = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "varnishThreshold", 8), 0, 255));
    c.density = static_cast<uint8_t>(std::clamp(vmInt(modeParams, "varnishDensity", 255), 0, 255));
    c.useOwnDotStrategy = vmBool(modeParams, "varnishUseOwnDotStrategy", false);
    c.smallDotThreshold = std::clamp(vmInt(modeParams, "varnishSmallDotThreshold", 104), 0, 255);
    c.medDotThreshold = std::clamp(vmInt(modeParams, "varnishMedDotThreshold", 168), 0, 255);
    c.enablePromotion = vmBool(modeParams, "varnishEnablePromotion", false);
    c.maskKey = modeParams.value("varnishMaskKey", "v").toString().trimmed();
    if (c.maskKey.isEmpty()) c.maskKey = "v";
    return c;
}

static inline uint8_t applySpecialtyThresholdDensity(uint8_t src, uint8_t threshold, uint8_t density) {
    if (density == 0) return 0;
    if (src <= threshold) return 0;
    if (threshold >= 255) return 0;

    const uint16_t norm = static_cast<uint16_t>(src - threshold) * 255 / (255 - threshold);
    const uint16_t out  = static_cast<uint16_t>(norm) * density / 255;
    return static_cast<uint8_t>(std::clamp<int>(out, 0, 255));
}

static inline float clamp01(float v) {
    return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v);
}

static inline float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return (x >= edge1) ? 1.f : 0.f;
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.f - 2.f * t);
}

struct Split2Params {
    uint8_t lightStart = 48;
    uint8_t lightEnd   = 160;
};

static inline void split2Crossfade(uint8_t v, uint8_t& darkOut, uint8_t& lightOut, const Split2Params& p) {
    if (v == 0) {
        darkOut = lightOut = 0;
        return;
    }

    float s = smoothstep(static_cast<float>(p.lightStart), static_cast<float>(p.lightEnd), static_cast<float>(v));
    float wDark = s;
    float wLight = 1.f - s;

    int dark = static_cast<int>(std::lround(static_cast<float>(v) * wDark));
    int light = static_cast<int>(v) - dark;

    darkOut = static_cast<uint8_t>(std::clamp(dark, 0, 255));
    lightOut = static_cast<uint8_t>(std::clamp(light, 0, 255));
}

struct Split3Params {
    uint8_t t1Start = 32;
    uint8_t t1End   = 128;
    uint8_t t2Start = 128;
    uint8_t t2End   = 224;
};

static inline void split3Crossfade(uint8_t v, uint8_t& llkOut, uint8_t& lkOut, uint8_t& kOut, const Split3Params& p) {
    if (v == 0) {
        llkOut = lkOut = kOut = 0;
        return;
    }

    float s1 = smoothstep(static_cast<float>(p.t1Start), static_cast<float>(p.t1End), static_cast<float>(v));
    float s2 = smoothstep(static_cast<float>(p.t2Start), static_cast<float>(p.t2End), static_cast<float>(v));

    float wLLk = 1.f - s1;
    float wK   = s2;
    float wLk  = 1.f - wLLk - wK;
    wLk = clamp01(wLk);

    int llk = static_cast<int>(std::lround(static_cast<float>(v) * wLLk));
    int k   = static_cast<int>(std::lround(static_cast<float>(v) * wK));
    int lk  = static_cast<int>(v) - llk - k;

    llkOut = static_cast<uint8_t>(std::clamp(llk, 0, 255));
    lkOut  = static_cast<uint8_t>(std::clamp(lk, 0, 255));
    kOut   = static_cast<uint8_t>(std::clamp(k, 0, 255));
}

static inline void applyGcrNeutralTransfer(
    std::vector<uint8_t>& cAdj,
    std::vector<uint8_t>& mAdj,
    std::vector<uint8_t>& yAdj,
    std::vector<uint8_t>& kAdj,
    int count,
    const GcrParams& p)
{
    if (!p.enabled)
        return;

    for (int i = 0; i < count; ++i) {
        uint8_t c = cAdj[i], m = mAdj[i], y = yAdj[i], k = kAdj[i];

        const uint8_t maxCMY = std::max({c, m, y});
        const uint8_t minCMY = std::min({c, m, y});
        const uint8_t chroma = maxCMY - minCMY;

        if (chroma > p.neutralGate) continue;
        if (maxCMY > p.gcrMaxTone) continue;

        const uint8_t neutral = minCMY;
        const uint16_t pull = (static_cast<uint16_t>(neutral) * p.gcrStrength) / 255;
        const uint8_t pull8 = static_cast<uint8_t>(std::min<uint16_t>(pull, neutral));

        c = static_cast<uint8_t>(c - pull8);
        m = static_cast<uint8_t>(m - pull8);
        y = static_cast<uint8_t>(y - pull8);

        const uint16_t kAdd = (static_cast<uint16_t>(pull8) * p.kGain) / 255;
        uint16_t kNew = std::min<uint16_t>(255, static_cast<uint16_t>(k) + kAdd);

        if (kNew < p.kMinInNeutral && neutral > 0)
            kNew = p.kMinInNeutral;

        cAdj[i] = c;
        mAdj[i] = m;
        yAdj[i] = y;
        kAdj[i] = static_cast<uint8_t>(kNew);
    }
}

static inline void applyNeutralLightInkFade(
    const std::vector<uint8_t>& cAdj,
    const std::vector<uint8_t>& mAdj,
    const std::vector<uint8_t>& yAdj,
    std::vector<uint8_t>& cLight,
    std::vector<uint8_t>& mLight,
    int count,
    const GcrParams& p)
{
    if (!p.enabled)
        return;

    auto fadePct = [&](uint8_t v, uint8_t pct) -> uint8_t {
        uint16_t vv = static_cast<uint16_t>(v) * static_cast<uint16_t>(100 - pct);
        return static_cast<uint8_t>(std::min<uint16_t>(255, vv / 100));
    };

    for (int i = 0; i < count; ++i) {
        const uint8_t c0 = cAdj[i], m0 = mAdj[i], y0 = yAdj[i];
        const uint8_t maxCMY = std::max({c0, m0, y0});
        const uint8_t minCMY = std::min({c0, m0, y0});
        const uint8_t chroma = maxCMY - minCMY;

        if (chroma > p.neutralGate) continue;
        if (maxCMY > p.gcrMaxTone) continue;

        cLight[i] = fadePct(cLight[i], p.lcFadePctInNeutral);
        mLight[i] = fadePct(mLight[i], p.lmFadePctInNeutral);
    }
}

static inline bool useComboLinearization(const MultiInkToneBuilder::BuildRequest& req)
{
    return req.enableLinearization &&
           req.linearization != nullptr &&
           req.linearization->hasExternalCurves();
}

static inline void applyCombo2(
    const MultiInkLinearization::ComboLut2& lut,
    uint8_t in,
    uint8_t& darkOut,
    uint8_t& lightOut)
{
    darkOut = lut.dark[in];
    lightOut = lut.light[in];
}

static inline void applyCombo3(
    const MultiInkLinearization::ComboLut3& lut,
    uint8_t in,
    uint8_t& darkOut,
    uint8_t& lightOut,
    uint8_t& lighterOut)
{
    darkOut    = lut.dark[in];
    lightOut   = lut.light[in];
    lighterOut = lut.lighter[in];
}

static inline uint8_t applyCombo1(
    const MultiInkLinearization::ComboLut1& lut,
    uint8_t in)
{
    return lut.single[in];
}

} // namespace

int MultiInkToneBuilder::whiteModeOverrideFromJob(const QString& s, bool& hasOverride)
{
    const QString v = s.trimmed();

    if (v.isEmpty() || v == "Use Global Setting") {
        hasOverride = false;
        return 0;
    }

    hasOverride = true;

    if (v == "Off") return 0;
    if (v == "Auto Underbase") return 1;
    if (v == "Flood") return 2;
    if (v == "Plate") return 3;

    hasOverride = false;
    return 0;
}

int MultiInkToneBuilder::varnishModeOverrideFromJob(const QString& s, bool& hasOverride)
{
    const QString v = s.trimmed();

    if (v.isEmpty() || v == "Use Global Setting") {
        hasOverride = false;
        return 0;
    }

    hasOverride = true;

    if (v == "Off") return 0;
    if (v == "Over Printed Area") return 1;
    if (v == "Flood") return 2;
    if (v == "Plate") return 3;

    hasOverride = false;
    return 0;
}

bool MultiInkToneBuilder::buildToneChannels(
    const BuildRequest& req,
    std::vector<std::vector<uint8_t>>& tones,
    const ExternalPlateLoader& plateLoader)
{
    if (!req.cmykImages) {
        tones.clear();
        return false;
    }

    const int width = static_cast<int>((*req.cmykImages)[0].columns());
    const int height = static_cast<int>((*req.cmykImages)[0].rows());
    const int count = width * height;

    std::vector<uint8_t> cBytes(count), mBytes(count), yBytes(count), kBytes(count);
    (*req.cmykImages)[0].write(0, 0, width, height, "I", Magick::CharPixel, cBytes.data());
    (*req.cmykImages)[1].write(0, 0, width, height, "I", Magick::CharPixel, mBytes.data());
    (*req.cmykImages)[2].write(0, 0, width, height, "I", Magick::CharPixel, yBytes.data());
    (*req.cmykImages)[3].write(0, 0, width, height, "I", Magick::CharPixel, kBytes.data());

    const SpecialtyInkConfig whiteCfg = getWhiteConfig(req.modeParams);
    const SpecialtyInkConfig varnishCfg = getVarnishConfig(req.modeParams);

    auto computeCoverageFromPostColor = [&](const std::vector<uint8_t>& c,
                                            const std::vector<uint8_t>& m,
                                            const std::vector<uint8_t>& y,
                                            const std::vector<uint8_t>& k,
                                            std::vector<uint8_t>& out)
    {
        out.resize(count);
        for (int i = 0; i < count; ++i) {
            out[i] = std::max(std::max(c[i], m[i]), std::max(y[i], k[i]));
        }
    };

    auto applySpecialtyConfigToCandidate = [&](const std::vector<uint8_t>& candidate,
                                               const SpecialtyInkConfig& cfg,
                                               std::vector<uint8_t>& out)
    {
        out.resize(count);
        for (int i = 0; i < count; ++i) {
            out[i] = applySpecialtyThresholdDensity(candidate[i], cfg.threshold, cfg.density);
        }
    };

    auto buildFloodPlate = [&](const SpecialtyInkConfig& cfg, std::vector<uint8_t>& out) {
        out.assign(count, cfg.density);
    };

    auto buildWhiteChannel = [&](const std::vector<uint8_t>& cPost,
                                 const std::vector<uint8_t>& mPost,
                                 const std::vector<uint8_t>& yPost,
                                 const std::vector<uint8_t>& kPost,
                                 std::vector<uint8_t>& whiteOut)
    {
        whiteOut.assign(count, 0);

        switch (whiteCfg.mode) {
        case 0:
            return;
        case 1: {
            std::vector<uint8_t> candidate;
            computeCoverageFromPostColor(cPost, mPost, yPost, kPost, candidate);
            applySpecialtyConfigToCandidate(candidate, whiteCfg, whiteOut);
            return;
        }
        case 2:
            buildFloodPlate(whiteCfg, whiteOut);
            return;
        case 3: {
            std::vector<uint8_t> plate;
            if (plateLoader && plateLoader(req.whitePlatePath, plate, width, height)) {
                applySpecialtyConfigToCandidate(plate, whiteCfg, whiteOut);
            } else {
                qWarning() << "MultiInkToneBuilder: white plate mode selected but plate not available. White disabled.";
            }
            return;
        }
        default:
            return;
        }
    };

    auto buildVarnishChannel = [&](const std::vector<uint8_t>& cPost,
                                   const std::vector<uint8_t>& mPost,
                                   const std::vector<uint8_t>& yPost,
                                   const std::vector<uint8_t>& kPost,
                                   std::vector<uint8_t>& varnishOut)
    {
        varnishOut.assign(count, 0);

        switch (varnishCfg.mode) {
        case 0:
            return;
        case 1: {
            std::vector<uint8_t> candidate;
            computeCoverageFromPostColor(cPost, mPost, yPost, kPost, candidate);
            applySpecialtyConfigToCandidate(candidate, varnishCfg, varnishOut);
            return;
        }
        case 2:
            buildFloodPlate(varnishCfg, varnishOut);
            return;
        case 3: {
            std::vector<uint8_t> plate;
            if (plateLoader && plateLoader(req.varnishPlatePath, plate, width, height)) {
                applySpecialtyConfigToCandidate(plate, varnishCfg, varnishOut);
            } else {
                qWarning() << "MultiInkToneBuilder: varnish plate mode selected but plate not available. Varnish disabled.";
            }
            return;
        }
        default:
            return;
        }
    };

    const GcrParams gcr = getGcrParams(req.modeParams);

    if (req.logConfiguration) {
        qDebug() << "MultiInkToneBuilder: combo linearization path ="
                 << (useComboLinearization(req) ? "true" : "false")
                 << "mode =" << static_cast<int>(req.mode);
    }

    switch (req.mode) {
    case InkMode::FourColor_YMCK: {
        tones.resize(4);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();

            for (int i = 0; i < count; ++i) {
                cAdj[i] = cLut.dark[cAdj[i]];
                mAdj[i] = mLut.dark[mAdj[i]];
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
                kAdj[i] = kLut.dark[kAdj[i]];
            }
        }

        tones[0] = cAdj;
        tones[1] = mAdj;
        tones[2] = yAdj;
        tones[3] = kAdj;
        return true;
    }

    case InkMode::FiveColor_YMCK_W: {
        tones.resize(5);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();
            const auto& wLut = req.linearization->lutW();

            for (int i = 0; i < count; ++i) {
                cAdj[i] = cLut.dark[cAdj[i]];
                mAdj[i] = mLut.dark[mAdj[i]];
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
                kAdj[i] = kLut.dark[kAdj[i]];
                (void)wLut; // white is applied after channel build; explicit W LUT support can be added later if desired
            }
        }

        tones[0] = cAdj;
        tones[1] = mAdj;
        tones[2] = yAdj;
        tones[3] = kAdj;
        buildWhiteChannel(cAdj, mAdj, yAdj, kAdj, tones[4]);

        if (useComboLinearization(req)) {
            const auto& wLut = req.linearization->lutW();
            for (int i = 0; i < count; ++i)
                tones[4][i] = wLut[tones[4][i]];
        }

        return true;
    }

    case InkMode::SixColor_YMCK_Lm_Lc: {
        tones.resize(6);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        std::vector<uint8_t> cDark(count), cLight(count), mDark(count), mLight(count);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();

            for (int i = 0; i < count; ++i) {
                applyCombo2(cLut, cAdj[i], cDark[i], cLight[i]);
                applyCombo2(mLut, mAdj[i], mDark[i], mLight[i]);
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
                kAdj[i] = kLut.dark[kAdj[i]];
            }
        } else {
            Split2Params cSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightStart", 24), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightEnd",   96), 0, 255))
            };
            Split2Params mSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightStart", 16), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightEnd",   80), 0, 255))
            };

            for (int i = 0; i < count; ++i) {
                split2Crossfade(cAdj[i], cDark[i], cLight[i], cSplit);
                split2Crossfade(mAdj[i], mDark[i], mLight[i], mSplit);
            }
        }

        tones[0] = cDark;
        tones[1] = mDark;
        tones[2] = yAdj;
        tones[3] = kAdj;
        tones[4] = mLight;
        tones[5] = cLight;
        return true;
    }

    case InkMode::SevenColor_YMCK_Lm_Lc_W: {
        tones.resize(7);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        std::vector<uint8_t> cDark(count), cLight(count), mDark(count), mLight(count);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();

            for (int i = 0; i < count; ++i) {
                applyCombo2(cLut, cAdj[i], cDark[i], cLight[i]);
                applyCombo2(mLut, mAdj[i], mDark[i], mLight[i]);
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
                kAdj[i] = kLut.dark[kAdj[i]];
            }
        } else {
            Split2Params cSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightStart", 48), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightEnd",  160), 0, 255))
            };
            Split2Params mSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightStart", 48), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightEnd",  160), 0, 255))
            };

            for (int i = 0; i < count; ++i) {
                split2Crossfade(cAdj[i], cDark[i], cLight[i], cSplit);
                split2Crossfade(mAdj[i], mDark[i], mLight[i], mSplit);
            }
        }

        tones[0] = cDark;
        tones[1] = mDark;
        tones[2] = yAdj;
        tones[3] = kAdj;
        tones[4] = mLight;
        tones[5] = cLight;
        buildWhiteChannel(cAdj, mAdj, yAdj, kAdj, tones[6]);

        if (useComboLinearization(req)) {
            const auto& wLut = req.linearization->lutW();
            for (int i = 0; i < count; ++i)
                tones[6][i] = wLut[tones[6][i]];
        }

        return true;
    }

    case InkMode::EightColor_YMCK_Lm_Lc_Lk_LLk: {
        tones.resize(8);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        std::vector<uint8_t> cDark(count), cLight(count), mDark(count), mLight(count);
        std::vector<uint8_t> llk(count), lk(count), kDark(count);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();

            for (int i = 0; i < count; ++i) {
                applyCombo2(cLut, cAdj[i], cDark[i], cLight[i]);
                applyCombo2(mLut, mAdj[i], mDark[i], mLight[i]);
                applyCombo3(kLut, kAdj[i], kDark[i], lk[i], llk[i]);
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
            }

            applyNeutralLightInkFade(cAdj, mAdj, yAdj, cLight, mLight, count, gcr);
        } else {
            Split2Params cSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightStart", 24), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightEnd",   96), 0, 255))
            };
            Split2Params mSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightStart", 16), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightEnd",   80), 0, 255))
            };
            Split3Params kSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT1Start", 24), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT1End",  112), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT2Start", 120), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT2End",   228), 0, 255))
            };

            for (int i = 0; i < count; ++i) {
                split2Crossfade(cAdj[i], cDark[i], cLight[i], cSplit);
                split2Crossfade(mAdj[i], mDark[i], mLight[i], mSplit);
                split3Crossfade(kAdj[i], llk[i], lk[i], kDark[i], kSplit);
            }

            applyNeutralLightInkFade(cAdj, mAdj, yAdj, cLight, mLight, count, gcr);
        }

        tones[0] = cDark;
        tones[1] = mDark;
        tones[2] = yAdj;
        tones[3] = kDark;
        tones[4] = cLight;
        tones[5] = mLight;
        tones[6] = lk;
        tones[7] = llk;
        return true;
    }

    case InkMode::TenColor_YMCK_Lm_Lc_Lk_LLk_W_V: {
        tones.resize(10);

        std::vector<uint8_t> cAdj = cBytes, mAdj = mBytes, yAdj = yBytes, kAdj = kBytes;
        applyGcrNeutralTransfer(cAdj, mAdj, yAdj, kAdj, count, gcr);

        std::vector<uint8_t> cDark(count), cLight(count), mDark(count), mLight(count);
        std::vector<uint8_t> llk(count), lk(count), kDark(count);

        if (useComboLinearization(req)) {
            const auto& cLut = req.linearization->lutC_Lc();
            const auto& mLut = req.linearization->lutM_Lm();
            const auto& yLut = req.linearization->lutY();
            const auto& kLut = req.linearization->lutK_Lk_LLk();

            for (int i = 0; i < count; ++i) {
                applyCombo2(cLut, cAdj[i], cDark[i], cLight[i]);
                applyCombo2(mLut, mAdj[i], mDark[i], mLight[i]);
                applyCombo3(kLut, kAdj[i], kDark[i], lk[i], llk[i]);
                yAdj[i] = applyCombo1(yLut, yAdj[i]);
            }

            applyNeutralLightInkFade(cAdj, mAdj, yAdj, cLight, mLight, count, gcr);
        } else {
            Split2Params cSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightStart", 48), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "cLightEnd",  160), 0, 255))
            };
            Split2Params mSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightStart", 48), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "mLightEnd",  160), 0, 255))
            };
            Split3Params kSplit{
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT1Start", 32), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT1End",  128), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT2Start", 128), 0, 255)),
                static_cast<uint8_t>(std::clamp(vmInt(req.modeParams, "kT2End",   224), 0, 255))
            };

            for (int i = 0; i < count; ++i) {
                split2Crossfade(cAdj[i], cDark[i], cLight[i], cSplit);
                split2Crossfade(mAdj[i], mDark[i], mLight[i], mSplit);
                split3Crossfade(kAdj[i], llk[i], lk[i], kDark[i], kSplit);
            }

            applyNeutralLightInkFade(cAdj, mAdj, yAdj, cLight, mLight, count, gcr);
        }

        tones[0] = cDark;
        tones[1] = mDark;
        tones[2] = yAdj;
        tones[3] = kDark;
        tones[4] = cLight;
        tones[5] = mLight;
        tones[6] = lk;
        tones[7] = llk;
        buildWhiteChannel(cAdj, mAdj, yAdj, kAdj, tones[8]);
        buildVarnishChannel(cAdj, mAdj, yAdj, kAdj, tones[9]);

        if (useComboLinearization(req)) {
            const auto& wLut = req.linearization->lutW();
            const auto& vLut = req.linearization->lutV();
            for (int i = 0; i < count; ++i) {
                tones[8][i] = wLut[tones[8][i]];
                tones[9][i] = vLut[tones[9][i]];
            }
        }

        return true;
    }
    }

    tones.clear();
    return false;
}
