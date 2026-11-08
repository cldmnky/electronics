import QtQuick
import Mnkybox 1.0

// SHIFT-face mixer strip: the configuration surface beside the performance
// strip. Gain, the FX1/FX2 rack slot wet, CUE, OUT34 and the track mode live
// here as six 2-column cells, so three strips still fit the console below the
// nav/transport chrome. The primary strip stays perform-only (fader, meters,
// M/S/ARM); the mode selector is this configuration view, never a permanent
// 9 px selector on the strip. FX1/FX2 edit the slot's wet; the effect itself is
// configured in the TRACK FX popup.
Rectangle {
    id: root

    property int stripIndex: 0
    property string stripName: ""
    property real fx1Wet: 1.0
    property real fx2Wet: 1.0
    property string fx1Name: "NONE"
    property string fx2Name: "NONE"
    property int mode: 1
    property string modeWord: "ARM"
    readonly property string nameText: stripName !== "" ? stripName
                                                        : "IN " + String(stripIndex + 1).padStart(2, "0")

    color: Style.panel
    radius: Style.corner
    border.width: Style.outlineWidth
    border.color: Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 3
        spacing: Style.gapTight

        Text {
            width: parent.width
            height: 20
            text: root.nameText
            color: Style.text
            font.pixelSize: Style.fontSecondary
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Grid {
            width: parent.width
            height: parent.height - 20 - Style.gapTight
            columns: 2
            spacing: Style.gapTight

            KnobChip {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                label: "GAIN"
                value: mixer.gainDbAt(root.stripIndex)
                valueText: Number(mixer.gainDbAt(root.stripIndex)).toFixed(1)
                suffix: " dB"
                minimumValue: -60
                maximumValue: 12
                popupLabel: root.nameText + " GAIN"
                popupKind: "db"
                focusId: "shift.mixer.gain." + root.stripIndex
                onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
            }
            KnobChip {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                label: "FX1 " + root.fx1Name
                value: root.fx1Wet
                valueText: Math.round(root.fx1Wet * 100)
                suffix: "%"
                popupLabel: root.nameText + " FX1 WET"
                popupKind: "percent"
                focusId: "shift.mixer.fx1." + root.stripIndex
                onMoved: (value) => mixer.setFxWetAt(root.stripIndex, 0, value)
            }
            KnobChip {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                label: "FX2 " + root.fx2Name
                value: root.fx2Wet
                valueText: Math.round(root.fx2Wet * 100)
                suffix: "%"
                popupLabel: root.nameText + " FX2 WET"
                popupKind: "percent"
                focusId: "shift.mixer.fx2." + root.stripIndex
                onMoved: (value) => mixer.setFxWetAt(root.stripIndex, 1, value)
            }
            KnobChip {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                label: "CUE"
                value: mixer.cueAt(root.stripIndex)
                valueText: Math.round(mixer.cueAt(root.stripIndex) * 100)
                suffix: "%"
                popupLabel: root.nameText + " CUE"
                popupKind: "percent"
                focusId: "shift.mixer.cue." + root.stripIndex
                onMoved: (value) => mixer.setCue(root.stripIndex, value)
            }
            KnobChip {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                label: "OUT34"
                value: mixer.out34At(root.stripIndex)
                valueText: Math.round(mixer.out34At(root.stripIndex) * 100)
                suffix: "%"
                popupLabel: root.nameText + " OUT34"
                popupKind: "percent"
                focusId: "shift.mixer.out34." + root.stripIndex
                onMoved: (value) => mixer.setOut34(root.stripIndex, value)
            }
            // the same mode word the strip pill shows; one tap cycles and
            // the full four-way choice is in the channel popout.
            TouchButton {
                width: (parent.width - Style.gapTight) / 2
                height: (parent.height - Style.gapTight * 2) / 3
                text: root.modeWord
                textSize: Style.fontSecondary
                checked: root.mode !== 0
                checkedColor: root.mode === 1 ? Style.record : Style.accent
                focusId: "shift.mixer.monitor." + root.stripIndex
                onClicked: mixer.setMode(root.stripIndex, (root.mode + 1) % 4)
            }
        }
    }
}
