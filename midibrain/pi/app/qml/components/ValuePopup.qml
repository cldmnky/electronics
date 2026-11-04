import QtQuick
import Mnkybox 1.0

// Transient value readout shown while a control changes (touch drag or panel
// EDIT-mode joystick). One instance lives in Main.qml and every slider/value
// path feeds the app popup state, so the component never steals focus and does
// not add hit-testable pixels: it has no MouseArea and is disabled.
Rectangle {
    id: root

    property bool popupVisible: false
    property string popupLabel: ""
    property string popupText: ""

    visible: opacity > 0.0
    enabled: false
    z: 120
    width: Math.max(210, Math.min(440, valueText.implicitWidth + 56))
    height: 58
    radius: Style.corner
    color: Style.panel
    border.width: 2
    border.color: Style.accent
    opacity: popupVisible ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: 120 }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 1

        Text {
            width: parent.width
            height: 16
            text: root.popupLabel
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            id: valueText
            width: parent.width
            height: 30
            text: root.popupText
            color: Style.text
            font.pixelSize: 22
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
