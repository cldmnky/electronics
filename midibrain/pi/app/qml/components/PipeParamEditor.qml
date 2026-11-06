import QtQuick
import Mnkybox 1.0

// One pipe parameter row, built from the generated schema metadata
// (midifx/gen/generated/midifx_ui.hpp via pipe_ui.cpp). The model row carries
// the label, range, unit value text and the editor kind, so a new pipe type
// needs no QML and no per-type table:
//   "toggle"  one ON/OFF button            (schema bool)
//   "choice"  previous / name / next        (schema enum)
//   "knob"    arc dial + unit value popup   (schema value with a unit)
//   "stepper" -/+ over the schema range     (plain schema int)
Item {
    id: root

    property var param
    property int pipeIndex: 0

    height: Style.touch

    readonly property string paramFocusId: "shift.routing.pipes.param." +
                                           root.pipeIndex + "." + param.param

    function setValue(value) {
        const minimum = Number(param.minimum)
        const maximum = Number(param.maximum)
        const clamped = Math.max(minimum, Math.min(maximum, Math.round(value)))
        routing.setPipeParam(root.pipeIndex, Number(param.param), clamped)
    }

    Text {
        width: 150
        height: parent.height
        text: param.name
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    TouchButton {
        x: 154
        width: 120
        height: parent.height
        visible: param.editor === "toggle"
        text: param.valueText
        textSize: Style.fontSmall
        checked: param.value === 1
        focusId: root.paramFocusId
        onClicked: root.setValue(param.value === 1 ? 0 : 1)
    }

    Row {
        x: 154
        width: parent.width - 154
        height: parent.height
        spacing: 2
        visible: param.editor === "choice"

        TouchButton {
            width: Style.touch
            height: parent.height
            text: "<"
            textSize: 16
            onClicked: root.setValue(param.value - 1)
        }
        TouchButton {
            width: parent.width - Style.touch * 2 - 4
            height: parent.height
            text: param.valueText
            textSize: Style.fontSmall
            checked: true
            focusId: root.paramFocusId
            onClicked: root.setValue(param.value + 1)
        }
        TouchButton {
            width: Style.touch
            height: parent.height
            text: ">"
            textSize: 16
            onClicked: root.setValue(param.value + 1)
        }
    }

    // Arc dial for a value that carries a unit. The dial keeps its own value
    // while the finger is down and commits on release: every model write
    // reassigns the parameter list, which rebuilds this delegate, so a live
    // write would end the drag after the first pixel.
    Item {
        id: knobRow

        x: 154
        width: parent.width - 154
        height: parent.height
        visible: param.editor === "knob"

        property real dialValue: Number(param.value)

        readonly property string focusId: root.paramFocusId
        readonly property bool focused: app.focusId === focusId

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Style.corner
            border.width: knobRow.focused ? 2 : 0
            border.color: Style.accent
        }
        KnobDial {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: Style.touch - 4
            height: Style.touch - 4
            fraction: (knobRow.dialValue - Number(param.minimum)) /
                      Math.max(1, Number(param.maximum) - Number(param.minimum))
        }
        Text {
            x: Style.touch + 4
            width: parent.width - Style.touch - 8
            height: parent.height
            text: param.valueText
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true

            property real pressY: 0
            property real pressValue: 0

            onPressed: (mouse) => {
                pressY = mouse.y
                pressValue = knobRow.dialValue
                app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const minimum = Number(param.minimum)
                const span = Number(param.maximum) - minimum
                if (span <= 0) return
                const next = Math.max(minimum,
                                      Math.min(Number(param.maximum),
                                               pressValue + (pressY - mouse.y) / 180 * span))
                knobRow.dialValue = Math.round(next)
                app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                app.showTextPopup(param.name,
                                  routing.pipeParamValueText(Number(param.param),
                                                             knobRow.dialValue))
            }
            onReleased: {
                app.touchAnchorY = -1
                if (knobRow.dialValue !== Number(param.value)) {
                    root.setValue(knobRow.dialValue)
                }
            }
            onCanceled: app.touchAnchorY = -1
        }
    }

    Row {
        x: 154
        width: parent.width - 154
        height: parent.height
        spacing: 2
        visible: param.editor === "stepper"

        TouchButton {
            width: Style.touch
            height: parent.height
            text: "-"
            textSize: 18
            onClicked: root.setValue(param.value - 1)
        }
        Rectangle {
            width: parent.width - Style.touch * 2 - 4
            height: parent.height
            color: Style.panelDark
            radius: Style.corner
            border.width: app.focusId === root.paramFocusId ? 2 : Style.outlineWidth
            border.color: app.focusId === root.paramFocusId
                          ? Style.accent : Style.outline
            // The panel focus ring stops here; the +/- buttons step the value.
            property string focusId: root.paramFocusId
            Text {
                anchors.centerIn: parent
                text: param.valueText
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
            }
        }
        TouchButton {
            width: Style.touch
            height: parent.height
            text: "+"
            textSize: 18
            onClicked: root.setValue(param.value + 1)
        }
    }
}
