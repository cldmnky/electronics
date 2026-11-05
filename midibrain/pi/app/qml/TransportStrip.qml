import QtQuick
import Mnkybox 1.0
import "components"

// Persistent transport strip directly under the tab row: rolling/REC state,
// bar:beat plus elapsed time, BPM, the metronome toggle and the dirty dot.
// No-Pico mode adds the touch transport (PLAY/STOP/REC) because the panel buttons
// live on the absent Pico. In hardware mode the physical panel owns them.
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
        id: stripRow

        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 8

        // Every width is fixed except the notice, so the row computes the
        // leftover instead of overlapping the touch transport in no-Pico mode.
        readonly property int baseWidth: 48 + (app.noPicoMode ? 150 : 190) +
                                         (app.noPicoMode ? 76 : 90) + 76 + 10 +
                                         (app.noPicoMode ? 0 : 14)
        readonly property int noPicoWidth: app.noPicoMode ? 60 + 3 * 50 : 0
        readonly property int itemCount: app.noPicoMode ? 10 : 6

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
            width: app.noPicoMode ? 150 : 190
            height: parent.height
            text: app.positionText
            color: Style.text
            font.pixelSize: Style.fontBody
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: app.noPicoMode ? 76 : 90
            height: parent.height
            text: app.tempoText
            color: Style.textDim
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            // Persistent bare-Pi marker: the mixer/transport shots must show
            // that this run has no Pico even when the System screen is closed.
            width: app.noPicoMode ? 60 : 0
            height: parent.height
            visible: app.noPicoMode
            text: "NO PICO"
            color: Style.record
            font.pixelSize: 11
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        TouchButton {
            // Touch transport, NO PICO mode only: the hardware PLAY/STOP/REC buttons
            // and the REC chord are panel inputs on the Pico.
            width: app.noPicoMode ? 50 : 0
            height: Style.touch
            visible: app.noPicoMode
            text: "PLAY"
            textSize: 9
            focusId: "transport.play"
            onClicked: app.play()
        }
        TouchButton {
            width: app.noPicoMode ? 50 : 0
            height: Style.touch
            visible: app.noPicoMode
            text: "STOP"
            textSize: 9
            focusId: "transport.stop"
            onClicked: app.stop()
        }
        TouchButton {
            width: app.noPicoMode ? 50 : 0
            height: Style.touch
            visible: app.noPicoMode
            text: "REC"
            textSize: 9
            checked: app.recording
            checkedColor: Style.record
            focusId: "transport.rec"
            onClicked: app.recToggle()
        }
        Text {
            width: Math.max(0, stripRow.width - stripRow.baseWidth -
                               stripRow.noPicoWidth -
                               stripRow.spacing * (stripRow.itemCount - 1))
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
