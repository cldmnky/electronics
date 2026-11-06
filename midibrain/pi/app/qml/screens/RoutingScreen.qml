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
    readonly property var outputNames: ["TRS OUT 1", "TRS OUT 2", "USB OUT"]
    readonly property int patchRows: Math.max(1, Math.max(routing.inputCount,
                                                           routing.outputCount))

    Column {
        anchors.fill: parent
        anchors.margins: screen.shiftFace ? Style.marginCompact : Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: parent.width - (screen.shiftFace ? (routing.shiftPage === 0 && routing.filterPageCount > 1 ? 424 : 284) : 220)
                height: parent.height
                text: (screen.shiftFace
                       ? (screen.pipesPage ? "SHIFT  MIDI FX PIPES - one ordered chain per route"
                                           : "SHIFT  FILTER / TRANSPOSE / CLOCK OUT")
                       : "ROUTING - tap a wire, or an input then an output")
                      + (routing.noPicoMode ? "  -  NO PICO: TRS UNAVAILABLE" : "")
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: screen.shiftFace ? 70 : 0
                height: parent.height
                visible: screen.shiftFace
                text: "FILTER"
                textSize: Style.fontSmall
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
            Row {
                width: (screen.shiftFace && routing.shiftPage === 0 &&
                        routing.filterPageCount > 1) ? 132 : 0
                height: parent.height
                spacing: 0
                visible: width > 0
                TouchButton {
                    width: Style.touch
                    height: parent.height
                    text: "<"
                    textSize: Style.fontTab
                    focusId: "shift.routing.filter.prev"
                    onClicked: routing.setFilterPage(routing.filterPage - 1)
                }
                Text {
                    width: 44
                    height: parent.height
                    text: (routing.filterPage + 1) + "/" + routing.filterPageCount
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                TouchButton {
                    width: Style.touch
                    height: parent.height
                    text: ">"
                    textSize: Style.fontTab
                    focusId: "shift.routing.filter.next"
                    onClicked: routing.setFilterPage(routing.filterPage + 1)
                }
            }
            // The MIDI FX pipe chain lives on this tab's SHIFT face, which the
            // panel reaches with a SHIFT long-hold and touch with a long press
            // on the tab. This affordance names it on the primary face instead
            // of duplicating the screen.
            TouchButton {
                width: screen.shiftFace ? 0 : 96
                height: parent.height
                visible: !screen.shiftFace
                text: "SHIFT PIPES"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: "routing.shiftpipes"
                onClicked: {
                    app.showShiftFace(true)
                    routing.setShiftPage(1)
                    app.showTextPopup("SHIFT PIPES", "MIDI FX chains")
                }
            }
            TouchButton {
                width: 112
                height: parent.height
                visible: !screen.pipesPage
                text: "RESET"
                variant: "danger"
                focusId: screen.shiftFace ? "shift.routing.reset" : "routing.reset"
                onClicked: routing.reset()
            }
        }

        Flickable {
            id: patchFlick
            width: parent.width
            visible: !screen.pipesPage
            height: screen.shiftFace ? Math.max(88, screen.height - Style.marginCompact * 2 - Style.touch * 6 - Style.gap * 2) : Math.max(120, screen.height - Style.margin * 2 - Style.touch - Style.gap)
            contentWidth: width
            contentHeight: Math.max(height, screen.patchRows * Style.touch)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Item {
                id: patchbay
                width: patchFlick.width
                height: patchFlick.contentHeight

                Rectangle {
                    anchors.fill: parent
                    color: Style.panel
                    radius: Style.cornerPanel
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
                        function onDevicesChanged() { wires.requestPaint() }
                    }

                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const rows = Math.max(1, Math.max(routing.inputCount,
                                                          routing.outputCount))
                        const nodeW = width * routing.nodeWidthFraction
                        const nodeH = height / rows
                        const handle = (width - 2 * nodeW) * routing.wireHandleFraction
                        for (let s = 0; s < routing.inputCount; ++s) {
                            for (let d = 0; d < routing.outputCount; ++d) {
                                const enabled = routing.routes[s * routing.outputCount + d] === true
                                const selected = routing.selectedInput === s
                                // Draw every route that is on, plus the possible
                                // targets of the selected input. A long per-device
                                // list would otherwise paint hundreds of idle grey
                                // curves and bury the live wiring.
                                if (!enabled && !selected) continue
                                const dimmed = routing.selectedInput >= 0 && !selected
                                let color = Style.outline
                                let thickness = Math.max(2, nodeH * 0.045)
                                if (enabled) {
                                    color = dimmed ? Qt.rgba(0.31, 0.76, 0.97, 0.35) : Style.accent
                                    thickness *= selected ? 1.6 : 1.0
                                } else if (selected) {
                                    color = Style.textDim
                                    thickness *= 0.8
                                }
                                const x0 = nodeW
                                const y0 = height * (s + 0.5) / rows
                                const x1 = width - nodeW
                                const y1 = height * (d + 0.5) / rows
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
                    model: routing.inputNodes
                    delegate: Rectangle {
                        readonly property int nodeIndex: modelData.nodeIndex
                        readonly property bool available: routing.inputAvailable(nodeIndex)
                        x: 0
                        y: patchbay.height * (nodeIndex + 0.5) / screen.patchRows - height / 2
                        width: patchbay.width * routing.nodeWidthFraction
                        height: patchbay.height / screen.patchRows
                        radius: Style.corner
                        property string focusId: "routing.node.in." + modelData.port
                        readonly property bool focused: app.focusId === focusId
                        color: !available ? Style.panelDark
                                          : (routing.selectedInput === nodeIndex ? Style.accent : Style.panel)
                        border.width: (routing.selectedInput === nodeIndex || focused) ? 2 : Style.outlineWidth
                        border.color: (routing.selectedInput === nodeIndex || focused)
                                      ? Style.accent : Style.outline

                        Column {
                            anchors.centerIn: parent
                            spacing: 1
                            Text {
                                width: parent.parent.width - 8
                                text: modelData.name
                                color: !available ? Style.textDim
                                                  : (routing.selectedInput === nodeIndex ? Style.panelDark : Style.text)
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: !available
                                text: "UNAVAILABLE (NO PICO)"
                                color: Style.record
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: available && !modelData.dynamic && modelData.port === 2
                                text: "VIRTUAL PORT"
                                color: routing.selectedInput === nodeIndex ? Style.panelDark : Style.textDim
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: modelData.dynamic && !modelData.connected
                                text: "DORMANT"
                                color: Style.record
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: modelData.dynamic && modelData.connected
                                text: "#" + modelData.port
                                color: routing.selectedInput === nodeIndex ? Style.panelDark : Style.textDim
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        TapHandler {
                            onTapped: routing.selectInput(nodeIndex)
                        }
                    }
                }

                Repeater {
                    model: routing.outputNodes
                    delegate: Rectangle {
                        readonly property int nodeIndex: modelData.nodeIndex
                        readonly property bool available: routing.outputAvailable(nodeIndex)
                        readonly property bool wired: routing.selectedInput >= 0 &&
                                                      routing.routes[routing.selectedInput * routing.outputCount + nodeIndex] === true
                        x: patchbay.width - width
                        y: patchbay.height * (nodeIndex + 0.5) / screen.patchRows - height / 2
                        width: patchbay.width * routing.nodeWidthFraction
                        height: patchbay.height / screen.patchRows
                        radius: Style.corner
                        property string focusId: "routing.node.out." + modelData.port
                        readonly property bool focused: app.focusId === focusId
                        color: available ? Style.panel : Style.panelDark
                        border.width: (wired || focused) ? 2 : Style.outlineWidth
                        border.color: (wired || focused) ? Style.accent : Style.outline

                        Column {
                            anchors.centerIn: parent
                            spacing: 1
                            Text {
                                width: parent.parent.width - 8
                                text: modelData.name
                                color: available ? Style.text : Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: !available
                                text: "UNAVAILABLE (NO PICO)"
                                color: Style.record
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: available && !modelData.dynamic && modelData.port === 5
                                text: routing.usbOutputs + (routing.usbOutputs === 1 ? " PORT" : " PORTS")
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: modelData.dynamic && !modelData.connected
                                text: "DORMANT"
                                color: Style.record
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                visible: modelData.dynamic && modelData.connected
                                text: "#" + modelData.port
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        TapHandler {
                            onTapped: routing.selectOutput(nodeIndex)
                        }
                    }
                }

                TapHandler {
                    onTapped: (eventPoint) => {
                        const p = eventPoint.position
                        const code = routing.hitWireTest(p.x, p.y, patchbay.width, patchbay.height)
                        if (code >= 0) {
                            const destination = code % routing.outputCount
                            const source = Math.floor(code / routing.outputCount)
                            routing.selectedInput = source
                            routing.toggleRoute(source, destination)
                        }
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

                // One page shows three input nodes (fixed TRS/USB first, then
                // per-device USB ports). The page is picked in the top row so
                // every mask row keeps its full 44 px touch height.
                Repeater {
                    model: routing.filterPageRows
                    delegate: Row {
                        property int sourceIndex: modelData.nodeIndex
                        width: parent.width
                        height: Style.touch
                        spacing: 0

                        Text {
                            width: 62
                            height: parent.height
                            text: modelData.name
                            color: routing.inputAvailable(sourceIndex) ? Style.text : Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 2
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
                        model: routing.filterPageRows
                        delegate: Row {
                            property int sourceIndex: modelData.nodeIndex
                            width: (parent.width - 8) / 3
                            height: Style.touch
                            spacing: 2

                            Text {
                                width: 64
                                height: parent.height
                                text: modelData.name
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TouchButton {
                                width: Style.touch
                                height: Style.touch
                                text: "-"
                                textSize: 20
                                onClicked: routing.adjustTranspose(sourceIndex, -1)
                            }
                            Rectangle {
                                width: Math.max(Style.touch, parent.width - 2 * Style.touch - 6 - 64)
                                height: Style.touch
                                color: Style.panelDark
                                border.width: app.focusId === ("shift.routing.transpose." + sourceIndex) ? 2 : Style.outlineWidth
                                border.color: app.focusId === ("shift.routing.transpose." + sourceIndex)
                                              ? Style.accent : Style.outline
                                Text {
                                    anchors.centerIn: parent
                                    text: (routing.transposes[sourceIndex] || 0) + " st"
                                    color: Style.text
                                    font.pixelSize: Style.fontBody
                                    font.weight: Font.DemiBold
                                }
                            }
                            TouchButton {
                                width: Style.touch
                                height: Style.touch
                                text: "+"
                                textSize: 20
                                onClicked: routing.adjustTranspose(sourceIndex, 1)
                            }
                        }
                    }
                }

                // Per-output clock/transport gates (session v2 clock). TRS OUT1/2
                // go to the Pico as EV_CLOCK_ROUTE; USB OUT gates the Pi-side
                // mirror in UsbClock. The open gate is the bright one. Per-device
                // USB clock gates are not part of the session clock format yet.
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
                                width: 64
                                height: parent.height
                                text: screen.outputNames[index]
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TouchButton {
                                width: (parent.width - 68) / 2
                                height: Style.touch
                                text: "CLOCK"
                                textSize: Style.fontSmall
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
                                width: (parent.width - 68) / 2
                                height: Style.touch
                                text: "TRANSP"
                                textSize: Style.fontSmall
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
        // add/remove/reorder/bypass, the parameter editors PipeParamEditor
        // builds from the generated schema metadata, and the chain preset file
        // actions (EXPORT/IMPORT) that travel inside the project folder.
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
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "<"
                        textSize: 16
                        focusId: "shift.routing.pipes.src.prev"
                        onClicked: routing.cyclePipeSource(-1)
                    }
                    TouchButton {
                        width: (parent.width - 40 - 24 - Style.touch * 4 - 18) / 2
                        height: parent.height
                        text: routing.pipeSourceName
                        textSize: Style.fontSmall
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.src.name"
                        onClicked: app.openConfigFrom("pipesource", this)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: ">"
                        textSize: 16
                        focusId: "shift.routing.pipes.src.next"
                        onClicked: routing.cyclePipeSource(1)
                    }
                    Text {
                        width: 24
                        height: parent.height
                        text: "TO"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "<"
                        textSize: 16
                        focusId: "shift.routing.pipes.dst.prev"
                        onClicked: routing.cyclePipeDestination(-1)
                    }
                    TouchButton {
                        width: (parent.width - 40 - 24 - Style.touch * 4 - 18) / 2
                        height: parent.height
                        text: routing.pipeDestinationName
                        textSize: Style.fontSmall
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.dst.name"
                        onClicked: app.openConfigFrom("pipedest", this)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: ">"
                        textSize: 16
                        focusId: "shift.routing.pipes.dst.next"
                        onClicked: routing.cyclePipeDestination(1)
                    }
                }

                Text {
                    width: parent.width
                    height: 14
                    text: routing.pipeStatusText
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
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
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
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
                        width: 140
                        height: parent.height
                        text: routing.addTypeName
                        textSize: Style.fontSmall
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.add.type"
                        onClicked: app.openConfigFrom("pipetype", this)
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
                        width: 96
                        height: parent.height
                        text: "ADD PIPE"
                        textSize: Style.fontSmall
                        checked: true
                        focusId: "shift.routing.pipes.add"
                        onClicked: routing.addPipe()
                    }
                    TouchButton {
                        width: 104
                        height: parent.height
                        text: "SCOPE  " + routing.selectedPipeScopeText
                        textSize: Style.fontSmall
                        focusId: "shift.routing.pipes.scope"
                        onClicked: app.openConfigFrom("pipechannels", this)
                    }
                    // Chain preset file: the selected route's ordered chain as
                    // one JSON file in the project folder (take-name style
                    // entry, .json implied).
                    TouchButton {
                        width: 96
                        height: parent.height
                        text: "EXPORT"
                        textSize: Style.fontSmall
                        variant: "ghost"
                        focusId: "shift.routing.pipes.chain.export"
                        onClicked: app.requestExportPipeChain()
                    }
                    TouchButton {
                        width: 96
                        height: parent.height
                        text: "IMPORT"
                        textSize: Style.fontSmall
                        variant: "ghost"
                        focusId: "shift.routing.pipes.chain.import"
                        onClicked: app.requestImportPipeChain()
                    }
                    Text {
                        width: parent.width - 40 - Style.touch * 2 - 140 - 96 - 104 - 96 * 2 - Style.gapTight * 7
                        height: parent.height
                        text: routing.pipeRows.length + " / 8 PIPES"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
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
                            textSize: Style.fontSmall
                            checked: routing.selectedPipe === index
                            focusId: "shift.routing.pipes.select." + index
                            onClicked: routing.selectPipe(index)
                        }
                        Text {
                            width: 158
                            height: parent.height
                            text: modelData.status
                            color: modelData.clocked ? Style.accent : Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: modelData.clocked ? Font.DemiBold : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        TouchButton {
                            width: 54
                            height: parent.height
                            text: "BYP"
                            textSize: Style.fontSmall
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
                            onClicked: routing.movePipe(index, -1)
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "v"
                            textSize: 16
                            focusId: "shift.routing.pipes.down." + index
                            onClicked: routing.movePipe(index, 1)
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "X"
                            textSize: Style.fontSmall
                    variant: "danger"
                            focusId: "shift.routing.pipes.remove." + index
                            onClicked: routing.removePipe(index)
                        }
                        Text {
                            width: parent.width - 132 - 158 - 54 - Style.touch * 3 - 18
                            height: parent.height
                            text: modelData.paramCount + " PARAMS"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
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
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 40
                        height: parent.height
                        text: "<"
                        textSize: Style.fontBody
                        focusId: "shift.routing.pipes.param.page.prev"
                        onClicked: routing.setParamPage(routing.paramPage - 1)
                    }
                    Text {
                        width: 88
                        height: parent.height
                        text: (routing.paramPage + 1) + " / " + routing.paramPageCount
                        color: Style.text
                        font.pixelSize: Style.fontSmall
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 40
                        height: parent.height
                        text: ">"
                        textSize: Style.fontBody
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
                        font.pixelSize: Style.fontSmall
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
