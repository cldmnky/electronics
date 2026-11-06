import QtQuick
import Mnkybox 1.0

// A value in a tight cell: a framed arc dial with its label and value. This is
// the refresh law's replacement for a slider outside a dedicated wide fader
// (mixer strip, TRACK/master fader): drag the chip, turn the panel encoder or
// step it with the buttons the screen puts beside it.
//
// `stepSize` picks the drag behaviour: 0 maps the drag over the range like a
// dial, a positive value nudges the value in fixed steps (a beat/measure field
// keeps its precision and needs no upper bound), and `dialScale` is the value
// the arc shows as full scale.
Rectangle {
    id: root

    property string label: ""
    property real value: 0.0
    property real minimumValue: 0.0
    // 0 = no upper clamp (the model owns the limit for a stepped field).
    property real maximumValue: 0.0
    // Full-scale value of the arc; 0 means "use maximumValue".
    property real dialScale: 0.0
    property real stepSize: 0.0
    property string valueText: ""
    property string suffix: ""
    property int decimals: 1
    property string popupLabel: ""
    property string popupKind: "generic"
    property string focusId: ""
    property bool active: true
    property bool dimmed: false

    signal moved(real value)

    readonly property real fullScale: dialScale > 0 ? dialScale
                                    : (maximumValue > minimumValue ? maximumValue : 1.0)
    readonly property real fraction: fullScale > 0
                                     ? Math.max(0, Math.min(1, value / fullScale)) : 0.0
    readonly property bool focused: app.focusId === focusId && focusId !== ""

    implicitHeight: Style.touch
    radius: Style.corner
    color: Style.panelDark
    border.width: root.focused ? 2 : Style.outlineWidth
    border.color: root.focused ? Style.accent : Style.outline
    opacity: active ? 1.0 : 0.55

    KnobDial {
        id: dial
        width: Math.max(24, Math.min(36, parent.height - 8))
        height: width
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        fraction: root.fraction
        dimmed: root.dimmed || !root.active
    }

    Column {
        anchors.left: dial.right
        anchors.leftMargin: 6
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Text {
            width: parent.width
            height: 13
            text: root.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            height: 17
            text: (root.valueText !== "" ? root.valueText
                                         : Number(root.value).toFixed(root.decimals))
                  + root.suffix
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.active
        preventStealing: true

        property real pressY: 0
        property real pressValue: 0

        onPressed: (mouse) => {
            pressY = mouse.y
            pressValue = root.value
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            let next
            if (root.stepSize > 0) {
                next = pressValue + Math.round((pressY - mouse.y) / 24) * root.stepSize
            } else {
                const span = root.maximumValue - root.minimumValue
                if (span <= 0) return
                next = pressValue + (pressY - mouse.y) / 180 * span
            }
            next = Math.max(root.minimumValue, next)
            if (root.maximumValue > root.minimumValue) {
                next = Math.min(root.maximumValue, next)
            }
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
            app.showValuePopup(root.popupLabel !== "" ? root.popupLabel : root.focusId,
                               root.popupKind, next)
            root.moved(next)
        }
        onReleased: app.touchAnchorY = -1
        onCanceled: app.touchAnchorY = -1
    }
}
