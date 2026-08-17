// Theme.qml
import QtQuick

QtObject {
    // Controlled by Main.qml (and later persisted if you want)
    property bool dark: true
    property var manager: themeManager

    // --- Dark palette (OpCharcoal + Teal) ---
    readonly property color _bgDark:       "#0F131A"
    readonly property color _surfaceDark:  "#14181F"
    readonly property color _surface2Dark: "#1A202A"
    readonly property color _textDark:     "#E6EAF2"
    readonly property color _subtextDark:  "#A7B0C0"
    readonly property color _dividerDark:  "#263042"

	// --- Light palette (soft, low-glare, professional) ---
	readonly property color _bgLight:       "#ECEFF4"  // soft gray-blue background
	readonly property color _surfaceLight:  "#F6F7FB"  // off-white surface (not pure)
	readonly property color _surface2Light: "#E3E7EF"  // stronger surface for cards/buttons
	readonly property color _textLight:     "#1F2937"  // softer than pure black
	readonly property color _subtextLight:  "#4B5563"  // good contrast
	readonly property color _dividerLight:  "#C7CEDB"  // stronger dividers

    // Accent (keep constant across themes)
    property color primary: manager ? manager.primaryColor : "#14181F"
    property color secondary: manager ? manager.secondaryColor : "#1FB8A6"
    property color accent: manager ? manager.accentColor : "#2DD4BF"
    property color accent2: secondary

    // Status (constant)
    property color danger:  "#EF4444"
    property color warning: "#F59E0B"

    // Public palette (switches based on dark flag)
    property color bg:       dark ? (manager ? manager.backgroundColor : _bgDark) : (manager ? manager.lightBackgroundColor : _bgLight)
    property color surface:  dark ? (manager ? manager.surfaceColor : _surfaceDark) : (manager ? manager.lightSurfaceColor : _surfaceLight)
    property color surface2: dark ? (manager ? manager.surface2Color : _surface2Dark) : (manager ? manager.lightSurface2Color : _surface2Light)
    property color text:     dark ? (manager ? manager.textColor : _textDark) : (manager ? manager.lightTextColor : _textLight)
    property color subtext:  dark ? (manager ? manager.subtextColor : _subtextDark) : (manager ? manager.lightSubtextColor : _subtextLight)
    property color divider:  dark ? (manager ? manager.dividerColor : _dividerDark) : (manager ? manager.lightDividerColor : _dividerLight)

    property string appName: manager ? manager.appName : "PrintFlow"
    property string displayName: manager ? manager.displayName : "Default"
    property string logoPath: manager && manager.logoPath.length > 0 ? manager.logoPath : "qrc:/assets/logo.png"
    property string splashLogoPath: manager && manager.splashLogoPath.length > 0 ? manager.splashLogoPath : logoPath
    property int logoWidth: manager && manager.logoWidth > 0 ? manager.logoWidth : 40
    property int logoHeight: manager && manager.logoHeight > 0 ? manager.logoHeight : 40
    property string aboutVendorName: manager ? manager.aboutVendorName : ""
    property string supportUrl: manager ? manager.supportUrl : ""
    property string copyrightText: manager ? manager.copyrightText : ""

    readonly property bool mobile: Qt.platform.os === "android" || Qt.platform.os === "ios"
    readonly property int defaultWindowWidth: 500
    readonly property int defaultWindowHeight: 720
    // Shared layout tokens. Android uses a restrained 8 dp grid and keeps
    // typography compact enough for a 360-420 dp phone viewport.
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 16
    readonly property int spaceXl: 24
    readonly property int pageMargin: mobile ? spaceMd : 12
    readonly property int panePadding: mobile ? spaceLg : 16
    readonly property int appBarHeight: mobile ? 56 : 60
    readonly property int controlHeight: mobile ? 48 : 40
    readonly property int compactControlHeight: mobile ? 44 : 40
    readonly property int cardRadius: mobile ? 14 : 12
    readonly property int controlRadius: mobile ? 12 : 8
    readonly property int bodyTextSize: mobile ? 14 : 14
    readonly property int labelTextSize: mobile ? 14 : 14
    readonly property int buttonTextSize: mobile ? 14 : 13
    readonly property int sectionTitleSize: mobile ? 16 : 18
    readonly property int pageTitleSize: mobile ? 20 : 20

    function boundedWidth(availableWidth, maxWidth) {
        if (availableWidth <= 0)
            return 0
        return mobile ? availableWidth : Math.min(availableWidth, maxWidth)
    }

    function headerButtonWidth(availableWidth) {
        if (mobile)
            return availableWidth < 380 ? 76 : 88
        if (availableWidth < 340)
            return 64
        return availableWidth < 380 ? 72 : 88
    }

    function headerTitleSize(availableWidth) {
        return availableWidth < 360 ? 18 : pageTitleSize
    }

    function formLabelWidth(availableWidth, preferredWidth) {
        if (availableWidth < 360)
            return Math.max(82, Math.floor(availableWidth * 0.30))
        if (availableWidth < 420)
            return Math.max(96, Math.floor(availableWidth * 0.34))
        return preferredWidth
    }

    function gridColumns(availableWidth, preferredColumns, minCellWidth) {
        if (availableWidth <= 0)
            return 1

        const columns = Math.max(1, Math.floor(availableWidth / minCellWidth))
        return Math.max(1, Math.min(preferredColumns, columns))
    }

    // Long action labels should stack on phones instead of being elided into
    // ambiguous fragments. Two columns remain available for short controls.
    function actionColumns(availableWidth, preferredColumns, minCellWidth) {
        if (mobile && availableWidth < 480)
            return 1
        return gridColumns(availableWidth, preferredColumns, minCellWidth)
    }
}
