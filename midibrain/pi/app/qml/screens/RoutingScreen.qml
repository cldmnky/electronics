import QtQuick
import Mnkybox 1.0
import "../components"

// Node-wire patchbay over per-source channel filters and transpose. Tap a wire
// to toggle it, or an input then an output. The QML geometry uses the same
// fractions as routing_model.hpp and calls its hit test through the view model.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"
    readonly property var inputNames: ["TRS IN 1", "TRS IN 2", "USB IN"]
    readonly property var outputNames: ["TRS OUT 1", "TRS OUT 2", "USB OUT"]

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        Row {
            width: parent.width
            height: Style.touch
            spacing: 6

            Text {
                width: parent.width - 118
                height: parent.height
                text: (screen.shiftFace ? "SHIFT  CHANNEL FILTER / TRANSPOSE"
                                        : "ROUTING - tap a wire, or an input then an output")
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: 112
                height: parent.height
                text: "RESET"
                focusId: screen.shiftFace ? "shift.routing.reset" : "routing.reset"
                onClicked: routing.reset()
            }
        }

        Item {
            id: patchbay
            width: parent.width
            height: screen.shiftFace ? Math.max(96, screen.height - Style.margin * 2 - Style.touch * 5 - 16) : Math.max(120, screen.height - Style.margin * 2 - Style.touch - 4)

            Rectangle {
                anchors.fill: parent
                color: Style.panelDark
                radius: Style.corner
                border.width: Style.outlineWidth
                border.color: Style.outline
            }

            Canvas {
                id: wires
                anchors.fill: parent

                Connections {
                    target: routing
                    function onRoutesChanged() { wires.requestPaint() }
                    function onSelectedInputChanged() { wires.requestPaint() }
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const nodeW = width * routing.nodeWidthFraction
                    const handle = (width - 2 * nodeW) * routing.wireHandleFraction
                    for (let s = 0; s < 3; ++s) {
                        for (let d = 0; d < 3; ++d) {
                            const enabled = routing.routes[s * 3 + d] === true
                            const selected = routing.selectedInput === s
                            const dimmed = routing.selectedInput >= 0 && !selected
                            let color = Style.outline
                            let thickness = Math.max(2, height * routing.nodeHeightFraction * 0.045)
                            if (enabled) {
                                color = dimmed ? Qt.rgba(0.31, 0.76, 0.97, 0.35) : Style.accent
                                thickness *= selected ? 1.6 : 1.0
                            } else if (dimmed) {
                                color = Qt.rgba(0.18, 0.20, 0.24, 0.7)
                                thickness *= 0.7
                            } else if (selected) {
                                color = Style.textDim
                                thickness *= 0.8
                            } else {
                                thickness *= 0.7
                            }
                            const x0 = nodeW
                            const y0 = height * (s + 0.5) / 3
                            const x1 = width - nodeW
                            const y1 = height * (d + 0.5) / 3
                            ctx.beginPath()
                            ctx.moveTo(x0, y0)
                            ctx.bezierCurveTo(x0 + handle, y0, x1 - handle, y1, x1, y1)
                            ctx.strokeStyle = color.toString()
                            ctx.lineWidth = thickness
                            ctx.lineCap = "round"
                            ctx.stroke()
                            if (enabled) {
                                ctx.fillStyle = color.toString()
                                ctx.beginPath()
                                ctx.arc(x0, y0, thickness * 1.6, 0, Math.PI * 2)
                                ctx.fill()
                                ctx.beginPath()
                                ctx.arc(x1, y1, thickness * 1.6, 0, Math.PI * 2)
                                ctx.fill()
                            }
                        }
                    }
                }
            }

            Repeater {
                model: 3
                delegate: Rectangle {
                    x: 0
                    y: patchbay.height * (index + 0.5) / 3 - height / 2
                    width: patchbay.width * routing.nodeWidthFraction
                    height: patchbay.height * routing.nodeHeightFraction
                    radius: Style.corner
                    readonly property bool focused: app.focusId === ("routing.node.in." + index)
                    color: routing.selectedInput === index ? Style.accent : Style.panel
                    border.width: (routing.selectedInput === index || focused) ? 2 : Style.outlineWidth
                    border.color: (routing.selectedInput === index || focused)
                                  ? Style.accent : Style.outline

                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            text: screen.inputNames[index]
                            color: routing.selectedInput === index ? Style.panelDark : Style.text
                            font.pixelSize: 12
                            font.bold: true
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: index === 2
                            text: routing.usbInputs + (routing.usbInputs === 1 ? " DEVICE" : " DEVICES")
                            color: routing.selectedInput === index ? Style.panelDark : Style.textDim
                            font.pixelSize: 10
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    TapHandler {
                        onTapped: routing.selectInput(index)
                    }
                }
            }

            Repeater {
                model: 3
                delegate: Rectangle {
                    x: patchbay.width - width
                    y: patchbay.height * (index + 0.5) / 3 - height / 2
                    width: patchbay.width * routing.nodeWidthFraction
                    height: patchbay.height * routing.nodeHeightFraction
                    radius: Style.corner
                    readonly property bool focused: app.focusId === ("routing.node.out." + index)
                    color: Style.panel
                    border.width: ((routing.selectedInput >= 0 &&
                                    routing.routes[routing.selectedInput * 3 + index] === true) ||
                                   focused) ? 2 : Style.outlineWidth
                    border.color: ((routing.selectedInput >= 0 &&
                                    routing.routes[routing.selectedInput * 3 + index] === true) ||
                                   focused) ? Style.accent : Style.outline

                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            text: screen.outputNames[index]
                            color: Style.text
                            font.pixelSize: 12
                            font.bold: true
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: index === 2
                            text: routing.usbOutputs + (routing.usbOutputs === 1 ? " PORT" : " PORTS")
                            color: Style.textDim
                            font.pixelSize: 10
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    TapHandler {
                        onTapped: routing.selectOutput(index)
                    }
                }
            }

            TapHandler {
                onTapped: (eventPoint) => {
                    const p = eventPoint.position
                    const code = routing.hitWireTest(p.x, p.y, patchbay.width, patchbay.height)
                    if (code >= 0) {
                        const source = Math.floor(code / 3)
                        const destination = code % 3
                        routing.selectedInput = source
                        routing.toggleRoute(source, destination)
                    }
                }
            }
        }

        Item {
            visible: screen.shiftFace
            width: parent.width
            height: Style.touch * 4

            Column {
                width: parent.width
                height: parent.height
                spacing: 0

                Repeater {
                    model: 3
                    delegate: Row {
                        property int sourceIndex: index
                        width: parent.width
                        height: Style.touch
                        spacing: 0

                        Text {
                            width: 84
                            height: parent.height
                            text: screen.inputNames[index]
                            color: Style.text
                            font.pixelSize: Style.fontSmall
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 4
                        }

                        Repeater {
                            model: 16
                            delegate: TouchButton {
                                width: Style.touch
                                height: Style.touch
                                text: String(index + 1)
                                textSize: Style.fontSmall
                                checked: ((routing.channelMasks[parent.sourceIndex] || 0) >> index) & 1
                                focusId: "shift.routing.mask." + parent.sourceIndex + "." + index
                                onClicked: routing.setMaskBit(parent.sourceIndex, index,
                                                              !(((routing.channelMasks[parent.sourceIndex] || 0) >> index) & 1))
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Repeater {
                        model: 3
                        delegate: Row {
                            width: (parent.width - 8) / 3
                            height: Style.touch
                            spacing: 2

                            Text {
                                width: 74
                                height: parent.height
                                text: screen.inputNames[index]
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.bold: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TouchButton {
                                width: Style.touch
                                height: Style.touch
                                text: "-"
                                textSize: 20
                                onClicked: routing.adjustTranspose(index, -1)
                            }
                            Rectangle {
                                width: Math.max(Style.touch, parent.width - 2 * Style.touch - 6 - 74)
                                height: Style.touch
                                color: Style.panelDark
                                border.width: app.focusId === ("shift.routing.transpose." + index) ? 2 : Style.outlineWidth
                                border.color: app.focusId === ("shift.routing.transpose." + index)
                                              ? Style.accent : Style.outline
                                Text {
                                    anchors.centerIn: parent
                                    text: (routing.transposes[index] || 0) + " st"
                                    color: Style.text
                                    font.pixelSize: Style.fontBody
                                    font.bold: true
                                }
                            }
                            TouchButton {
                                width: Style.touch
                                height: Style.touch
                                text: "+"
                                textSize: 20
                                onClicked: routing.adjustTranspose(index, 1)
                            }
                        }
                    }
                }
            }
        }
    }
}
