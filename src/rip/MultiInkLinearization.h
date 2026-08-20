#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <QString>

class MultiInkLinearization
{
public:

	using Lut8 = std::array<uint8_t, 256>;

    enum class Channel {
        C,
        M,
        Y,
        K,
        LC,
        LM,
        LK,
        LLK,
        W,
        V,
		Count
    };

	struct ComboLut1 {
        Lut8 single{};
        bool valid = false;
    };

    struct ComboLut2 {
        Lut8 dark{};
        Lut8 light{};
        bool valid = false;
    };

    struct ComboLut3 {
        Lut8 dark{};
        Lut8 light{};
        Lut8 lighter{};
        bool valid = false;
    };

    MultiInkLinearization();

    // Reset to identity LUTs for all channels.
    void rebuildIdentityLuts();

    // Load XML transfer curves. Missing light channels are derived from parent channels.
    bool loadTransferCurveXml(const QString& path);

    // Clear external state and go back to identity LUTs.
    void clearExternalCurves();

    bool hasExternalCurves() const { return m_hasExternalCurves; }
    QString lastError() const { return m_lastError; }

    // Existing combo accessors kept intact for current tone-builder integration.
    const ComboLut2& lutC_Lc() const { return m_cLc; }
    const ComboLut2& lutM_Lm() const { return m_mLm; }
    const ComboLut1& lutY() const { return m_y; }
    const ComboLut3& lutK_Lk_LLk() const { return m_kLkLLk; }

    const Lut8& lutW() const { return m_w; }
    const Lut8& lutV() const { return m_v; }

    // Apply the shared dark-ink CMYK curves used by four-color output. This
    // is also the complete color set for the legacy X-33 pipeline.
    bool applyFourColorTones(
        std::array<std::vector<uint8_t>, 4>& tones) const;

    // W remains identity when the selected XML has no White separation.
    bool applyWhiteTone(std::vector<uint8_t>& tone) const;

private:
    static Lut8 makeLinearLut();

    static double clamp01(double v);
    static double smoothstep(double edge0, double edge1, double x);

    static bool parseCurvePairs(const QString& curveText,
                                std::vector<double>& xs,
                                std::vector<double>& ys,
                                QString* errorOut);

    static Lut8 buildLutFromTransferPairs(const std::vector<double>& xs,
                                          const std::vector<double>& ys);

    static Lut8 deriveLightInkLut(const Lut8& parent,
                                  double fadeStart,
                                  double fadeEnd,
                                  double strength = 1.0);

    static int channelIndex(Channel ch) {
        return static_cast<int>(ch);
    }

    static bool mapSeparationName(const QString& name, Channel& outChannel);

    void deriveMissingLightChannels();
    void rebuildCombosFromChannels();

private:
    std::array<Lut8, static_cast<int>(Channel::Count)> m_channelLuts{};
    std::array<bool, static_cast<int>(Channel::Count)> m_channelValid{};

    ComboLut2 m_cLc{};
    ComboLut2 m_mLm{};
    ComboLut1 m_y{};
    ComboLut3 m_kLkLLk{};

    Lut8 m_w{};
    Lut8 m_v{};

    bool m_hasExternalCurves = false;
    QString m_lastError;
};
