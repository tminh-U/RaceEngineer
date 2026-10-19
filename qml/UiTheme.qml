import QtQuick

QtObject {
    readonly property color canvas: "#111111"
    readonly property color chrome: "#0b0b0b"
    readonly property color surface: "#1a1a1a"
    readonly property color raised: "#262626"
    readonly property color divider: "#383838"
    readonly property color outline: "#777777"
    readonly property color text: "#f0f0f0"
    readonly property color muted: "#b0b0b0"
    readonly property color accent: "#e0e0e0"
    readonly property color accentText: "#111111"
    readonly property color warning: "#b5b5b5"
    readonly property color error: "#858585"
    readonly property FontLoader sansRegular: FontLoader { source: "../assets/fonts/RaceEngineerSans-Regular.ttf" }
    readonly property FontLoader sansMedium: FontLoader { source: "../assets/fonts/RaceEngineerSans-Medium.ttf" }
    readonly property FontLoader sansSemiBold: FontLoader { source: "../assets/fonts/RaceEngineerSans-SemiBold.ttf" }
    readonly property FontLoader mono: FontLoader { source: "../assets/fonts/IBMPlexMono-Regular.ttf" }
    readonly property FontLoader display: FontLoader { source: "../assets/fonts/BarlowCondensed-SemiBold.ttf" }
    readonly property int uiWeight: Font.Medium
    readonly property int sectionWeight: Font.DemiBold
    readonly property int displayWeight: Font.Bold
    readonly property int monoWeight: Font.Bold
    readonly property string uiFont: sansMedium.status === FontLoader.Ready ? sansMedium.name : "Segoe UI"
    readonly property string monoFont: mono.status === FontLoader.Ready ? mono.name : "Consolas"
    readonly property string displayFont: display.status === FontLoader.Ready ? display.name : uiFont

    function pageTextSize(baseSize) {
        return Math.round(Number(baseSize) * 1.15)
    }

    function hasNumber(value) {
        return value !== undefined && value !== null && value !== "" && isFinite(Number(value))
    }
    function number(value, decimals, suffix) {
        return hasNumber(value) ? Number(value).toFixed(decimals).replace(".", ",") + (suffix || "") : "—"
    }
    function lap(value) {
        if (!hasNumber(value) || Number(value) < 0) return "—:—.———"
        var milliseconds = Math.round(Number(value) * 1000)
        return Math.floor(milliseconds / 60000) + ":"
            + String(Math.floor(milliseconds / 1000) % 60).padStart(2, "0") + "."
            + String(milliseconds % 1000).padStart(3, "0")
    }
    function gear(value) {
        if (!hasNumber(value)) return "—"
        return Number(value) < 0 ? "R" : Number(value) === 0 ? "N" : String(value)
    }
}
