#include "ColorManagementManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QDebug>
#include <algorithm>


// ------------------------- helpers -------------------------

static bool isSupportedInkMode(int inkMode) {
    switch (inkMode) {
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}


static QString familyKeyForInkMode(int inkMode) {
    switch (inkMode) {
		case 4:
		case 5:
		    return "A";
		case 6:
		case 7:
		    return "B";
		case 8:
		case 10:
		    return "C";
		default:
			return QString();
    }
}


static bool isSupportedFamilyKey(const QString& familyKey) {
    return familyKey == "A" || familyKey == "B" || familyKey == "C";
}


static QString normalizedPrinterName(const QString& printerName) {
    const QString name = printerName.trimmed();
    if (name.compare(QStringLiteral("X-36 Studio"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("X-36NC (Photo Printer)"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("X-36NC"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("X-36 Studio");
    }
    return name;
}


static QVariantMap jsonObjectToVariantMap(const QJsonObject& o) {
    QVariantMap m;
    for (auto it = o.begin(); it != o.end(); ++it) {
        // Keep everything scalar; if you later add nested objects, you can extend this.
        m.insert(it.key(), it.value().toVariant());
    }
    return m;
}


static QJsonObject variantMapToJsonObject(const QVariantMap& m) {
    QJsonObject o;
    for (auto it = m.begin(); it != m.end(); ++it) {
        o.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    return o;
}


static QVariantMap defaultDirectPrintSettings() {
    QVariantMap m;
    m["selectedPrinterIndex"] = -1;
    m["selectedPrinterName"] = QString();
    m["printDirection"] = 0;
    m["printSpeed"] = 1;
    m["wcSequence"] = 0;
    m["eclosionGrade"] = 2;
    m["headSelect"] = 0;
    m["whiteInkPercent"] = 0;
    m["whiteInkPassCount"] = 0;
    m["varnishInkPercent"] = 0;
    m["varnishInkPassCount"] = 0;
    m["headVoltage"] = 512;
    m["disableUv0"] = 0;
    m["disableUv1"] = 0;
    m["disableUv2"] = 0;
    m["disableUv3"] = 0;
    m["disableUv4"] = 0;
    m["disableUv5"] = 0;
    m["carReset"] = 0;
    m["stripBlank"] = 0;
    m["blankDistance"] = 0;
    m["pass"] = 0;
    m["vsdMode"] = 0;
    return m;
}


static QVariantMap normalizedDirectPrintSettings(const QVariantMap& in) {
    QVariantMap out = defaultDirectPrintSettings();
    for (auto it = in.begin(); it != in.end(); ++it)
        out[it.key()] = it.value();

    auto clamp = [&](const char* key, int lo, int hi) {
        const QString k = QString::fromUtf8(key);
        out[k] = std::max(lo, std::min(hi, out.value(k).toInt()));
    };

    clamp("selectedPrinterIndex", -1, 99);
    out["selectedPrinterName"] = out.value("selectedPrinterName").toString().trimmed();
    clamp("printDirection", 0, 3);
    clamp("printSpeed", 0, 3);
    clamp("wcSequence", 0, 1);
    const int savedEclosionGrade = out.value("eclosionGrade", 2).toInt();
    // Grade zero was the old default and disables feathering. The UI now
    // exposes the three usable levels, so migrate old zero values to Medium.
    out["eclosionGrade"] = savedEclosionGrade <= 0
        ? 2
        : std::clamp(savedEclosionGrade, 1, 3);
    clamp("headSelect", 0, 2);
    clamp("whiteInkPercent", 0, 9);
    clamp("whiteInkPassCount", 0, 255);
    clamp("varnishInkPercent", 0, 9);
    clamp("varnishInkPassCount", 0, 255);
    clamp("headVoltage", 400, 600);
    clamp("disableUv0", 0, 1);
    clamp("disableUv1", 0, 1);
    clamp("disableUv2", 0, 1);
    clamp("disableUv3", 0, 1);
    clamp("disableUv4", 0, 1);
    clamp("disableUv5", 0, 1);
    clamp("carReset", 0, 1);
    clamp("stripBlank", 0, 2);
    clamp("blankDistance", 0, 65535);
    clamp("pass", 0, 255);
    clamp("vsdMode", 0, 65535);
    return out;
}


static QVariantMap clampMapU8(const QVariantMap& in) {
    QVariantMap out = in;

    auto clampInt = [&](const char* k, int lo, int hi) {
        const QString key = QString::fromUtf8(k);
        if (!out.contains(key)) return;
        int v = out.value(key).toInt();
        v = std::max(lo, std::min(hi, v));
        out[key] = v;
    };

    // Common u8-ish params (0..255)
    const char* u8Keys[] = {
        "cLightStart","cLightEnd","mLightStart","mLightEnd",
        "kT1Start","kT1End","kT2Start","kT2End",
        "gcrEnabled","neutralGate","gcrMaxTone","gcrStrength","kGain","kMinInNeutral",
        "lightInkMinThreshold",

        "promoToneGate","promoMedLo","promoMedHi","promoLrgLo","promoLrgHi",
        "promoFlatVarEps","promoMinNeiInked","promoKickBonus",

        "neutralChromaCut","neutralMaxTone",

        "whiteThreshold","whiteDensity",
        "varnishThreshold","varnishDensity",

        "whiteSmallDotThreshold","whiteMedDotThreshold",
        "varnishSmallDotThreshold","varnishMedDotThreshold"
    };

    for (auto k : u8Keys) clampInt(k, 0, 255);

    // Percent keys (0..100)
    auto clampPct = [&](const char* k) {
        const QString key = QString::fromUtf8(k);
        if (!out.contains(key)) return;
        int v = out.value(key).toInt();
        v = std::max(0, std::min(100, v));
        out[key] = v;
    };
    clampPct("lcFadePctInNeutral");
    clampPct("lmFadePctInNeutral");
    clampPct("neutralReducePct1");
    clampPct("neutralReducePct2");

    // Enumerated small-range ints
    clampInt("whiteMode",   0, 3); // 0=Off,1=AutoUnderbase,2=Flood,3=Plate
    clampInt("varnishMode", 0, 3); // 0=Off,1=Overprint,2=Flood,3=Plate

    // Booleans (normalize)
    auto normBool = [&](const char* k) {
        const QString key = QString::fromUtf8(k);
        if (!out.contains(key)) return;
        out[key] = out.value(key).toBool();
    };

    normBool("useLightInkMinThresholdOverride");
    
    normBool("gcrEnabled");

    normBool("whiteUseOwnDotStrategy");
    normBool("whiteEnablePromotion");

    normBool("varnishUseOwnDotStrategy");
    normBool("varnishEnablePromotion");

    // String defaults if present
    auto normString = [&](const char* k, const QString& def = QString()) {
        const QString key = QString::fromUtf8(k);
        if (!out.contains(key)) {
            if (!def.isNull()) out[key] = def;
            return;
        }
        out[key] = out.value(key).toString().trimmed();
    };

    normString("whiteMaskKey", "w");
    normString("varnishMaskKey", "v");

    return out;
}



// ------------------------- ctor -------------------------

ColorManagementManager::ColorManagementManager(QObject* parent)
    : QObject(parent)
{
    if (!load()) {
        resetToDefaults();
        // Optional: persist defaults on first run.
        // save();
    }
}



// ------------------------- profiles -------------------------

QString ColorManagementManager::selectedPrinter() const { return m_selectedPrinter; }


void ColorManagementManager::setSelectedPrinter(const QString& p) {
    const QString normalized = normalizedPrinterName(p);
    if (m_selectedPrinter == normalized) return;
    m_selectedPrinter = normalized;
    emit selectedPrinterChanged();
    emit profilesChanged();
}


QString ColorManagementManager::defaultInputProfile() const { return m_defaultInputProfile; }


void ColorManagementManager::setDefaultInputProfile(const QString& p) {
    if (m_defaultInputProfile == p) return;
    m_defaultInputProfile = p;
    emit profilesChanged();
}


QString ColorManagementManager::defaultOutputProfile() const { return m_defaultOutputProfile; }


void ColorManagementManager::setDefaultOutputProfile(const QString& p) {
    if (m_defaultOutputProfile == p) return;
    m_defaultOutputProfile = p;
    emit profilesChanged();
}


QString ColorManagementManager::effectiveOutputProfile() const {
    const auto key = m_selectedPrinter;
    if (!key.isEmpty()) {
        const auto it = m_printerOutputProfiles.constFind(key);
        if (it != m_printerOutputProfiles.constEnd()) {
            const QString v = it.value().toString();
            if (!v.trimmed().isEmpty()) return v.trimmed();
        }
    }
    return m_defaultOutputProfile.trimmed();
}


QString ColorManagementManager::printerOutputProfile(const QString& printerName) const {
    return m_printerOutputProfiles.value(normalizedPrinterName(printerName)).toString();
}


void ColorManagementManager::setPrinterOutputProfile(const QString& printerName, const QString& profilePath) {
    if (printerName.trimmed().isEmpty()) return;

    const QString key = normalizedPrinterName(printerName);
    const QString newVal = profilePath.trimmed();
    const QString cur = m_printerOutputProfiles.value(key).toString();

    if (cur == newVal) return;
    m_printerOutputProfiles.insert(key, newVal);
    emit profilesChanged();
}


QString ColorManagementManager::multiInkOutputMode() const {
    return m_multiInkOutputMode;
}


void ColorManagementManager::setMultiInkOutputMode(const QString& mode) {
    const QString normalized = (mode.trimmed().toLower() == "direct") ? "direct" : "prn";
    if (m_multiInkOutputMode == normalized)
        return;

    m_multiInkOutputMode = normalized;
    emit directPrintSettingsChanged();
    save();
}


QString ColorManagementManager::directPrintSdkRootPath() const {
    return m_directPrintSdkRootPath;
}


void ColorManagementManager::setDirectPrintSdkRootPath(const QString& path) {
    const QString clean = path.trimmed();
    if (m_directPrintSdkRootPath == clean)
        return;

    m_directPrintSdkRootPath = clean;
    emit directPrintSettingsChanged();
    save();
}

QString ColorManagementManager::multiInkDirectPrintSdkRootPath() const {
    return m_multiInkDirectPrintSdkRootPath;
}


void ColorManagementManager::setMultiInkDirectPrintSdkRootPath(const QString& path) {
    const QString clean = path.trimmed();
    if (m_multiInkDirectPrintSdkRootPath == clean)
        return;

    m_multiInkDirectPrintSdkRootPath = clean;
    emit directPrintSettingsChanged();
    save();
}


QString ColorManagementManager::directPrintSdkFamilyForPrinter(const QString& printerName) const {
    const QString name = normalizedPrinterName(printerName);
    if (name.compare(QStringLiteral("X-33"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("legacy-cmyk");
    if (name.compare(QStringLiteral("X-36 Studio"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("multi-ink");
    return QString();
}


QString ColorManagementManager::directPrintSdkRootForPrinter(const QString& printerName) const {
    return directPrintSdkFamilyForPrinter(printerName) == QStringLiteral("multi-ink")
        ? m_multiInkDirectPrintSdkRootPath
        : m_directPrintSdkRootPath;
}


QVariantMap ColorManagementManager::directPrintSettings() const {
    return normalizedDirectPrintSettings(m_directPrintSettings);
}


void ColorManagementManager::setDirectPrintSettings(const QVariantMap& settings) {
    const QVariantMap normalized = normalizedDirectPrintSettings(settings);
    if (m_directPrintSettings == normalized)
        return;

    m_directPrintSettings = normalized;
    emit directPrintSettingsChanged();
    save();
}


QVariant ColorManagementManager::directPrintSetting(const QString& key) const {
    return directPrintSettings().value(key);
}


void ColorManagementManager::setDirectPrintSetting(const QString& key, const QVariant& value) {
    if (key.trimmed().isEmpty())
        return;

    QVariantMap settings = directPrintSettings();
    settings[key.trimmed()] = value;
    setDirectPrintSettings(settings);
}


QString ColorManagementManager::outputProfileFamilyForInkMode(int inkMode) const {
    return familyKeyForInkMode(inkMode);
}


QString ColorManagementManager::familyDefaultOutputProfile(const QString& familyKey) const {
    const QString key = familyKey.trimmed().toUpper();
    if (!isSupportedFamilyKey(key))
        return QString();
    return m_familyDefaultOutputProfiles.value(key).toString().trimmed();
}


void ColorManagementManager::setFamilyDefaultOutputProfile(const QString& familyKey, const QString& profilePath) {
    const QString key = familyKey.trimmed().toUpper();
    if (!isSupportedFamilyKey(key))
        return;

    const QString newVal = profilePath.trimmed();
    const QString cur = m_familyDefaultOutputProfiles.value(key).toString();
    if (cur == newVal)
        return;

    m_familyDefaultOutputProfiles.insert(key, newVal);
    emit profilesChanged();
    save();
}


QString ColorManagementManager::printerFamilyOutputProfile(const QString& printerName, const QString& familyKey) const {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString key = familyKey.trimmed().toUpper();

    if (printerKey.isEmpty() || !isSupportedFamilyKey(key))
        return QString();

    const QVariantMap familyMap = m_printerFamilyOutputProfiles.value(printerKey).toMap();
    return familyMap.value(key).toString().trimmed();
}


void ColorManagementManager::setPrinterFamilyOutputProfile(const QString& printerName, const QString& familyKey, const QString& profilePath) {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString key = familyKey.trimmed().toUpper();

    if (printerKey.isEmpty() || !isSupportedFamilyKey(key))
        return;

    QVariantMap familyMap = m_printerFamilyOutputProfiles.value(printerKey).toMap();

    const QString newVal = profilePath.trimmed();
    const QString cur = familyMap.value(key).toString();
    if (cur == newVal)
        return;

    familyMap.insert(key, newVal);
    m_printerFamilyOutputProfiles.insert(printerKey, familyMap);

    emit profilesChanged();
    save();
}


QString ColorManagementManager::effectiveOutputProfileForPrinterAndInkMode(const QString& printerName, int inkMode) const {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString familyKey = familyKeyForInkMode(inkMode);

    // 1. Printer settings override
    if (!printerKey.isEmpty() && !familyKey.isEmpty()) {
        const QVariantMap familyMap = m_printerFamilyOutputProfiles.value(printerKey).toMap();
        const QString printerFamilyProfile = familyMap.value(familyKey).toString().trimmed();
        if (!printerFamilyProfile.isEmpty())
            return printerFamilyProfile;
    }

    // 2. Family default
    if (!familyKey.isEmpty()) {
        const QString familyDefault = m_familyDefaultOutputProfiles.value(familyKey).toString().trimmed();
        if (!familyDefault.isEmpty())
            return familyDefault;
    }

    // 3. No global default in new path
    return QString();
}



// ------------------------- dot strategy -------------------------

int ColorManagementManager::minInkThreshold() const { return m_minInkThreshold; }


void ColorManagementManager::setMinInkThreshold(int v) {
    v = std::clamp(v, 0, 255);
    if (m_minInkThreshold == v) return;
    m_minInkThreshold = v;
    emit dotStrategyChanged();
}


int ColorManagementManager::smallDotThreshold() const { return m_smallDotThreshold; }


void ColorManagementManager::setSmallDotThreshold(int v) {
    v = std::clamp(v, 0, 255);
    if (m_smallDotThreshold == v) return;
    m_smallDotThreshold = v;
    emit dotStrategyChanged();
}

int ColorManagementManager::medDotThreshold() const { return m_medDotThreshold; }

void ColorManagementManager::setMedDotThreshold(int v) {
    v = std::clamp(v, 0, 255);
    if (m_medDotThreshold == v) return;
    m_medDotThreshold = v;
    emit dotStrategyChanged();
}


bool ColorManagementManager::enablePromotion() const { return m_enablePromotion; }


void ColorManagementManager::setEnablePromotion(bool v) {
    if (m_enablePromotion == v) return;
    m_enablePromotion = v;
    emit dotStrategyChanged();
}

// Added: floor gating + dot swap

int ColorManagementManager::floorRangeCMY() const { return m_floorRangeCMY; }


void ColorManagementManager::setFloorRangeCMY(int v) {
    v = std::clamp(v, 0, 64);
    if (m_floorRangeCMY == v) return;
    m_floorRangeCMY = v;
    emit dotStrategyChanged();
}


int ColorManagementManager::floorMaxCMY() const { return m_floorMaxCMY; }


void ColorManagementManager::setFloorMaxCMY(int v) {
    v = std::clamp(v, 0, 8);
    if (m_floorMaxCMY == v) return;
    m_floorMaxCMY = v;
    emit dotStrategyChanged();
}


int ColorManagementManager::floorRangeK() const { return m_floorRangeK; }


void ColorManagementManager::setFloorRangeK(int v) {
    v = std::clamp(v, 0, 64);
    if (m_floorRangeK == v) return;
    m_floorRangeK = v;
    emit dotStrategyChanged();
}

int ColorManagementManager::floorMaxK() const { return m_floorMaxK; }


void ColorManagementManager::setFloorMaxK(int v) {
    v = std::clamp(v, 0, 8);
    if (m_floorMaxK == v) return;
    m_floorMaxK = v;
    emit dotStrategyChanged();
}


bool ColorManagementManager::enableDotSwap() const { return m_enableDotSwap; }


void ColorManagementManager::setEnableDotSwap(bool v) {
    if (m_enableDotSwap == v) return;
    m_enableDotSwap = v;
    emit dotStrategyChanged();
}


// ------------------------- Linearization Params -------------------------
bool ColorManagementManager::linearizationEnabled() const {
    return m_enableLinearization;
}


void ColorManagementManager::setLinearizationEnabled(bool enabled) {
    if (m_enableLinearization == enabled) return;
    m_enableLinearization = enabled;
    emit linearizationChanged();
}


QString ColorManagementManager::linearizationPresetName() const {
    return m_linearizationPresetName;
}

void ColorManagementManager::setLinearizationPresetName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (m_linearizationPresetName == trimmed) return;
    m_linearizationPresetName = trimmed;
    emit linearizationChanged();
}


QString ColorManagementManager::linearizationDataPath() const {
    return m_linearizationDataPath;
}


void ColorManagementManager::setLinearizationDataPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (m_linearizationDataPath == trimmed) return;
    m_linearizationDataPath = trimmed;
    emit linearizationChanged();
}


QString ColorManagementManager::familyDefaultLinearizationPath(const QString& familyKey) const {
    const QString key = familyKey.trimmed().toUpper();
    if (!isSupportedFamilyKey(key))
        return QString();
    return m_familyDefaultLinearizationPaths.value(key).toString().trimmed();
}


void ColorManagementManager::setFamilyDefaultLinearizationPath(const QString& familyKey, const QString& xmlPath) {
    const QString key = familyKey.trimmed().toUpper();
    if (!isSupportedFamilyKey(key))
        return;

    const QString newVal = xmlPath.trimmed();
    const QString cur = m_familyDefaultLinearizationPaths.value(key).toString();
    if (cur == newVal)
        return;

    m_familyDefaultLinearizationPaths.insert(key, newVal);
    emit linearizationChanged();
    save();
}


QString ColorManagementManager::printerFamilyLinearizationPath(const QString& printerName, const QString& familyKey) const {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString key = familyKey.trimmed().toUpper();

    if (printerKey.isEmpty() || !isSupportedFamilyKey(key))
        return QString();

    const QVariantMap familyMap = m_printerFamilyLinearizationPaths.value(printerKey).toMap();
    return familyMap.value(key).toString().trimmed();
}


void ColorManagementManager::setPrinterFamilyLinearizationPath(const QString& printerName, const QString& familyKey, const QString& xmlPath) {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString key = familyKey.trimmed().toUpper();

    if (printerKey.isEmpty() || !isSupportedFamilyKey(key))
        return;

    QVariantMap familyMap = m_printerFamilyLinearizationPaths.value(printerKey).toMap();

    const QString newVal = xmlPath.trimmed();
    const QString cur = familyMap.value(key).toString();
    if (cur == newVal)
        return;

    familyMap.insert(key, newVal);
    m_printerFamilyLinearizationPaths.insert(printerKey, familyMap);

    emit linearizationChanged();
    save();
}


QString ColorManagementManager::effectiveLinearizationPathForPrinterAndInkMode(const QString& printerName, int inkMode) const {
    const QString printerKey = normalizedPrinterName(printerName);
    const QString familyKey = familyKeyForInkMode(inkMode);

    // 1. Printer family override
    if (!printerKey.isEmpty() && !familyKey.isEmpty()) {
        const QVariantMap familyMap = m_printerFamilyLinearizationPaths.value(printerKey).toMap();
        const QString printerFamilyXml = familyMap.value(familyKey).toString().trimmed();
        if (!printerFamilyXml.isEmpty())
            return printerFamilyXml;
    }

    // 2. Family default
    if (!familyKey.isEmpty()) {
        const QString familyDefaultXml = m_familyDefaultLinearizationPaths.value(familyKey).toString().trimmed();
        if (!familyDefaultXml.isEmpty())
            return familyDefaultXml;
    }

    // 3. Legacy fallback
    return m_linearizationDataPath.trimmed();
}


// ------------------------- multi-ink per-mode params -------------------------

QVariantMap ColorManagementManager::defaultMultiInkParamsForMode(int inkMode) const {
    QVariantMap m;

    // Shared defaults (dot promotion thresholds)
    m["promoToneGate"]     = 112;
    m["promoMedLo"]        = 18;
    m["promoMedHi"]        = 26;
    m["promoLrgLo"]        = 28;
    m["promoLrgHi"]        = 36;
    m["promoFlatVarEps"]   = 18;
    m["promoMinNeiInked"]  = 8;
    m["promoKickBonus"]    = 2;

    // Optional light-ink min threshold override
    m["useLightInkMinThresholdOverride"] = true;
    m["lightInkMinThreshold"] = 2;
    
    // White:
    //   0 = Off
    //   1 = Auto Underbase
    //   2 = Flood
    //   3 = Plate
    m["whiteMode"] = 0;
    m["whiteThreshold"] = 8;
    m["whiteDensity"] = 255;
    m["whiteUseOwnDotStrategy"] = false;
    m["whiteSmallDotThreshold"] = 104;
    m["whiteMedDotThreshold"] = 168;
    m["whiteEnablePromotion"] = false;
    m["whiteMaskKey"] = "w";

    // Varnish:
    //   0 = Off
    //   1 = Overprint
    //   2 = Flood
    //   3 = Plate
    m["varnishMode"] = 0;
    m["varnishThreshold"] = 8;
    m["varnishDensity"] = 255;
    m["varnishUseOwnDotStrategy"] = false;
    m["varnishSmallDotThreshold"] = 104;
    m["varnishMedDotThreshold"] = 168;
    m["varnishEnablePromotion"] = false;
    m["varnishMaskKey"] = "v";
    
    auto addGcrDefaults = [&]() {
		// GCR / neutral controls (used by 5/6/7/8/10; 4 does not use these)
	    m["gcrEnabled"]         = false;
		m["neutralGate"]        = 14;
		m["gcrMaxTone"]         = 180;
		m["gcrStrength"]        = 160;
		m["kGain"]              = 220;
		m["kMinInNeutral"]      = 6;
		m["lcFadePctInNeutral"] = 70;
		m["lmFadePctInNeutral"] = 50;
	};

    // Mode-specific defaults mirror your current hard-coded values
    // NOTE: 4/5 have no split params; still return promo + lightInkMinThreshold defaults.
    if (inkMode == 5) {
    	addGcrDefaults();
	m["whiteMode"] = 1; // Auto Underbase
    }
    else if (inkMode == 6) {
       	addGcrDefaults();
       	
        m["cLightStart"] = 24;  m["cLightEnd"] = 96;
        m["mLightStart"] = 16;  m["mLightEnd"] = 80;
    }
    else if (inkMode == 7) {
	addGcrDefaults();

        m["cLightStart"] = 48;  m["cLightEnd"] = 160;
        m["mLightStart"] = 48;  m["mLightEnd"] = 160;
        
        m["whiteMode"] = 1; // Auto Underbase
    }
    else if (inkMode == 8) {
	addGcrDefaults();
	    
        m["cLightStart"] = 24;  m["cLightEnd"] = 96;
        m["mLightStart"] = 16;  m["mLightEnd"] = 80;

        m["kT1Start"] = 24;  m["kT1End"] = 112;
        m["kT2Start"] = 120; m["kT2End"] = 228;
    }
    else if (inkMode == 10) {
	addGcrDefaults();
		
        m["cLightStart"] = 48;  m["cLightEnd"] = 160;
        m["mLightStart"] = 48;  m["mLightEnd"] = 160;

        m["kT1Start"] = 32;  m["kT1End"] = 128;
        m["kT2Start"] = 128; m["kT2End"] = 224;
        
        m["whiteMode"] = 1; // Auto Underbase
        m["varnishMode"] = 1; // Overprint
    }

    return clampMapU8(m);
}


QVariantMap ColorManagementManager::getMultiInkParams(int inkMode) const {
    if (!isSupportedInkMode(inkMode))
        return QVariantMap();

    // Only 6/7/8/10 actually use split params today, but we support 4/5 too (returns defaults).
    if (m_multiInkParamsByMode.contains(inkMode))
        return m_multiInkParamsByMode.value(inkMode);

    return defaultMultiInkParamsForMode(inkMode);
}


void ColorManagementManager::setMultiInkParams(int inkMode, const QVariantMap& params) {
    if (!isSupportedInkMode(inkMode))
        return;

    // Merge: default keys + overrides
    QVariantMap merged = defaultMultiInkParamsForMode(inkMode);
    for (auto it = params.begin(); it != params.end(); ++it)
        merged[it.key()] = it.value();

    merged = clampMapU8(merged);
    m_multiInkParamsByMode[inkMode] = merged;

	emit multiInkParamsChanged();

    save();
}

QVariant ColorManagementManager::getMultiInkParam(int inkMode, const QString& key) const {
    return getMultiInkParams(inkMode).value(key);
}


void ColorManagementManager::setMultiInkParam(int inkMode, const QString& key, const QVariant& value) {
    if (!isSupportedInkMode(inkMode)) return;
    QVariantMap m = getMultiInkParams(inkMode); // merged defaults
    m[key] = value;
    setMultiInkParams(inkMode, m);              // clamps + saves + emits
}


QVariantMap ColorManagementManager::getWhiteParams(int inkMode) const {
    QVariantMap src = getMultiInkParams(inkMode);
    QVariantMap out;
    out["whiteMode"] = src.value("whiteMode");
    out["whiteThreshold"] = src.value("whiteThreshold");
    out["whiteDensity"] = src.value("whiteDensity");
    out["whiteUseOwnDotStrategy"] = src.value("whiteUseOwnDotStrategy");
    out["whiteSmallDotThreshold"] = src.value("whiteSmallDotThreshold");
    out["whiteMedDotThreshold"] = src.value("whiteMedDotThreshold");
    out["whiteEnablePromotion"] = src.value("whiteEnablePromotion");
    out["whiteMaskKey"] = src.value("whiteMaskKey");
    return out;
}


QVariantMap ColorManagementManager::getVarnishParams(int inkMode) const {
    QVariantMap src = getMultiInkParams(inkMode);
    QVariantMap out;
    out["varnishMode"] = src.value("varnishMode");
    out["varnishThreshold"] = src.value("varnishThreshold");
    out["varnishDensity"] = src.value("varnishDensity");
    out["varnishUseOwnDotStrategy"] = src.value("varnishUseOwnDotStrategy");
    out["varnishSmallDotThreshold"] = src.value("varnishSmallDotThreshold");
    out["varnishMedDotThreshold"] = src.value("varnishMedDotThreshold");
    out["varnishEnablePromotion"] = src.value("varnishEnablePromotion");
    out["varnishMaskKey"] = src.value("varnishMaskKey");
    return out;
}


// ------------------------- persistence (single JSON file) -------------------------

QString ColorManagementManager::_settingsPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QDir::separator() + "color_management.json";
}


bool ColorManagementManager::load() {
    QFile f(_settingsPath());
    if (!f.exists()) return false;
    if (!f.open(QIODevice::ReadOnly)) return false;

    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;

    const auto o = doc.object();

    m_selectedPrinter = normalizedPrinterName(
        o.value("selectedPrinter").toString(m_selectedPrinter));

    // Profiles
    m_defaultInputProfile  = o.value("defaultInputProfile").toString(m_defaultInputProfile);
    m_defaultOutputProfile = o.value("defaultOutputProfile").toString(m_defaultOutputProfile);
    
    // Dot strategy
    setMinInkThreshold(o.value("minInkThreshold").toInt(m_minInkThreshold));
    setSmallDotThreshold(o.value("smallDotThreshold").toInt(m_smallDotThreshold));
    setMedDotThreshold(o.value("medDotThreshold").toInt(m_medDotThreshold));
    setEnablePromotion(o.value("enablePromotion").toBool(m_enablePromotion));

    setFloorRangeCMY(o.value("floorRangeCMY").toInt(m_floorRangeCMY));
    setFloorMaxCMY(o.value("floorMaxCMY").toInt(m_floorMaxCMY));
    setFloorRangeK(o.value("floorRangeK").toInt(m_floorRangeK));
    setFloorMaxK(o.value("floorMaxK").toInt(m_floorMaxK));
    setEnableDotSwap(o.value("enableDotSwap").toBool(m_enableDotSwap));
    
    // Linearization
    setLinearizationEnabled(o.value("enableLinearization").toBool(m_enableLinearization));
    setLinearizationPresetName(o.value("linearizationPresetName").toString(m_linearizationPresetName));
    setLinearizationDataPath(o.value("linearizationDataPath").toString(m_linearizationDataPath));

    // Direct print settings
    m_multiInkOutputMode = (o.value("multiInkOutputMode").toString(m_multiInkOutputMode).trimmed().toLower() == "direct")
        ? "direct"
        : "prn";
    m_directPrintSdkRootPath = o.value("directPrintSdkRootPath").toString(m_directPrintSdkRootPath).trimmed();
    m_multiInkDirectPrintSdkRootPath =
        o.value("multiInkDirectPrintSdkRootPath").toString(m_multiInkDirectPrintSdkRootPath).trimmed();
    m_directPrintSettings = normalizedDirectPrintSettings(
        jsonObjectToVariantMap(o.value("directPrintSettings").toObject()));

    // Printer profile map
    m_printerOutputProfiles.clear();
    const auto mapObj = o.value("printerOutputProfiles").toObject();
    for (auto it = mapObj.begin(); it != mapObj.end(); ++it)
        m_printerOutputProfiles.insert(normalizedPrinterName(it.key()), it.value().toString());

    // Multi-ink params by mode
    m_multiInkParamsByMode.clear();
    const QJsonObject miObj = o.value("multiInkParamsByMode").toObject();
    for (auto it = miObj.begin(); it != miObj.end(); ++it) {
        bool ok = false;
        const int mode = it.key().toInt(&ok);
        if (!ok || !isSupportedInkMode(mode))
            continue;
        const QJsonObject paramsObj = it.value().toObject();
        QVariantMap params = jsonObjectToVariantMap(paramsObj);

        // Merge over defaults (for forward/backward compatibility)
        QVariantMap merged = defaultMultiInkParamsForMode(mode);
        for (auto pit = params.begin(); pit != params.end(); ++pit)
            merged[pit.key()] = pit.value();

        m_multiInkParamsByMode[mode] = clampMapU8(merged);
    }
    
    // Family default linearization XML paths
    m_familyDefaultLinearizationPaths.clear();
    const auto familyLinObj = o.value("familyDefaultLinearizationPaths").toObject();
    for (auto it = familyLinObj.begin(); it != familyLinObj.end(); ++it)
        m_familyDefaultLinearizationPaths.insert(it.key(), it.value().toString());

    // Printer+family linearization XML overrides
    m_printerFamilyLinearizationPaths.clear();
    const auto printerFamilyLinObj = o.value("printerFamilyLinearizationPaths").toObject();
    for (auto it = printerFamilyLinObj.begin(); it != printerFamilyLinObj.end(); ++it) {
        m_printerFamilyLinearizationPaths.insert(
            normalizedPrinterName(it.key()), jsonObjectToVariantMap(it.value().toObject()));
    }
    
	// Printer Profile family defaults
    m_familyDefaultOutputProfiles.clear();
    const auto familyDefaultsObj = o.value("familyDefaultOutputProfiles").toObject();
    for (auto it = familyDefaultsObj.begin(); it != familyDefaultsObj.end(); ++it)
        m_familyDefaultOutputProfiles.insert(it.key(), it.value().toString());

    // Printer+family overrides
    m_printerFamilyOutputProfiles.clear();
    const auto printerFamilyObj = o.value("printerFamilyOutputProfiles").toObject();
    for (auto it = printerFamilyObj.begin(); it != printerFamilyObj.end(); ++it) {
        m_printerFamilyOutputProfiles.insert(
            normalizedPrinterName(it.key()), jsonObjectToVariantMap(it.value().toObject()));
    }

    emit profilesChanged();
    emit linearizationChanged();
    emit dotStrategyChanged();
    emit multiInkParamsChanged();
    emit directPrintSettingsChanged();
    return true;
}


bool ColorManagementManager::save() {
    QJsonObject o;

    o["selectedPrinter"] = m_selectedPrinter;

    // Profiles
    o["defaultInputProfile"]  = m_defaultInputProfile;
    o["defaultOutputProfile"] = m_defaultOutputProfile;

    // Dot strategy
    o["minInkThreshold"]    = m_minInkThreshold;
    o["smallDotThreshold"]  = m_smallDotThreshold;
    o["medDotThreshold"]    = m_medDotThreshold;
    o["enablePromotion"]    = m_enablePromotion;

    o["floorRangeCMY"]      = m_floorRangeCMY;
    o["floorMaxCMY"]        = m_floorMaxCMY;
    o["floorRangeK"]        = m_floorRangeK;
    o["floorMaxK"]          = m_floorMaxK;
    o["enableDotSwap"]      = m_enableDotSwap;
    
    // Linearization
    o["enableLinearization"] = m_enableLinearization;
    o["linearizationPresetName"] = m_linearizationPresetName;
    o["linearizationDataPath"] = m_linearizationDataPath;

    // Direct print settings
    o["multiInkOutputMode"] = m_multiInkOutputMode;
    o["directPrintSdkRootPath"] = m_directPrintSdkRootPath;
    o["multiInkDirectPrintSdkRootPath"] = m_multiInkDirectPrintSdkRootPath;
    o["directPrintSettings"] = variantMapToJsonObject(normalizedDirectPrintSettings(m_directPrintSettings));
    
	// Family default linearization XML paths
    QJsonObject familyLinObj;
    for (auto it = m_familyDefaultLinearizationPaths.begin(); it != m_familyDefaultLinearizationPaths.end(); ++it)
        familyLinObj[it.key()] = it.value().toString();
    o["familyDefaultLinearizationPaths"] = familyLinObj;

    // Printer+family linearization XML overrides
    QJsonObject printerFamilyLinObj;
    for (auto it = m_printerFamilyLinearizationPaths.begin(); it != m_printerFamilyLinearizationPaths.end(); ++it)
        printerFamilyLinObj[it.key()] = variantMapToJsonObject(it.value().toMap());
    o["printerFamilyLinearizationPaths"] = printerFamilyLinObj;

    // Printer overrides
    QJsonObject mapObj;
    for (auto it = m_printerOutputProfiles.begin(); it != m_printerOutputProfiles.end(); ++it)
        mapObj[it.key()] = it.value().toString();
    o["printerOutputProfiles"] = mapObj;
    
	// Family defaults
    QJsonObject familyDefaultsObj;
    for (auto it = m_familyDefaultOutputProfiles.begin(); it != m_familyDefaultOutputProfiles.end(); ++it)
        familyDefaultsObj[it.key()] = it.value().toString();
    o["familyDefaultOutputProfiles"] = familyDefaultsObj;

    // Pprinter+family overrides
    QJsonObject printerFamilyObj;
    for (auto it = m_printerFamilyOutputProfiles.begin(); it != m_printerFamilyOutputProfiles.end(); ++it)
        printerFamilyObj[it.key()] = variantMapToJsonObject(it.value().toMap());
    o["printerFamilyOutputProfiles"] = printerFamilyObj;

    // Multi-ink params by mode
    QJsonObject miObj;
    for (auto it = m_multiInkParamsByMode.begin(); it != m_multiInkParamsByMode.end(); ++it) {
        const int mode = it.key();
        if (!isSupportedInkMode(mode)) continue;
        miObj[QString::number(mode)] = variantMapToJsonObject(clampMapU8(it.value()));
    }
    o["multiInkParamsByMode"] = miObj;

    QFile f(_settingsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ColorManagementManager::save: FAILED to open" << _settingsPath();
        return false;
    }

    const auto bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
    const auto written = f.write(bytes);
    const bool ok = (written == bytes.size());

    if (!ok) {
        qWarning() << "ColorManagementManager::save: FAILED write"
                   << written << "of" << bytes.size() << "bytes to" << _settingsPath();
        return false;
    }

    return true;
}


void ColorManagementManager::resetToDefaults() {
    m_selectedPrinter = QStringLiteral("X-33");
    // Profiles
    m_defaultInputProfile.clear();
    m_defaultOutputProfile.clear();
    m_printerOutputProfiles.clear();
    m_familyDefaultOutputProfiles.clear();
    m_printerFamilyOutputProfiles.clear();
    m_familyDefaultLinearizationPaths.clear();
    m_printerFamilyLinearizationPaths.clear();

    // Dot strategy: match your baseline + UI defaults
    m_minInkThreshold   = 8;
    m_smallDotThreshold = 64;
    m_medDotThreshold   = 128;
    m_enablePromotion   = false;

    m_floorRangeCMY = 24;
    m_floorMaxCMY   = 2;
    m_floorRangeK   = 12;
    m_floorMaxK     = 0;
    m_enableDotSwap = false;

    m_multiInkOutputMode = "prn";
    m_directPrintSdkRootPath.clear();
    m_multiInkDirectPrintSdkRootPath.clear();
    m_directPrintSettings = defaultDirectPrintSettings();
    
    // Linearization
    m_enableLinearization = true;
    m_linearizationPresetName = "Default";
    m_linearizationDataPath.clear();

    // Multi-ink per-mode params: seed defaults for supported modes
    m_multiInkParamsByMode.clear();
    for (int mode : {4,5,6,7,8,10}) {
        m_multiInkParamsByMode[mode] = defaultMultiInkParamsForMode(mode);
    }

    emit profilesChanged();
    emit linearizationChanged();
    emit dotStrategyChanged();
    emit multiInkParamsChanged();
    emit directPrintSettingsChanged();
}
