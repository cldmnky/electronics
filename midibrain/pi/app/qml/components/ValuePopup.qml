import QtQuick
import Mnkybox 1.0

// Transient value readout shown while a control changes (touch drag or panel
// EDIT-mode joystick). One instance lives in Main.qml and every slider/value
// path feeds the app popup state, so the component never steals focus and does
// not add hit-testable pixels: it has no MouseArea and is disabled.
//
// Placement is the fixed top band, like the Bluebox transient readout: the tab
// row is the screen area the finger never works in, so a readout there can
// never sit under the control being changed. The one case that band would cover
// the finger is a control at the very top of the panel (the fullscreen EQ grid
// reaches y = 0); then the readout moves below the touch point instead. `anchorY`
// is that touch point in window coordinates, -1 when the panel EDIT path owns
// the readout and there is no finger.
Rectangle {
    id: root

    property bool popupVisible: false
    property string popupLabel: ""
    property string popupText: ""
    property real anchorY: -1

    readonly property real topMargin: Style.margin
    // A finger covers roughly this much above and below the touch point, so the
    // band counts as covered while the point is within a finger radius of it.
    readonly property real fingerRadius: Style.touch / 2
    readonly property real fingerGap: Style.touch
    readonly property real bandBottom: topMargin + height

    // Below the finger when the finger is on or above the band edge, clamped to
    // the window; the top band otherwise.
    readonly property real placementY: (anchorY < 0 || anchorY - fingerRadius > bandBottom)
                                       ? topMargin
                                       : Math.min(parent.height - height - topMargin,
                                                  anchorY + fingerGap)

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
    x: Math.round((parent.width - width) / 2)
    y: placementY

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
