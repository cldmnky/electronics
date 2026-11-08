import QtQuick
import Mnkybox 1.0

// Interactive parameter knob: KnobDial plus the value popup and the drag path
// every other value control uses. Dragging is relative (vertical drag, 180 px
// full range) so a circular knob does not jump when the finger lands on it;
// the model value still arrives through moved() and the panel focus ring stays
// on the knob itself.
Item {
    id: root

    property real value: 0.0
    property real minimumValue: 0.0
    property real maximumValue: 1.0
    property string label: ""
    property string valueText: ""
    property string suffix: ""
    property string focusId: ""
    property string popupLabel: ""
    property string popupKind: "generic"
    property bool active: true
    property bool dragging: false
    property bool showText: true
    property int dialSize: 44

    signal moved(real value)

    // a bare knob (no label column) is still a 44 px touch target even if
    // a caller asks for a smaller painted dial; the MouseArea fills this item.
    readonly property int hitSize: Math.max(Style.touch, dialSize)
    implicitWidth: showText ? 126 : hitSize
    implicitHeight: showText ? 58 : hitSize
    opacity: active ? 1.0 : 0.45

    readonly property real fraction: maximumValue > minimumValue
                                     ? Math.max(0, Math.min(1,
                                           (value - minimumValue) / (maximumValue - minimumValue)))
                                     : 0.0
    readonly property real startValue: value

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: Style.corner
        border.width: (app.focusId === root.focusId && root.focusId !== "") ? 2 : 0
        border.color: Style.accent
        visible: root.showText
    }

    KnobDial {
        id: dial
        width: root.dialSize
        height: root.dialSize
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: root.showText ? 3 : 0
        fraction: root.fraction
        dimmed: !root.active
    }

    Column {
        visible: root.showText
        anchors.left: dial.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: root.width - dial.width - 14
        spacing: 0

        Text {
            width: parent.width
            height: 16
            text: root.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            height: 22
            text: root.valueText !== "" ? root.valueText + root.suffix
                                        : Number(root.value).toFixed(1) + root.suffix
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: root.active
        preventStealing: true
        property real pressY: 0
        property real pressValue: 0
        onPressed: (mouse) => {
            root.dragging = true
            pressY = mouse.y
            pressValue = root.value
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const span = root.maximumValue - root.minimumValue
            if (span <= 0) return
            const next = Math.max(root.minimumValue,
                                  Math.min(root.maximumValue,
                                           pressValue + (pressY - mouse.y) / 180 * span))
            root.value = next
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
            app.showValuePopup(root.popupLabel !== "" ? root.popupLabel : root.focusId,
                               root.popupKind, next)
            root.moved(next)
        }
        onReleased: {
            root.dragging = false
            app.touchAnchorY = -1
        }
        onCanceled: {
            root.dragging = false
            app.touchAnchorY = -1
        }
    }
}
