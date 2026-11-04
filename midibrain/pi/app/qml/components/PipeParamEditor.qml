import QtQuick
import Mnkybox 1.0

// One generated pipe parameter row. The generator mapping is owned by
// pipe_params.cpp: editor strings "toggle", "choice", "level", "note", "ms",
// "signed" and the default "stepper" select one of the controls below, so a new
// parameter kind is a table entry, not a new hand-written screen.
Item {
    id: root

    property var param
    property int pipeIndex: 0

    height: Style.touch

    function setValue(value) {
        const minimum = Number(param.minimum)
        const maximum = Number(param.maximum)
        const clamped = Math.max(minimum, Math.min(maximum, value))
        routing.setPipeParam(root.pipeIndex, Number(param.param), clamped)
    }

    Text {
        width: 150
        height: parent.height
        text: param.name
        color: Style.textDim
        font.pixelSize: 10
        font.bold: true
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
        focusId: "shift.routing.pipes.param." + root.pipeIndex + "." + param.param
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
            focusId: "shift.routing.pipes.param." + root.pipeIndex + "." + param.param
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

    Row {
        x: 154
        width: parent.width - 154
        height: parent.height
        spacing: 2
        visible: param.editor !== "toggle" && param.editor !== "choice" &&
                 param.editor !== "level"

        TouchButton {
            width: Style.touch
            height: parent.height
            text: "-"
            textSize: 18
            onClicked: root.setValue(param.value - (param.editor === "ms" ? 10 : 1))
        }
        Rectangle {
            width: parent.width - Style.touch * 2 - 4
            height: parent.height
            color: Style.panelDark
            border.width: app.focusId === ("shift.routing.pipes.param." + root.pipeIndex + "." + param.param) ? 2 : Style.outlineWidth
            border.color: app.focusId === ("shift.routing.pipes.param." + root.pipeIndex + "." + param.param)
                          ? Style.accent : Style.outline
            Text {
                anchors.centerIn: parent
                text: param.valueText
                color: Style.text
                font.pixelSize: Style.fontBody
                font.bold: true
            }
        }
        TouchButton {
            width: Style.touch
            height: parent.height
            text: "+"
            textSize: 18
            onClicked: root.setValue(param.value + (param.editor === "ms" ? 10 : 1))
        }
    }

    Row {
        x: 154
        width: parent.width - 154
        height: parent.height
        spacing: 2
        visible: param.editor === "level"

        TouchSlider {
            width: parent.width - 68
            height: parent.height
            minimumValue: Number(param.minimum)
            maximumValue: Number(param.maximum)
            value: Number(param.value)
            focusId: "shift.routing.pipes.param." + root.pipeIndex + "." + param.param
            popupLabel: param.name
            popupKind: "percent"
            onMoved: (value) => root.setValue(value)
        }
        Text {
            width: 64
            height: parent.height
            text: param.valueText
            color: Style.text
            font.pixelSize: 10
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}
