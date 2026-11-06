import QtQuick
import Mnkybox 1.0

// Compact parameter knob for the built-in FX panels: the comp-popup card
// layout (framed cell, arc dial, parameter name, value) so five of them fit one
// row. Drag semantics and the value popup are the shared TouchKnob's, so the
// panel and the popup editors behave the same.
Item {
    id: root

    property string label: ""
    property real value: 0.0
    property real minimumValue: 0.0
    property real maximumValue: 1.0
    property string displayText: ""
    property string popupKind: "generic"
    property string popupLabel: ""
    property string focusId: ""
    signal moved(real value)

    implicitWidth: 72
    implicitHeight: 72

    Rectangle {
        anchors.fill: parent
        color: Style.panelDark
        radius: Style.corner
        border.width: (app.focusId === root.focusId && root.focusId !== "") ? 2
                      : Style.outlineWidth
        border.color: (app.focusId === root.focusId && root.focusId !== "")
                      ? Style.accent : Style.outline
    }

    TouchKnob {
        id: dial
        width: 42
        height: 42
        anchors.top: parent.top
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        showText: false
        dialSize: 42
        minimumValue: root.minimumValue
        maximumValue: root.maximumValue
        value: root.value
        focusId: ""
        popupLabel: root.popupLabel !== "" ? root.popupLabel : root.label
        popupKind: root.popupKind
        Binding on value { value: root.value; when: !dial.dragging }
        onMoved: (value) => root.moved(value)
    }

    Text {
        anchors.top: dial.bottom
        anchors.topMargin: 1
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: 13
        text: root.label
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    Text {
        anchors.top: dial.bottom
        anchors.topMargin: 13
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: 14
        text: root.displayText
        color: Style.text
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
