import QtQuick
import Mnkybox 1.0

// One pipe parameter as a card in the two-column parameter grid, built from
// the generated schema metadata (pipe_ui.cpp):
//   "knob" / "stepper"        arc dial, label and value (the comp popup card)
//   "toggle" / "choice"       label and value with a chevron; a tap opens the
//                             choice list for that positional parameter
// The dial keeps its own value while the finger is down and commits on
// release: every model write reassigns the parameter list, which rebuilds the
// delegate, so a live write would end the drag after the first pixel.
Item {
    id: root

    property var param
    property int pipeIndex: 0

    readonly property int paramIndex: param !== undefined && param.param !== undefined
                                      ? Number(param.param) : -1
    readonly property bool numeric: param !== undefined &&
                                    (param.editor === "knob" || param.editor === "stepper")
    readonly property string focusId: paramIndex < 0 || pipeIndex < 0 ? ""
                                      : "shift.routing.pipes.param." + pipeIndex + "." + paramIndex
    readonly property bool focused: app.focusId === focusId && focusId !== ""

    function setValue(value) {
        const minimum = Number(param.minimum)
        const maximum = Number(param.maximum)
        routing.setPipeParam(root.pipeIndex, Number(param.param),
                             Math.max(minimum, Math.min(maximum, Math.round(value))))
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.corner
        color: Style.panel
        border.width: root.focused ? 2 : Style.outlineWidth
        border.color: root.focused ? Style.accent : Style.outline
    }

    // ---- numeric: arc dial + label + value -------------------------------
    KnobDial {
        id: dial
        visible: root.numeric
        width: Math.min(36, parent.height - 26)
        height: width
        anchors.top: parent.top
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        fraction: {
            if (!root.numeric) return 0
            const minimum = Number(root.param.minimum)
            const span = Number(root.param.maximum) - minimum
            return span > 0 ? (root.dialValue - minimum) / span : 0
        }
    }

    Text {
        id: dialLabel
        visible: root.numeric
        anchors.top: dial.bottom
        anchors.topMargin: 1
        width: parent.width - 8
        height: 12
        text: root.param !== undefined && root.param.name !== undefined ? root.param.name : ""
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Text {
        visible: root.numeric
        anchors.top: dialLabel.bottom
        width: parent.width - 8
        height: 13
        text: root.param !== undefined && root.param.valueText !== undefined
              ? root.param.valueText : ""
        color: Style.text
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    MouseArea {
        id: dialDrag
        anchors.fill: parent
        visible: root.numeric
        preventStealing: true

        property real pressY: 0
        property real pressValue: 0

        onPressed: (mouse) => {
            pressY = mouse.y
            pressValue = root.dialValue
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const minimum = Number(root.param.minimum)
            const span = Number(root.param.maximum) - minimum
            if (span <= 0) return
            const next = Math.max(minimum,
                                  Math.min(Number(root.param.maximum),
                                           pressValue + (pressY - mouse.y) / 180 * span))
            root.dialValue = Math.round(next)
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
            app.showTextPopup(root.param.name,
                              routing.pipeParamValueText(root.paramIndex, root.dialValue))
        }
        onReleased: {
            app.touchAnchorY = -1
            if (root.dialValue !== Number(root.param.value)) root.setValue(root.dialValue)
        }
        onCanceled: app.touchAnchorY = -1
    }

    // Dial value: the model's while the finger is up, the drag's while it is
    // down. A Binding keeps the model able to move the dial again after a drag.
    property real dialValue: root.param !== undefined && root.param.value !== undefined
                             ? Number(root.param.value) : 0
    Binding on dialValue {
        when: !dialDrag.pressed
        value: root.param !== undefined && root.param.value !== undefined
               ? Number(root.param.value) : 0
    }

    // ---- bool / enum: label + value chip that opens its choice list ------
    Text {
        visible: !root.numeric
        anchors.top: parent.top
        anchors.topMargin: Math.max(4, Math.round((parent.height - 40) / 2))
        width: parent.width - 8
        height: 14
        text: root.param !== undefined && root.param.name !== undefined ? root.param.name : ""
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Text {
        visible: !root.numeric
        anchors.top: parent.top
        anchors.topMargin: Math.max(4, Math.round((parent.height - 40) / 2)) + 16
        width: parent.width - 40
        height: 20
        text: root.param !== undefined && root.param.valueText !== undefined
              ? root.param.valueText : ""
        color: Style.text
        font.pixelSize: Style.fontTitle
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Text {
        visible: !root.numeric
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: "v"
        color: Style.textDim
        font.pixelSize: Style.fontBody
        font.weight: Font.DemiBold
    }

    TapHandler {
        enabled: !root.numeric
        onTapped: app.openPipeParam(root.pipeIndex, root.paramIndex, root)
    }
}
