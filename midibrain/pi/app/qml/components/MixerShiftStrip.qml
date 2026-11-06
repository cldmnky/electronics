import QtQuick
import Mnkybox 1.0

// SHIFT-face mixer strip: configure Gain, FX1, FX2, CUE, OUT34 and the
// per-track monitor mode. The primary strip stays perform-only (Vol/Pan/M/S/ARM).
Rectangle {
    id: root

    property int stripIndex: 0
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
            text: "IN " + String(root.stripIndex + 1).padStart(2, "0")
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        ShiftRow {
            width: parent.width
            label: "GAIN"
            value: mixer.gainDbAt(root.stripIndex)
            minimumValue: -60
            maximumValue: 12
            popupKind: "db"
            focusId: "shift.mixer.gain." + root.stripIndex
            onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
        }
        ShiftRow {
            width: parent.width
            label: "FX1"
            value: mixer.sendAt(root.stripIndex, 0)
            popupKind: "percent"
            focusId: "shift.mixer.fx1." + root.stripIndex
            onMoved: (value) => mixer.setSend(root.stripIndex, 0, value)
        }
        ShiftRow {
            width: parent.width
            label: "FX2"
            value: mixer.sendAt(root.stripIndex, 1)
            popupKind: "percent"
            focusId: "shift.mixer.fx2." + root.stripIndex
            onMoved: (value) => mixer.setSend(root.stripIndex, 1, value)
        }
        ShiftRow {
            width: parent.width
            label: "CUE"
            value: mixer.cueAt(root.stripIndex)
            popupKind: "percent"
            focusId: "shift.mixer.cue." + root.stripIndex
            onMoved: (value) => mixer.setCue(root.stripIndex, value)
        }
        ShiftRow {
            width: parent.width
            label: "OUT"
            value: mixer.out34At(root.stripIndex)
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
            textSize: 9
            checked: root.monitor !== 1
            checkedColor: root.monitor === 0 ? Style.record
                          : (root.monitor === 2 ? Style.ok : Style.accent)
            focusId: "shift.mixer.monitor." + root.stripIndex
            onClicked: mixer.setMonitor(root.stripIndex, (root.monitor + 1) % 4)
        }
    }

    component ShiftRow: Row {
        property string label: ""
        property real value: 0
        property real minimumValue: 0
        property real maximumValue: 1
        property string popupKind: "percent"
        property string focusId: ""
        signal moved(real value)

        height: Style.touch
        spacing: 0

        Text {
            width: 24
            height: parent.height
            text: parent.label
            color: Style.textDim
            font.pixelSize: 8
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        TouchSlider {
            id: shiftSlider
            width: parent.width - 24
            height: parent.height
            minimumValue: parent.minimumValue
            maximumValue: parent.maximumValue
            value: parent.value
            focusId: parent.focusId
            popupLabel: "IN " + String(root.stripIndex + 1).padStart(2, "0") + " " + parent.label
            popupKind: parent.popupKind
            Binding on value { value: shiftSlider.parent.value; when: !shiftSlider.dragging }
            onMoved: (value) => parent.moved(value)
        }
    }
}
