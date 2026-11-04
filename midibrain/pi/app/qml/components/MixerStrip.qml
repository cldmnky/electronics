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
    property real level: 0.0
    property bool clip: false
    property int monitor: 1
    property string monitorMark: ""
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
        spacing: 0
        clip: true

        Item {
            width: parent.width
            height: 12
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

        Row {
            width: parent.width
            height: Math.max(Style.touch, root.height - 12 - 4 * Style.touch)
            spacing: 2

            TouchSlider {
                id: gain
                width: parent.width - 16
                height: parent.height
                vertical: true
                minimumValue: -60
                maximumValue: 12
                value: root.gainDb
                focusId: "mixer.gain." + root.stripIndex
                popupLabel: "IN " + String(root.stripIndex + 1).padStart(2, "0") + " GAIN"
                popupKind: "db"
                Binding on value { value: root.gainDb; when: !gain.dragging }
                onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
            }

            // Per-strip meter with a clip-hold LED. The level is the post-gain
            // engine tap mapped to dB; the LED latches a full-scale block.
            Item {
                id: meter
                width: 14
                height: parent.height

                Rectangle {
                    anchors.fill: parent
                    color: Style.panelDark
                    border.width: Style.outlineWidth
                    border.color: Style.outline
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 1
                    height: Math.max(0, (parent.height - 2) *
                                     Math.max(0, Math.min(1, root.level)))
                    color: root.level > 0.95 ? Style.record : Style.ok
                }
                Rectangle {
                    id: clipLed
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 1
                    width: 8
                    height: 6
                    radius: 1
                    color: root.clip ? Style.record : Style.outline
                    border.width: Style.outlineWidth
                    border.color: Style.panelDark
                }
                // Per-track input monitoring: OFF/ON always, Auto only when an
                // active take makes Auto choose the take over the live input.
                Text {
                    anchors.top: clipLed.bottom
                    anchors.topMargin: 1
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.monitorMark
                    color: root.monitor === 0 ? Style.record
                           : (root.monitor === 2 ? Style.ok : Style.soloOn)
                    font.pixelSize: 7
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        TouchSlider {
            id: panSlider
            width: parent.width
            height: Style.touch
            minimumValue: 0
            maximumValue: 1
            value: root.pan
            focusId: "mixer.pan." + root.stripIndex
            popupLabel: "IN " + String(root.stripIndex + 1).padStart(2, "0") + " PAN"
            popupKind: "pan"
            Binding on value { value: root.pan; when: !panSlider.dragging }
            onMoved: (value) => mixer.setPan(root.stripIndex, value)
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
