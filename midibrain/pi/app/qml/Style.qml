pragma Singleton
// The mnkybox look: flat, dark, one cyan accent, thin outlines and small corner
// radii. Contrast is a measured property, not taste (WCAG 2.x relative
// luminance):
//   primary text on background >= 7:1
//   dim text and the accent on the near-black surfaces >= 4.5:1
//   selected vs idle background >= 3:1
//   every surface relative luminance <= 0.03
// Every interactive control is at least Style.touch (44 px) in both directions.
import QtQuick

QtObject {
    readonly property color background: "#15171b"
    readonly property color panel: "#1e2127"
    readonly property color panelDark: "#101216"
    readonly property color outline: "#2f343d"
    readonly property color text: "#e6e8ec"
    readonly property color textDim: "#9aa1ad"
    readonly property color accent: "#4fc3f7"
    readonly property color record: "#ef5350"
    readonly property color soloOn: "#ffc107"
    readonly property color muteOn: "#ff7043"
    readonly property color ok: "#66bb6a"
    // Selected EQ node. USER-MANUAL section 9: "four nodes with the selected one
    // pink" -- the accent stays reserved for the focus ring and the tab.
    readonly property color nodePink: "#f06292"
    readonly property color gridLine: "#262b33"

    readonly property int touch: 44
    readonly property int tabHeight: 52
    readonly property int transportHeight: 44
    readonly property int masterWidth: 112
    readonly property int margin: 6
    readonly property int corner: 3
    readonly property int outlineWidth: 1
    readonly property int track: 5
    readonly property int thumb: 22

    readonly property int fontSmall: 11
    readonly property int fontBody: 13
    readonly property int fontTitle: 15
}
