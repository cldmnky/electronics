import QtQuick
import Mnkybox 1.0
import "../components"

// ARRANGE clip "..." sheet (docs/daw-plan.md W6). One clip at a time: OPEN EDIT
// hands the take to the W3 edit popup, MOVE/TRIM arm the touch drag and the
// panel value knob (pos/length stay editable through the sheet AND the
// encoder, never drag-only), DUPLICATE/MUTE/DELETE are instant, and GAIN/FADE
// opens the three KnobChip cards (never a slider). The panel ring is trapped
// while this sheet is open (AppController::rebuildFocusIds). Sizes are inline:
// 52 px secondary actions, 14-18 px labels.
Rectangle {
    id: root

    anchors.fill: parent
    visible: arrange.clipMenuOpen
    color: "#9e000000"
    z: 92

    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        id: panel
        width: Math.min(744, root.width - 16)
        height: content.implicitHeight + 24
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent

        Column {
            id: content
            x: 12
            y: 12
            width: panel.width - 24
            spacing: Style.gap

            Row {
                width: parent.width
                height: 26
                Text {
                    width: parent.width - 200
                    height: parent.height
                    text: arrange.clipMenuTitle
                    color: Style.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: 200
                    height: parent.height
                    text: arrange.clipMenuPane === 0
                          ? "TAP AN ACTION"
                          : "GAIN / FADES - LIVE"
                    color: Style.textDim
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            // ---- actions ----------------------------------------------------
            Column {
                visible: arrange.clipMenuPane === 0
                width: parent.width
                spacing: Style.gapTight
                Text {
                    width: parent.width
                    height: 20
                    text: arrange.clipPositionText + "   " + arrange.clipLengthText
                    color: Style.textDim
                    font.pixelSize: 14
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Grid {
                    width: parent.width
                    columns: 2
                    spacing: Style.gap
                    Repeater {
                        model: arrange.clipActions
                        delegate: TouchButton {
                            width: (parent.width - Style.gap) / 2
                            height: 52
                            text: (index === 5 && arrange.clipMuted)
                                  ? "UNMUTE"
                                  : modelData
                            textSize: 16
                            variant: index === 6 ? "danger" : "secondary"
                            focusId: "arrange.clip.action." + index
                            onClicked: arrange.clipMenuAction(index)
                        }
                    }
                }
            }

            // ---- gain / fades ----------------------------------------------
            Column {
                visible: arrange.clipMenuPane === 1
                width: parent.width
                spacing: Style.gapTight
                Row {
                    width: parent.width
                    height: 56
                    spacing: Style.gapTight
                    KnobChip {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "GAIN"
                        value: arrange.clipGainDb
                        minimumValue: -96
                        maximumValue: 12
                        stepSize: 0.5
                        dialScale: 24
                        suffix: " dB"
                        decimals: 1
                        popupLabel: "CLIP GAIN"
                        popupKind: "db"
                        focusId: "arrange.clip.gain"
                        onMoved: (value) => arrange.setClipGainDb(value)
                    }
                    KnobChip {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "FADE IN"
                        value: arrange.clipFadeInBeats
                        minimumValue: 0
                        maximumValue: 16
                        stepSize: 0.25
                        dialScale: 8
                        suffix: " b"
                        decimals: 2
                        popupLabel: "FADE IN"
                        popupKind: "generic"
                        focusId: "arrange.clip.fadein"
                        onMoved: (value) => arrange.setClipFadeIn(value)
                    }
                    KnobChip {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "FADE OUT"
                        value: arrange.clipFadeOutBeats
                        minimumValue: 0
                        maximumValue: 16
                        stepSize: 0.25
                        dialScale: 8
                        suffix: " b"
                        decimals: 2
                        popupLabel: "FADE OUT"
                        popupKind: "generic"
                        focusId: "arrange.clip.fadeout"
                        onMoved: (value) => arrange.setClipFadeOut(value)
                    }
                }
                Text {
                    width: parent.width
                    text: "Gain and fades are non-destructive clip values; the take file is never rewritten. 0 dB / 0 beats are the defaults."
                    color: Style.textDim
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }

            Row {
                width: parent.width
                height: 56
                spacing: Style.gapTight
                TouchButton {
                    width: 96
                    height: parent.height
                    text: arrange.clipMenuPane === 0 ? "" : "BACK"
                    visible: arrange.clipMenuPane !== 0
                    textSize: 16
                    variant: "ghost"
                    focusId: "arrange.clip.pane.back"
                    onClicked: arrange.showClipMenuActions()
                }
                Item { width: parent.width - 96 - 104 - Style.gapTight * 2; height: 1 }
                TouchButton {
                    width: 104
                    height: parent.height
                    text: "CLOSE"
                    textSize: 16
                    variant: "ghost"
                    focusId: "arrange.clip.close"
                    onClicked: arrange.closeClipMenu()
                }
            }
        }
    }
}
