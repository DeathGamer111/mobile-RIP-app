#include "MultiInkLinearization.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QStringList>
#include <QDebug>

#include <algorithm>
#include <cmath>


namespace {


static inline QString normalizedSeparationName(const QString& s)
{
    QString out = s.trimmed().toLower();
    out.remove(' ');
    out.remove('_');
    out.remove('-');
    return out;
}

} // namespace


double MultiInkLinearization::clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}


double MultiInkLinearization::smoothstep(double edge0, double edge1, double x)
{
    if (edge0 == edge1)
        return (x >= edge1) ? 1.0 : 0.0;

    double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}


MultiInkLinearization::Lut8 MultiInkLinearization::makeLinearLut()
{
    Lut8 lut{};
    for (int i = 0; i < 256; ++i)
        lut[i] = static_cast<uint8_t>(i);
    return lut;
}


MultiInkLinearization::MultiInkLinearization()
{
    rebuildIdentityLuts();
}


void MultiInkLinearization::rebuildIdentityLuts()
{
    const Lut8 identity = makeLinearLut();

    for (int i = 0; i < static_cast<int>(Channel::Count); ++i) {
        m_channelLuts[i] = identity;
        m_channelValid[i] = false;
    }

    m_hasExternalCurves = false;
    m_lastError.clear();

    rebuildCombosFromChannels();
}


void MultiInkLinearization::clearExternalCurves()
{
    rebuildIdentityLuts();
}


bool MultiInkLinearization::mapSeparationName(const QString& name, Channel& outChannel)
{
    const QString s = normalizedSeparationName(name);

    if (s == "cyan" || s == "c") {
        outChannel = Channel::C;
        return true;
    }
    if (s == "magenta" || s == "m") {
        outChannel = Channel::M;
        return true;
    }
    if (s == "yellow" || s == "y") {
        outChannel = Channel::Y;
        return true;
    }
    if (s == "black" || s == "k") {
        outChannel = Channel::K;
        return true;
    }

    if (s == "lightcyan" || s == "lc") {
        outChannel = Channel::LC;
        return true;
    }
    if (s == "lightmagenta" || s == "lm") {
        outChannel = Channel::LM;
        return true;
    }
    if (s == "lightblack" || s == "lk") {
        outChannel = Channel::LK;
        return true;
    }
    if (s == "lightlightblack" || s == "llk") {
        outChannel = Channel::LLK;
        return true;
    }

    if (s == "white" || s == "w") {
        outChannel = Channel::W;
        return true;
    }
    if (s == "varnish" || s == "v") {
        outChannel = Channel::V;
        return true;
    }

    return false;
}


bool MultiInkLinearization::parseCurvePairs(const QString& curveText,
                                            std::vector<double>& xs,
                                            std::vector<double>& ys,
                                            QString* errorOut)
{
    xs.clear();
    ys.clear();

    const QStringList parts = curveText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 4 || (parts.size() % 2) != 0) {
        if (errorOut) *errorOut = "Curve must contain x/y numeric pairs.";
        return false;
    }

    xs.reserve(parts.size() / 2);
    ys.reserve(parts.size() / 2);

    for (int i = 0; i < parts.size(); i += 2) {
        bool okX = false;
        bool okY = false;
        const double x = parts[i].toDouble(&okX);
        const double y = parts[i + 1].toDouble(&okY);

        if (!okX || !okY) {
            if (errorOut) *errorOut = "Curve contains non-numeric values.";
            return false;
        }

        xs.push_back(clamp01(x));
        ys.push_back(clamp01(y));
    }

    for (size_t i = 1; i < xs.size(); ++i) {
        if (xs[i] < xs[i - 1]) {
            if (errorOut) *errorOut = "Curve x-values must be ascending.";
            return false;
        }
    }

    return true;
}


MultiInkLinearization::Lut8
MultiInkLinearization::buildLutFromTransferPairs(const std::vector<double>& xs,
                                                 const std::vector<double>& ys)
{
    Lut8 lut{};

    if (xs.empty() || ys.empty() || xs.size() != ys.size()) {
        return makeLinearLut();
    }

    for (int i = 0; i < 256; ++i) {
        const double x = static_cast<double>(i) / 255.0;

        double y = ys.back();

        if (x <= xs.front()) {
            y = ys.front();
        } else if (x >= xs.back()) {
            y = ys.back();
        } else {
            auto it = std::lower_bound(xs.begin(), xs.end(), x);
            const int idx = static_cast<int>(std::distance(xs.begin(), it));

            const int i0 = std::max(0, idx - 1);
            const int i1 = std::min(static_cast<int>(xs.size()) - 1, idx);

            const double x0 = xs[i0];
            const double x1 = xs[i1];
            const double y0 = ys[i0];
            const double y1 = ys[i1];

            if (std::abs(x1 - x0) < 1e-9) {
                y = y1;
            } else {
                const double t = (x - x0) / (x1 - x0);
                y = y0 + (y1 - y0) * t;
            }
        }

        lut[i] = static_cast<uint8_t>(std::lround(clamp01(y) * 255.0));
    }

    return lut;
}


MultiInkLinearization::Lut8
MultiInkLinearization::deriveLightInkLut(const Lut8& parent,
                                         double fadeStart,
                                         double fadeEnd,
                                         double strength)
{
    Lut8 out{};

    strength = clamp01(strength);

    for (int i = 0; i < 256; ++i) {
        const double x = static_cast<double>(i) / 255.0;
        const double parentNorm = static_cast<double>(parent[i]) / 255.0;

        // Strong in highlights, fades away into darker tones.
        const double fadeOut = 1.0 - smoothstep(fadeStart, fadeEnd, x);
        const double v = clamp01(parentNorm * fadeOut * strength);

        out[i] = static_cast<uint8_t>(std::lround(v * 255.0));
    }

    return out;
}


