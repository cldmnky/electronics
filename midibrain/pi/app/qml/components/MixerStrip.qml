import QtQuick
import Mnkybox 1.0

// One banked mixer strip: gain fader, pan, two sends and MUTE/SOLO/ARM. The
// outer border is the selected-strip indicator; every control carries a panel
// focus id.
Rectangle {
    id: root

    property int stripIndex: 0
    property real gainDb: 0.0
    property real pan: 0.5
    property string panText: "C"
    property real sendA: 0.0
    property real sendB: 0.0
    property bool mute: false
    property bool solo: false
    property bool arm: false
    property bool selected: false
    readonly property bool focused: app.focusId === ("mixer.select." + stripIndex)

    color: Style.panel
    radius: Style.corner
    border.width: (selected || focused) ? 2 : Style.outlineWidth
    border.color: (selected || focused) ? Style.accent : Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        clip: true

        Item {
            width: parent.width
            height: 16
            Text {
                anchors.left: parent.left
                text: "IN " + String(root.stripIndex + 1).padStart(2, "0")
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
            }
            Text {
                anchors.right: parent.right
                text: root.panText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
            }
        }

        TouchSlider {
            id: gain
            width: parent.width
            height: Math.max(Style.touch, root.height - 16 - 6 * Style.touch - 22)
            vertical: true
            minimumValue: -60
            maximumValue: 12
            value: root.gainDb
            focusId: "mixer.gain." + root.stripIndex
            Binding on value { value: root.gainDb; when: !gain.dragging }
            onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
        }

        TouchSlider {
            id: panSlider
            width: parent.width
            height: Style.touch
            minimumValue: 0
            maximumValue: 1
            value: root.pan
            focusId: "mixer.pan." + root.stripIndex
            Binding on value { value: root.pan; when: !panSlider.dragging }
            onMoved: (value) => mixer.setPan(root.stripIndex, value)
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 2
            Text {
                width: 12
                height: parent.height
                text: "A"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }
            TouchSlider {
                id: sendA
                width: parent.width - 14
                height: Style.touch
                minimumValue: 0
                maximumValue: 1
                value: root.sendA
                focusId: "mixer.sendA." + root.stripIndex
                Binding on value { value: root.sendA; when: !sendA.dragging }
                onMoved: (value) => mixer.setSend(root.stripIndex, 0, value)
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 2
            Text {
                width: 12
                height: parent.height
                text: "B"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }
            TouchSlider {
                id: sendB
                width: parent.width - 14
                height: Style.touch
                minimumValue: 0
                maximumValue: 1
                value: root.sendB
                focusId: "mixer.sendB." + root.stripIndex
                Binding on value { value: root.sendB; when: !sendB.dragging }
                onMoved: (value) => mixer.setSend(root.stripIndex, 1, value)
            }
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "M"
            checked: root.mute
            checkedColor: Style.muteOn
            focusId: "mixer.mute." + root.stripIndex
            onClicked: mixer.toggleMute(root.stripIndex)
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "S"
            checked: root.solo
            checkedColor: Style.soloOn
            focusId: "mixer.solo." + root.stripIndex
            onClicked: mixer.toggleSolo(root.stripIndex)
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "ARM"
            textSize: Style.fontSmall
            checked: root.arm
            checkedColor: Style.record
            focusId: "mixer.arm." + root.stripIndex
            onClicked: mixer.toggleArm(root.stripIndex)
        }
    }

    TapHandler {
        onTapped: app.selectStrip(root.stripIndex)
    }
}
