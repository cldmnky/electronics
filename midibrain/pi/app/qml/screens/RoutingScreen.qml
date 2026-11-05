import QtQuick
import Mnkybox 1.0
import "../components"

// Node-wire patchbay over per-source channel filters and transpose. Tap a wire
// to toggle it, or an input then an output. The QML geometry uses the same
// fractions as routing_model.hpp and calls its hit test through the view model.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"
    readonly property bool pipesPage: screen.shiftFace && routing.shiftPage === 1
    readonly property var inputNames: ["TRS IN 1", "TRS IN 2", "USB IN"]
    readonly property var inputShort: ["TRS1", "TRS2", "USB"]
    readonly property var outputNames: ["TRS OUT 1", "TRS OUT 2", "USB OUT"]
    readonly property var outputShort: ["TRS1", "TRS2", "USB"]

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        Row {
            width: parent.width
            height: Style.touch
            spacing: 6

            Text {
                width: parent.width - (screen.shiftFace ? 270 : 118)
                height: parent.height
                text: (screen.shiftFace
                       ? (screen.pipesPage ? "SHIFT  MIDI FX PIPES - one ordered chain per route"
                                           : "SHIFT  FILTER / TRANSPOSE / CLOCK OUT")
                       : "ROUTING - tap a wire, or an input then an output")
                      + (routing.noPicoMode ? "  -  NO PICO: TRS UNAVAILABLE" : "")
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: screen.shiftFace ? 70 : 0
                height: parent.height
                visible: screen.shiftFace
                text: "FILTER"
                textSize: 9
                checked: routing.shiftPage === 0
                focusId: "shift.routing.page.0"
                onClicked: routing.setShiftPage(0)
            }
            TouchButton {
                width: screen.shiftFace ? 70 : 0
                height: parent.height
                visible: screen.shiftFace
                text: "PIPES"
                textSize: Style.fontSmall
                checked: routing.shiftPage === 1
                focusId: "shift.routing.page.1"
                onClicked: routing.setShiftPage(1)
            }
            TouchButton {
                width: 112
                height: parent.height
                visible: !screen.pipesPage
                text: "RESET"
                focusId: screen.shiftFace ? "shift.routing.reset" : "routing.reset"
                onClicked: routing.reset()
            }
        }

        Item {
            id: patchbay
            width: parent.width
            visible: !screen.pipesPage
            height: screen.shiftFace ? Math.max(88, screen.height - Style.margin * 2 - Style.touch * 6 - 16) : Math.max(120, screen.height - Style.margin * 2 - Style.touch - 4)

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
                    readonly property bool available: routing.inputAvailable(index)
                    color: !available ? Style.panelDark
                                      : (routing.selectedInput === index ? Style.accent : Style.panel)
                    border.width: (routing.selectedInput === index || focused) ? 2 : Style.outlineWidth
                    border.color: (routing.selectedInput === index || focused)
                                  ? Style.accent : Style.outline

                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            text: screen.inputNames[index]
                            color: !available ? Style.textDim
                                              : (routing.selectedInput === index ? Style.panelDark : Style.text)
                            font.pixelSize: 12
                            font.bold: true
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: !available
                            text: "UNAVAILABLE (NO PICO)"
                            color: Style.record
                            font.pixelSize: 8
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: available && index === 2
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
                    readonly property bool available: routing.outputAvailable(index)
                    color: available ? Style.panel : Style.panelDark
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
                            color: available ? Style.text : Style.textDim
                            font.pixelSize: 12
                            font.bold: true
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: !available
                            text: "UNAVAILABLE (NO PICO)"
                            color: Style.record
                            font.pixelSize: 8
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            visible: available && index === 2
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
            visible: screen.shiftFace && routing.shiftPage === 0
            width: parent.width
            height: Style.touch * 5

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

                // Per-output clock/transport gates (session v2 clock). TRS OUT1/2
                // go to the Pico as EV_CLOCK_ROUTE; USB OUT gates the Pi-side
                // mirror in UsbClock. The open gate is the bright one.
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
                                text: screen.outputNames[index]
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.bold: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TouchButton {
                                width: (parent.width - 80) / 2
                                height: Style.touch
                                text: "CLOCK"
                                textSize: 9
                                active: !routing.noPicoMode || index === 2
                                checked: index === 0 ? session.trs1Clock
                                        : (index === 1 ? session.trs2Clock : session.usbClock)
                                focusId: "shift.clock.clock." + index
                                onClicked: {
                                    if (index === 0) session.trs1Clock = !session.trs1Clock
                                    else if (index === 1) session.trs2Clock = !session.trs2Clock
                                    else session.usbClock = !session.usbClock
                                    app.showTextPopup("CLOCK OUT " + screen.outputNames[index],
                                                      session.clockText)
                                }
                            }
                            TouchButton {
                                width: (parent.width - 80) / 2
                                height: Style.touch
                                text: "TRANSP"
                                textSize: 9
                                active: !routing.noPicoMode || index === 2
                                checked: index === 0 ? session.trs1Transport
                                        : (index === 1 ? session.trs2Transport : session.usbTransport)
                                focusId: "shift.clock.transport." + index
                                onClicked: {
                                    if (index === 0) session.trs1Transport = !session.trs1Transport
                                    else if (index === 1) session.trs2Transport = !session.trs2Transport
                                    else session.usbTransport = !session.usbTransport
                                    app.showTextPopup("TRANSPORT OUT " + screen.outputNames[index],
                                                      session.clockText)
                                }
                            }
                        }
                    }
                }
            }
        }

        // MIDI FX pipe page (Routing SHIFT > PIPES): per-route ordered chain,
        // add/remove/reorder/bypass, and the per-type parameter editors that
        // PipeParamEditor generates from pipe_params.cpp.
        Item {
            visible: screen.pipesPage
            width: parent.width
            height: Math.max(280, parent.height - Style.touch - 4)

            Column {
                width: parent.width
                height: parent.height
                spacing: 2

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3

                    Text {
                        width: 40
                        height: parent.height
                        text: "FROM"
                        color: Style.textDim
                        font.pixelSize: 9
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    Repeater {
                        model: 3
                        delegate: TouchButton {
                            width: (parent.width - 40 - 24 - 8) / 6
                            height: parent.height
                            text: screen.inputShort[index]
                            textSize: 10
                            checked: routing.pipeSource === index
                            focusId: "shift.routing.pipes.src." + index
                            onClicked: routing.pipeSource = index
                        }
                    }
                    Text {
                        width: 24
                        height: parent.height
                        text: "TO"
                        color: Style.textDim
                        font.pixelSize: 9
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    Repeater {
                        model: 3
                        delegate: TouchButton {
                            width: (parent.width - 40 - 24 - 8) / 6
                            height: parent.height
                            text: screen.outputShort[index]
                            textSize: 10
                            checked: routing.pipeDestination === index
                            focusId: "shift.routing.pipes.dst." + index
                            onClicked: routing.pipeDestination = index
                        }
                    }
                }

                Text {
                    width: parent.width
                    height: 14
                    text: routing.pipeStatusText
                    color: Style.textDim
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3

                    Text {
                        width: 40
                        height: parent.height
                        text: "ADD"
                        color: Style.textDim
                        font.pixelSize: 9
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "<"
                        textSize: 16
                        focusId: "shift.routing.pipes.type.prev"
                        onClicked: routing.cycleAddType(-1)
                    }
                    TouchButton {
                        width: 220
                        height: parent.height
                        text: routing.addTypeName
                        textSize: Style.fontSmall
                        focusId: "shift.routing.pipes.add.type"
                        onClicked: routing.cycleAddType(1)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: ">"
                        textSize: 16
                        focusId: "shift.routing.pipes.type.next"
                        onClicked: routing.cycleAddType(1)
                    }
                    TouchButton {
                        width: 110
                        height: parent.height
                        text: "ADD PIPE"
                        textSize: Style.fontSmall
                        checked: true
                        focusId: "shift.routing.pipes.add"
                        onClicked: routing.addPipe()
                    }
                    Text {
                        width: parent.width - 40 - Style.touch * 2 - 220 - 110 - 15
                        height: parent.height
                        text: routing.pipeRows.length + " / 8 PIPES"
                        color: Style.textDim
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                ListView {
                    width: parent.width
                    height: 88
                    clip: true
                    model: routing.pipeRows
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Row {
                        width: parent.width
                        height: Style.touch
                        spacing: 3

                        TouchButton {
                            width: 132
                            height: parent.height
                            text: (index + 1) + ". " + modelData.typeName
                            textSize: 9
                            checked: routing.selectedPipe === index
                            focusId: "shift.routing.pipes.select." + index
                            onClicked: routing.selectPipe(index)
                        }
                        Text {
                            width: 158
                            height: parent.height
                            text: modelData.status
                            color: modelData.clocked ? Style.soloOn : Style.textDim
                            font.pixelSize: 9
                            font.bold: modelData.clocked
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        TouchButton {
                            width: 54
                            height: parent.height
                            text: "BYP"
                            textSize: 9
                            checked: modelData.bypass
                            focusId: "shift.routing.pipes.bypass." + index
                            onClicked: routing.setPipeBypass(index, !modelData.bypass)
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "^"
                            textSize: 16
                            focusId: "shift.routing.pipes.up." + index
                            onClicked: {
                                routing.selectPipe(index)
                                routing.moveSelectedPipe(-1)
                            }
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "v"
                            textSize: 16
                            focusId: "shift.routing.pipes.down." + index
                            onClicked: {
                                routing.selectPipe(index)
                                routing.moveSelectedPipe(1)
                            }
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "X"
                            textSize: Style.fontSmall
                            idleColor: "#5b2222"
                            focusId: "shift.routing.pipes.remove." + index
                            onClicked: {
                                routing.selectPipe(index)
                                routing.removeSelectedPipe()
                            }
                        }
                        Text {
                            width: parent.width - 132 - 158 - 54 - Style.touch * 3 - 18
                            height: parent.height
                            text: modelData.paramCount + " PARAMS"
                            color: Style.textDim
                            font.pixelSize: 9
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: 22
                    spacing: 3

                    Text {
                        width: 60
                        height: parent.height
                        text: "PARAM"
                        color: Style.textDim
                        font.pixelSize: 9
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 40
                        height: parent.height
                        text: "<"
                        textSize: 12
                        focusId: "shift.routing.pipes.param.page.prev"
                        onClicked: routing.setParamPage(routing.paramPage - 1)
                    }
                    Text {
                        width: 88
                        height: parent.height
                        text: (routing.paramPage + 1) + " / " + routing.paramPageCount
                        color: Style.text
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 40
                        height: parent.height
                        text: ">"
                        textSize: 12
                        focusId: "shift.routing.pipes.param.page.next"
                        onClicked: routing.setParamPage(routing.paramPage + 1)
                    }
                    Text {
                        width: parent.width - 240
                        height: parent.height
                        text: routing.selectedPipe >= 0 && routing.pipeRows.length > 0
                              ? "EDIT PARAMS OF PIPE " + (routing.selectedPipe + 1)
                              : "ADD A PIPE TO EDIT PARAMS"
                        color: Style.textDim
                        font.pixelSize: 9
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Repeater {
                    model: routing.pipeParams
                    delegate: PipeParamEditor {
                        width: parent.width
                        height: Style.touch
                        pipeIndex: routing.selectedPipe
                        param: modelData
                    }
                }
            }
        }
    }
}
