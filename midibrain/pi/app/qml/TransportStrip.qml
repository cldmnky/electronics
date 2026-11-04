import QtQuick
import Mnkybox 1.0
import "components"

// Persistent transport strip directly under the tab row: rolling/REC state,
// bar:beat plus elapsed time, BPM, the metronome toggle and the dirty dot.
Rectangle {
    id: root

    color: Style.panelDark
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Style.outline
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 8

        Text {
            width: 48
            height: parent.height
            text: app.transportState
            color: app.transportState === "REC" ? Style.record
                   : (app.transportState === "ROLL" ? Style.accent : Style.textDim)
            font.pixelSize: 12
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: 190
            height: parent.height
            text: app.positionText
            color: Style.text
            font.pixelSize: Style.fontBody
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: 90
            height: parent.height
            text: app.tempoText
            color: Style.textDim
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: Math.max(0, parent.width - 48 - 190 - 90 - 84 - 16 - 5 * 8)
            height: parent.height
            text: app.noticeText
            color: Style.accent
            font.pixelSize: 11
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        TouchButton {
            width: 76
            height: Style.touch
            anchors.verticalCenter: parent.verticalCenter
            text: "CLICK"
            textSize: Style.fontSmall
            checked: app.clickEnabled
            focusId: "transport.click"
            onClicked: app.setClickEnabled(!app.clickEnabled)
        }
        Item {
            width: 10
            height: parent.height
            Rectangle {
                visible: app.dirty
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: Style.accent
            }
        }
    }
}
