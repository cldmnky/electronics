import QtQuick
import Mnkybox 1.0
import "../components"

// Destructive-action confirmation: dim scrim, panel, CANCEL and the requested
// confirm label. Both buttons keep the 44 px touch height.
Rectangle {
    id: root

    anchors.fill: parent
    color: "#9e000000"
    visible: app.confirmOpen
    z: 101

    MouseArea {
        anchors.fill: parent
        onClicked: app.confirmCancel()
    }

    Rectangle {
        width: Math.min(560, root.width - 60)
        height: Math.min(200, root.height - 80)
        anchors.centerIn: parent
        color: Style.panel
        radius: Style.corner
        border.width: 1
        border.color: Style.outline

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            Text {
                width: parent.width
                height: 26
                text: app.confirmTitle
                color: Style.text
                font.pixelSize: 17
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                height: parent.height - 26 - Style.touch - 16
                text: app.confirmMessage
                color: Style.textDim
                font.pixelSize: Style.fontBody
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                verticalAlignment: Text.AlignTop
            }
            Row {
                width: parent.width
                height: Style.touch
                spacing: 8
                layoutDirection: Qt.RightToLeft

                TouchButton {
                    width: 150
                    height: parent.height
                    text: app.confirmLabel
                    idleColor: "#7f2b2a"
                    onClicked: app.confirmAccept()
                }
                TouchButton {
                    width: 130
                    height: parent.height
                    text: "CANCEL"
                    onClicked: app.confirmCancel()
                }
            }
        }
    }
}
