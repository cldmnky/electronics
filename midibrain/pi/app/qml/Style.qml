pragma Singleton
// The mnkybox look: three dark surface tones, 1 px hairlines, one cyan accent
// and a strict role split (cyan = focus/active/primary, red = record/danger,
// green = healthy/running). Contrast is a measured property, not taste
// (WCAG 2.x relative luminance):
//   primary text on background >= 7:1
//   dim text and the accent on the near-black surfaces >= 4.5:1
//   selected vs idle background >= 3:1
//   danger text on every surface >= 4.5:1
// Every surface relative luminance <= 0.03. Every interactive control is at
// least Style.touch (44 px) in both directions.
import QtQuick

QtObject {
    // Three-tone depth: page, panel/card, raised control.
    readonly property color background: "#0e1116"
    readonly property color panel: "#161b22"
    readonly property color raised: "#1f2630"
    // Inset wells (list rows, meter troughs, readout fields).
    readonly property color panelDark: "#0b0e12"
    readonly property color outline: "#2a323d"
    readonly property color text: "#e6edf3"
    readonly property color textDim: "#9aa4b2"
    readonly property color accent: "#4fc3f7"
    // Danger/record red is light enough for 4.5:1 on the raised surface too.
    readonly property color record: "#f26b6b"
    readonly property color ok: "#66bb6a"
    // Selected EQ node. USER-MANUAL section 9: "four nodes with the selected one
    // pink" -- the only non-role colour in the theme.
    readonly property color nodePink: "#f06292"
    readonly property color gridLine: "#1b212a"
    // Danger fill for a destructive button that is not currently pressed.
    readonly property color dangerFill: "#3a1a1c"

    readonly property int touch: 44
    readonly property int tabHeight: 52
    readonly property int transportHeight: 44
    readonly property int masterWidth: 112

    // 8 px base grid; dense grid screens keep the same grid with a compact edge.
    readonly property int gap: 8
    readonly property int gapTight: 4
    readonly property int gapHair: 2
    readonly property int margin: 16
    readonly property int marginCompact: 8

    readonly property int outlineWidth: 1
    readonly property int corner: 6
    readonly property int cornerPanel: 10
    readonly property int track: 6
    readonly property int thumb: 22

    readonly property int fontSmall: 11
    readonly property int fontBody: 13
    readonly property int fontTitle: 16
    readonly property int fontDisplay: 22
    readonly property int fontTab: 14

    readonly property int motionPopup: 120
    readonly property int motionFace: 80
    readonly property int motionValue: 120
}
