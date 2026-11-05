import QtQuick
import Mnkybox 1.0

// Touch-first flat button. `checked` is bound by the screen to model state; a
// tap emits clicked() and the screen performs the state change, so the visual
// never lies about a failed write. The focus ring is the panel NAV focus, not a
// hover/keyboard highlight.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    property bool active: true
    property color checkedColor: Style.accent
    property color idleColor: Style.panel
    property color checkedTextColor: Style.panelDark
    property string focusId: ""
    property int textSize: Style.fontBody
    property bool bold: true

    signal clicked()

    implicitWidth: Math.max(Style.touch, label.implicitWidth + 16)
    implicitHeight: Style.touch
    radius: Style.corner
    color: !active ? Style.panelDark
                    : (tap.pressed ? checkedColor
                                   : (checked ? checkedColor : idleColor))
    border.width: (app.focusId === focusId && focusId !== "") ? 2 : Style.outlineWidth
    border.color: (app.focusId === focusId && focusId !== "") ? Style.accent : Style.outline
    opacity: tap.pressed ? 0.8 : (active ? 1.0 : 0.45)

    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 8
        text: root.text
        color: root.checked ? root.checkedTextColor : Style.text
        font.pixelSize: root.textSize
        font.bold: root.bold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    TapHandler {
        id: tap
        enabled: root.active
        onTapped: root.clicked()
    }
}