void MultiInkLinearization::deriveMissingLightChannels()
{
    // Explicit XML channels always win.
    // Only derive when missing.

    // LC from C
    if (!m_channelValid[channelIndex(Channel::LC)] &&
        m_channelValid[channelIndex(Channel::C)]) {
        m_channelLuts[channelIndex(Channel::LC)] =
            deriveLightInkLut(m_channelLuts[channelIndex(Channel::C)], 0.45, 0.80, 1.00);
    }

    // LM from M
    if (!m_channelValid[channelIndex(Channel::LM)] &&
        m_channelValid[channelIndex(Channel::M)]) {
        m_channelLuts[channelIndex(Channel::LM)] =
            deriveLightInkLut(m_channelLuts[channelIndex(Channel::M)], 0.45, 0.80, 1.00);
    }

    // LK from K
    if (!m_channelValid[channelIndex(Channel::LK)] &&
        m_channelValid[channelIndex(Channel::K)]) {
        m_channelLuts[channelIndex(Channel::LK)] =
            deriveLightInkLut(m_channelLuts[channelIndex(Channel::K)], 0.55, 0.88, 0.95);
    }

    // LLK from K
    if (!m_channelValid[channelIndex(Channel::LLK)] &&
        m_channelValid[channelIndex(Channel::K)]) {
        m_channelLuts[channelIndex(Channel::LLK)] =
            deriveLightInkLut(m_channelLuts[channelIndex(Channel::K)], 0.30, 0.65, 0.85);
    }

    // White / varnish remain identity unless explicitly provided.
}


void MultiInkLinearization::rebuildCombosFromChannels()
{
    m_cLc.dark   = m_channelLuts[channelIndex(Channel::C)];
    m_cLc.light  = m_channelLuts[channelIndex(Channel::LC)];
    m_cLc.valid  = true;

    m_mLm.dark   = m_channelLuts[channelIndex(Channel::M)];
    m_mLm.light  = m_channelLuts[channelIndex(Channel::LM)];
    m_mLm.valid  = true;

    m_y.single   = m_channelLuts[channelIndex(Channel::Y)];
    m_y.valid    = true;

    // Keep existing combo ordering expected by current tone builder:
    // applyCombo3(kLut, in, kDark, lk, llk)
    m_kLkLLk.dark    = m_channelLuts[channelIndex(Channel::K)];
    m_kLkLLk.light   = m_channelLuts[channelIndex(Channel::LK)];
    m_kLkLLk.lighter = m_channelLuts[channelIndex(Channel::LLK)];
    m_kLkLLk.valid   = true;

    m_w = m_channelLuts[channelIndex(Channel::W)];
    m_v = m_channelLuts[channelIndex(Channel::V)];
}


bool MultiInkLinearization::loadTransferCurveXml(const QString& path)
{
    rebuildIdentityLuts();
    m_lastError.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QString("Could not open linearization XML: %1").arg(path);
        return false;
    }

    QXmlStreamReader xml(&file);
    bool foundAnySupportedCurve = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (!xml.isStartElement())
            continue;

        if (xml.name() != QLatin1String("TransferCurve"))
            continue;

        const auto attrs = xml.attributes();
        const QString separation = attrs.value("Separation").toString().trimmed();
        const QString curveText  = attrs.value("Curve").toString().trimmed();

        if (separation.isEmpty() || curveText.isEmpty())
            continue;

        Channel ch;
        if (!mapSeparationName(separation, ch)) {
            qDebug() << "MultiInkLinearization: ignoring unsupported separation:" << separation;
            continue;
        }

        std::vector<double> xs;
        std::vector<double> ys;
        QString parseError;
        if (!parseCurvePairs(curveText, xs, ys, &parseError)) {
            m_lastError = QString("Invalid curve for '%1': %2").arg(separation, parseError);
            rebuildIdentityLuts();
            return false;
        }

        m_channelLuts[channelIndex(ch)] = buildLutFromTransferPairs(xs, ys);
        m_channelValid[channelIndex(ch)] = true;
        foundAnySupportedCurve = true;
    }

    if (xml.hasError()) {
        m_lastError = QString("XML parse error: %1").arg(xml.errorString());
        rebuildIdentityLuts();
        return false;
    }

    if (!foundAnySupportedCurve) {
        m_lastError = "No supported TransferCurve entries were found in the XML.";
        rebuildIdentityLuts();
        return false;
    }

    deriveMissingLightChannels();
    rebuildCombosFromChannels();

    m_hasExternalCurves = true;
    return true;
}


bool MultiInkLinearization::applyFourColorTones(
    std::array<std::vector<uint8_t>, 4>& tones) const
{
    if (!m_hasExternalCurves)
        return false;

    const size_t count = tones[0].size();
    for (const auto& tone : tones) {
        if (tone.size() != count)
            return false;
    }

    const auto& cLut = lutC_Lc();
    const auto& mLut = lutM_Lm();
    const auto& yLut = lutY();
    const auto& kLut = lutK_Lk_LLk();
    for (size_t index = 0; index < count; ++index) {
        tones[0][index] = cLut.dark[tones[0][index]];
        tones[1][index] = mLut.dark[tones[1][index]];
        tones[2][index] = yLut.single[tones[2][index]];
        tones[3][index] = kLut.dark[tones[3][index]];
    }
    return true;
}


bool MultiInkLinearization::applyWhiteTone(std::vector<uint8_t>& tone) const
{
    if (!m_hasExternalCurves)
        return false;

    for (uint8_t& value : tone)
        value = m_w[value];
    return true;
}
