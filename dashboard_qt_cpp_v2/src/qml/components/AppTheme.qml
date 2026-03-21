pragma Singleton

import QtQuick

QtObject {
    readonly property color windowBase:     "#05070b"
    readonly property color panel:          "#121821"
    readonly property color panelAlt:       "#18202b"
    readonly property color panelRaised:    "#1f2936"
    readonly property color panelStrong:    "#293648"
    readonly property color panelHover:     "#253142"
    readonly property color border:         "#2b3646"
    readonly property color borderSoft:     "#1a2230"
    readonly property color borderStrong:   "#435166"

    readonly property color chrome:          "#0d1219"
    readonly property color chromeElevated:  "#121923"
    readonly property color glassFill:       "#10161e"
    readonly property color glassFillStrong: "#151d28"
    readonly property color glassFillSoft:   "#0b1017"
    readonly property color windowGlow:      "#070b12"

    readonly property color textPrimary:    "#f4f7fb"
    readonly property color textSecondary:  "#cfd7e2"
    readonly property color textMuted:      "#91a0b4"
    readonly property color textDim:        "#627184"

    readonly property color accentPrimary:   "#a9c9ff"
    readonly property color accentSecondary: "#6f99d8"
    readonly property color accentSoft:      "#3f5d8a"
    readonly property color accentGlass:     "#d7e6ff"

    readonly property color success:        "#5cd7ae"
    readonly property color successStrong:  "#2f9e79"
    readonly property color warning:        "#efb25c"
    readonly property color danger:         "#ef706c"
    readonly property color dangerStrong:   "#b64545"

    readonly property color chartA: "#edf3fb"
    readonly property color chartB: "#a9c9ff"
    readonly property color chartC: "#78d7c7"
    readonly property color chartD: "#ffc181"

    readonly property color shadowColor: "#000000"
    readonly property color sheenColor:  "#ffffff"

    property var      sceneBackdropSource:      null
    property vector2d sceneBackdropSize:        Qt.vector2d(1, 1)
    property vector2d sceneBackdropTextureSize: Qt.vector2d(1, 1)

    function alpha(colorValue, amount) {
        return Qt.rgba(colorValue.r, colorValue.g, colorValue.b, amount)
    }
}
