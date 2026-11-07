pragma Singleton
// The mnkybox look: three dark surface tones, 1 px hairlines, one cyan accent
// and a strict role split (cyan = focus/active/primary, red = record/danger,
// green = healthy/running). Contrast is a measured property, not taste
// (WCAG 2.x relative luminance):
//   primary text on background >= 7:1
//   dim text and the accent on the near-black surfaces >= 4.5:1
//   selected vs idle background >= 3:1
//   danger text on every surface >= 4.5:1
// Every surface relative luminance <= 0.03. Frequent controls use
// touchPrimary (64 px), secondary ones touchSecondary (56 px); Style.touch
// (44 px) is the absolute floor kept for the screens staged for migration.
// The sizing policy and its physical-mm rationale live in
// docs/ui-refresh-spec.md section 7.
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
    // The four-role switch legend (C1): SOLO fills with `ok` green and a dark
    // label, MUTE fills with `textDim` and keeps `text` readable on it, ARM
    // fills with `record` red, and the panel focus ring stays `accent`. The
    // EQ node is the one non-role colour (pink).
    readonly property color gridLine: "#1b212a"
    // Danger fill for a destructive button that is not currently pressed.
    readonly property color dangerFill: "#3a1a1c"

    // Sizing policy (docs/ui-refresh-spec.md section 7): the 800x480 panel is
    // ~75.8 x 45.36 mm, so 44 px is only ~4.2 mm. Frequent touch targets are
    // 64 px (~6.1 mm), secondary ones 56 px (~5.3 mm); 44 px stays only as the
    // legacy floor on screens the later stages have not migrated yet.
    readonly property int touch: 44
    readonly property int touchPrimary: 64
    readonly property int touchSecondary: 56
    // Navigation and transport migrated in stage 1; every other screen keeps
    // its own fixed arithmetic until its stage moves it.
    readonly property int tabHeight: 56
    readonly property int transportHeight: 64
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
    // Stage-1 sizing policy: primary labels/values 16-18, secondary info 14,
    // important transport/recording values 22-28. Never solve overflow by
    // shrinking these - remove, group or relocate a control first.
    readonly property int fontSecondary: 14
    readonly property int fontLabel: 17
    readonly property int fontValue: 24
    // Nav labels are primary labels now (four destinations, not nine).
    readonly property int fontTab: fontLabel

    readonly property int motionPopup: 120
    readonly property int motionFace: 80
    readonly property int motionValue: 120
}
