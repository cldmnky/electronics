import QtQuick
import Mnkybox 1.0

// SHIFT-face mixer strip: configure Gain, FX1, FX2, CUE, OUT34 and the
// per-track monitor mode. The primary strip stays perform-only (Vol/Pan/M/S/ARM).
// Every value here is a narrow per-strip cell, so it is an arc-dial chip
// (KnobChip), never a slider: the refresh law keeps sliders for the dedicated
// wide faders only.
Rectangle {
    id: root

    property int stripIndex: 0
    property string stripName: ""
    readonly property string nameText: stripName !== "" ? stripName
                                                        : "IN " + String(stripIndex + 1).padStart(2, "0")
    readonly property int monitor: mixer.monitorAt(stripIndex)

    color: Style.panel
    radius: Style.corner
    border.width: Style.outlineWidth
    border.color: Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 0

        Text {
            width: parent.width
            height: 12
            text: root.nameText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        KnobChip {
            width: parent.width
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
            width: parent.width
            label: "FX1"
            value: mixer.sendAt(root.stripIndex, 0)
            valueText: Math.round(mixer.sendAt(root.stripIndex, 0) * 100)
            suffix: "%"
            popupLabel: root.nameText + " FX1"
            popupKind: "percent"
            focusId: "shift.mixer.fx1." + root.stripIndex
            onMoved: (value) => mixer.setSend(root.stripIndex, 0, value)
        }
        KnobChip {
            width: parent.width
            label: "FX2"
            value: mixer.sendAt(root.stripIndex, 1)
            valueText: Math.round(mixer.sendAt(root.stripIndex, 1) * 100)
            suffix: "%"
            popupLabel: root.nameText + " FX2"
            popupKind: "percent"
            focusId: "shift.mixer.fx2." + root.stripIndex
            onMoved: (value) => mixer.setSend(root.stripIndex, 1, value)
        }
        KnobChip {
            width: parent.width
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
            width: parent.width
            label: "OUT"
            value: mixer.out34At(root.stripIndex)
            valueText: Math.round(mixer.out34At(root.stripIndex) * 100)
            suffix: "%"
            popupLabel: root.nameText + " OUT34"
            popupKind: "percent"
            focusId: "shift.mixer.out34." + root.stripIndex
            onMoved: (value) => mixer.setOut34(root.stripIndex, value)
        }
        TouchButton {
            width: parent.width
            height: Style.touch
            text: root.monitor === 0 ? "M PLAY"
                  : (root.monitor === 1 ? "M AUTO"
                     : (root.monitor === 2 ? "M BOTH" : "M MON"))
            textSize: Style.fontSmall
            checked: root.monitor !== 1
            focusId: "shift.mixer.monitor." + root.stripIndex
            onClicked: mixer.setMonitor(root.stripIndex, (root.monitor + 1) % 4)
        }
    }
}
