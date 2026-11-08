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
    // the route the last wire tap selected. Kept separately from
    // selectedInput so both endpoints can light together while the route is
    // being inspected (the tap also toggles the wire).
    property int selectedDestination: -1

    // QML tap slop: hitWireTest is exact, so probe a narrow vertical band
    // around the finger. Wires are near-horizontal; this widens the target
    // without moving the drawing or claiming a route the user cannot see.
    function routeAt(px, py) {
        const offsets = [0, -10, 10, -18, 18]
        for (let i = 0; i < offsets.length; ++i) {
            const code = routing.hitWireTest(px, py + offsets[i],
                                             patchbay.width, patchbay.height)
            if (code >= 0) return code
        }
        return -1
    }
    readonly property var inputNames: ["TRS IN 1", "TRS IN 2", "USB IN"]
    readonly property var outputNames: ["TRS OUT 1", "TRS OUT 2", "USB OUT"]
    readonly property int patchRows: Math.max(1, Math.max(routing.inputCount,
                                                           routing.outputCount))

    // press-and-hold RESET. The fill shows the remaining 800 ms; an early
    // release says why nothing happened instead of wiping silently.
    component HoldResetButton: Rectangle {
        id: hold

        property string focusId: ""
        property real progress: 0.0
        signal activated()

        readonly property bool focused: app.focusId === focusId && focusId !== ""

        color: Style.dangerFill
        radius: Style.corner
        border.width: hold.focused ? 2 : 1
        border.color: hold.focused ? Style.accent : Style.record

        Rectangle {
            width: parent.width * hold.progress
            height: parent.height
            radius: Style.corner
            color: Qt.rgba(0.949, 0.42, 0.42, 0.35)
        }
        Text {
            anchors.centerIn: parent
            text: "HOLD TO RESET"
            color: Style.record
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Timer {
            id: holdTimer
            interval: 16
            repeat: true
            onTriggered: {
                hold.progress = Math.min(1.0, hold.progress + 16.0 / 800.0)
                if (hold.progress >= 1.0) {
                    holdTimer.stop()
                    hold.activated()
                }
            }
        }
        TapHandler {
            onPressedChanged: {
                if (pressed) {
                    hold.progress = 0.0
                    holdTimer.restart()
                } else {
                    holdTimer.stop()
                    if (hold.progress < 1.0) {
                        app.showTextPopup("RESET", "RESET requires a hold")
                    }
                    hold.progress = 0.0
                }
            }
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: screen.shiftFace ? Style.marginCompact : Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                // On the pipes page this line is the route readout: chain
                // length, which side evaluates it and any clocked-pipe caveat.
                width: parent.width - (screen.pipesPage ? 156
                                       : (screen.shiftFace
                                          ? (routing.shiftPage === 0 && routing.filterPageCount > 1 ? 444 : 304)
                                          : 244))
                height: parent.height
                text: (screen.pipesPage ? routing.pipeStatusText
                       : (screen.shiftFace
                          ? "SHIFT  FILTER / TRANSPOSE / CLOCK OUT"
                          : "ROUTING - tap a wire"
                            + (routing.noPicoMode ? "  -  NO PICO: TRS UNAVAILABLE" : "")))
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
            // a patchbay is ten minutes of work, so touch resets only on
            // an 800 ms hold with a fill; the panel asks for confirmation (the
            // focus press has no duration).
            HoldResetButton {
                width: 132
                height: parent.height
                visible: !screen.pipesPage
                focusId: screen.shiftFace ? "shift.routing.reset" : "routing.reset"
                onActivated: routing.reset()
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
                                const selectedRoute = selected &&
                                                      screen.selectedDestination === d
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
                                if (selectedRoute) {
                                    // The selected route reads as one path, not
                                    // as a source highlight plus a list of all
                                    // possible targets.
                                    ctx.beginPath()
                                    ctx.moveTo(x0, y0)
                                    ctx.bezierCurveTo(x0 + handle, y0, x1 - handle, y1, x1, y1)
                                    ctx.strokeStyle = Style.text.toString()
                                    ctx.lineWidth = thickness + 2
                                    ctx.stroke()
                                }
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
                        border.width: focused ? 2 : 0
                        border.color: Style.text

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
                            onTapped: {
                                screen.selectedDestination = -1
                                routing.selectInput(nodeIndex)
                            }
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
                        readonly property bool endpoint: routing.selectedInput >= 0 &&
                                                        screen.selectedDestination === nodeIndex
                        x: patchbay.width - width
                        y: patchbay.height * (nodeIndex + 0.5) / screen.patchRows - height / 2
                        width: patchbay.width * routing.nodeWidthFraction
                        height: patchbay.height / screen.patchRows
                        radius: Style.corner
                        property string focusId: "routing.node.out." + modelData.port
                        readonly property bool focused: app.focusId === focusId
                        color: endpoint ? Style.accent
                                        : (available ? Style.panel : Style.panelDark)
                        border.width: focused ? 2 : (endpoint || wired ? 2 : 0)
                        border.color: focused ? Style.text : Style.accent

                        Column {
                            anchors.centerIn: parent
                            spacing: 1
                            Text {
                                width: parent.parent.width - 8
                                text: modelData.name
                                color: endpoint ? Style.panelDark
                                                : (available ? Style.text : Style.textDim)
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
                                color: endpoint ? Style.panelDark : Style.textDim
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
                            onTapped: {
                                screen.selectedDestination = nodeIndex
                                routing.selectOutput(nodeIndex)
                            }
                        }
                    }
                }

                TapHandler {
                    onTapped: (eventPoint) => {
                        const p = eventPoint.position
                        const code = screen.routeAt(p.x, p.y)
                        if (code >= 0) {
                            const destination = code % routing.outputCount
                            const source = Math.floor(code / routing.outputCount)
                            routing.selectedInput = source
                            screen.selectedDestination = destination
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
                                width: 56
                                height: parent.height
                                text: modelData.name
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            // Transpose as an arc dial: the panel encoder and a
                            // vertical drag move the same value, and the value
                            // commits on release so the rebuilt row cannot cut
                            // the drag short.
                            Item {
                                id: transposeCard
                                width: parent.width - 56
                                height: parent.height
                                property int dialValue: routing.transposes[sourceIndex] || 0
                                property string focusId: "shift.routing.transpose." + sourceIndex

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Style.corner
                                    color: Style.panelDark
                                    border.width: app.focusId === transposeCard.focusId ? 2
                                                  : Style.outlineWidth
                                    border.color: app.focusId === transposeCard.focusId
                                                  ? Style.accent : Style.outline
                                }
                                KnobDial {
                                    width: Style.touch - 4
                                    height: Style.touch - 4
                                    anchors.left: parent.left
                                    anchors.leftMargin: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    fraction: (transposeCard.dialValue + 24) / 48
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Style.touch + 2
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: transposeCard.dialValue + " st"
                                    color: Style.text
                                    font.pixelSize: Style.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    preventStealing: true

                                    property int pressY: 0
                                    property int pressValue: 0

                                    onPressed: (mouse) => {
                                        pressY = mouse.y
                                        pressValue = transposeCard.dialValue
                                        app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (!pressed) return
                                        const next = Math.max(-24, Math.min(24,
                                                    pressValue + Math.round((pressY - mouse.y) / 6)))
                                        transposeCard.dialValue = next
                                        app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                                        app.showTextPopup("TRANSPOSE " + modelData.name,
                                                          next + " st")
                                    }
                                    onReleased: {
                                        app.touchAnchorY = -1
                                        if (transposeCard.dialValue !== (routing.transposes[sourceIndex] || 0)) {
                                            routing.setTranspose(sourceIndex, transposeCard.dialValue)
                                        }
                                    }
                                }
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
                        // USB OUT is the Pi mirror: it has clock/transport gates
                        // only. The tempo divider and sync delay are firmware
                        // adjustments on the Pico's TRS outputs.
                        Text {
                            width: parent.width
                            height: 14
                            text: "USB OUT: gates only - clock adjustments apply to the Pico outputs"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // MIDI FX pipe page (Routing SHIFT > PIPES): one ordered chain per
        // route. Every selection is a popup selector (FROM, TO, pipe type,
        // parameter choices, chain menu) so no row carries a prev/next stepper;
        // the chain itself is one compact line per pipe and the selected pipe's
        // parameters are the same arc-dial cards the comp popup uses.
        Item {
            id: pipesPage
            visible: screen.pipesPage
            width: parent.width
            // The header row above and the outer column's gap are already
            // spoken for; everything left belongs to this page.
            height: Math.max(280, parent.height - Style.touch - Style.gap)

            // Two chain rows stay visible; the parameters below them show four
            // cards (two columns) and scroll for a longer schema.
            readonly property int chainListHeight: Style.touch * 2
            readonly property int paramAreaHeight: Math.max(Style.touch,
                                                            height - Style.touch * 3
                                                            - chainListHeight
                                                            - Style.gapTight * 4)
            readonly property int cardHeight: Math.max(Style.touch,
                                                       Math.floor((paramAreaHeight
                                                                   - Style.gapTight) / 2))

            Column {
                width: parent.width
                height: parent.height
                spacing: Style.gapTight

                // ROUTE ROW: the two live readouts that open the port lists.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    TouchButton {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        text: "FROM  " + routing.pipeSourceName
                        textSize: Style.fontBody
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.src.name"
                        onClicked: app.openConfigFrom("pipesource", this)
                    }
                    TouchButton {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        text: "TO  " + routing.pipeDestinationName
                        textSize: Style.fontBody
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.dst.name"
                        onClicked: app.openConfigFrom("pipedest", this)
                    }
                }

                // OUTPUT INSERTS: the selected TO output's ordered chain list.
                // ROUTE edits the normal FROM->TO chain; a numbered chip edits
                // that insert chain in the same pipe editor below. ADD appends
                // a chain (four max); ^/v reorder and X removes the selected
                // one. TRS outputs are Pico-owned, so this strip says so
                // instead of pretending the chain is selectable.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    Text {
                        width: 62
                        height: parent.height
                        text: "INSERTS"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    TouchButton {
                        width: 62
                        height: parent.height
                        text: "ROUTE"
                        textSize: Style.fontSmall
                        checked: routing.insertSlot === -1
                        visible: routing.outputInsertsAvailable
                        focusId: "shift.routing.pipes.insert.route"
                        onClicked: routing.selectRouteChain()
                    }
                    Repeater {
                        model: routing.insertRows
                        delegate: TouchButton {
                            width: 70
                            height: parent.height
                            text: modelData.text
                            textSize: Style.fontSmall
                            checked: modelData.selected
                            focusId: "shift.routing.pipes.insert.chain." + index
                            onClicked: routing.selectInsertChain(index)
                        }
                    }
                    TouchButton {
                        width: 54
                        height: parent.height
                        text: "+"
                        textSize: Style.fontTab
                        visible: routing.outputInsertsAvailable
                        focusId: "shift.routing.pipes.insert.add"
                        onClicked: routing.addInsertChain()
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "ON"
                        textSize: Style.fontSmall
                        checked: routing.insertSlot >= 0 &&
                                 (routing.insertRows[routing.insertSlot] !== undefined) &&
                                 routing.insertRows[routing.insertSlot].enabled
                        visible: routing.insertSlot >= 0
                        focusId: "shift.routing.pipes.insert.on." + routing.insertSlot
                        onClicked: routing.toggleInsertChain(routing.insertSlot)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "^"
                        textSize: 16
                        visible: routing.insertSlot >= 0
                        focusId: "shift.routing.pipes.insert.up." + routing.insertSlot
                        onClicked: routing.moveInsertChain(routing.insertSlot, -1)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "v"
                        textSize: 16
                        visible: routing.insertSlot >= 0
                        focusId: "shift.routing.pipes.insert.down." + routing.insertSlot
                        onClicked: routing.moveInsertChain(routing.insertSlot, 1)
                    }
                    TouchButton {
                        width: Style.touch
                        height: parent.height
                        text: "X"
                        textSize: Style.fontSmall
                        variant: "danger"
                        visible: routing.insertSlot >= 0
                        focusId: "shift.routing.pipes.insert.remove." + routing.insertSlot
                        onClicked: routing.removeInsertChain(routing.insertSlot)
                    }
                    Text {
                        width: Math.max(0, parent.width - 62 - 62 -
                                        routing.insertRows.length * 70 -
                                        (routing.outputInsertsAvailable ? 54 : 0) -
                                        (routing.insertSlot >= 0 ? Style.touch * 4 : 0) -
                                        Style.gapTight * 8)
                        height: parent.height
                        visible: !routing.outputInsertsAvailable
                        text: "PICO-OWNED OUTPUT - OUTPUT INSERTS ARE USB ONLY"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                // ADD ROW: type selector, ADD, the selected pipe's channel-scope
                // chip and the chain menu that owns the preset file actions.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    TouchButton {
                        width: 236
                        height: parent.height
                        text: routing.addTypeName
                        textSize: Style.fontSmall
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.add.type"
                        onClicked: app.openConfigFrom("pipetype", this)
                    }
                    TouchButton {
                        width: 88
                        height: parent.height
                        text: "ADD"
                        textSize: Style.fontBody
                        variant: "primary"
                        focusId: "shift.routing.pipes.add"
                        onClicked: routing.addPipe()
                    }
                    TouchButton {
                        width: 168
                        height: parent.height
                        text: "MIDI " + routing.selectedPipeScopeText + " >"
                        textSize: Style.fontSmall
                        focusId: "shift.routing.pipes.scope"
                        onClicked: app.openConfigFrom("pipechannels", this)
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: "CHAIN"
                        textSize: Style.fontSmall
                        variant: "ghost"
                        chevron: "down"
                        focusId: "shift.routing.pipes.chain"
                        onClicked: app.openConfigFrom("chainmenu", this)
                    }
                    Text {
                        width: parent.width - 236 - 88 - 168 - 92 - Style.gapTight * 4
                        height: parent.height
                        text: routing.pipeRows.length + " / 8 PIPES"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                // CHAIN: one line per pipe. Tapping a row selects it for
                // parameter editing; the bypass/move/remove buttons belong to
                // the selected row only, so an idle chain stays quiet.
                ListView {
                    id: chainList
                    width: parent.width
                    // The panel ring can land on a row that is scrolled out of
                    // view; bring it back instead of hiding the focus.
                    function syncToFocus() {
                        const prefix = "shift.routing.pipes.select."
                        if (!app.focusId.startsWith(prefix)) return
                        const row = Number(app.focusId.substring(prefix.length))
                        if (row >= 0 && row < count) positionViewAtIndex(row, ListView.Contain)
                    }
                    Connections {
                        target: app
                        function onFocusChanged() { chainList.syncToFocus() }
                    }
                    // A short chain does not reserve the two-row height: the
                    // parameter cards stay right under it.
                    height: Math.min(pipesPage.chainListHeight,
                                     Math.max(Style.touch,
                                              routing.pipeRows.length * Style.touch))
                    clip: true
                    cacheBuffer: 0
                    model: routing.pipeRows
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: chainRow
                        readonly property bool selected: routing.selectedPipe === index
                        width: chainList.width
                        height: Style.touch
                        radius: Style.corner
                        color: selected ? Style.accent : Style.panel
                        border.width: app.focusId === focusId ? 2 : Style.outlineWidth
                        border.color: app.focusId === focusId ? Style.accent : Style.outline
                        property string focusId: "shift.routing.pipes.select." + index

                        TapHandler {
                            onTapped: routing.selectPipe(index)
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            spacing: Style.gapTight

                            Text {
                                width: 132
                                height: parent.height
                                text: (index + 1) + ". " + modelData.typeName
                                color: chainRow.selected ? Style.panelDark : Style.text
                                font.pixelSize: Style.fontBody
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                width: parent.width - 132 - 58
                                       - (actions.visible ? actions.width + Style.gapTight * 3
                                                          : Style.gapTight * 2)
                                height: 26
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 4
                                color: Style.panelDark
                                border.width: 1
                                border.color: modelData.warning ? Style.record
                                              : (modelData.clocked ? Style.accent : Style.outline)

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    text: modelData.status
                                    color: modelData.warning ? Style.record
                                           : (modelData.bypass ? Style.textDim
                                              : (modelData.clocked ? Style.accent
                                                                   : Style.textDim))
                                    font.pixelSize: Style.fontSmall
                                    font.weight: Font.DemiBold
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                            Row {
                                id: actions
                                width: Style.touch * 4 + Style.gapHair * 3
                                height: parent.height
                                spacing: Style.gapHair
                                visible: chainRow.selected

                                TouchButton {
                                    width: Style.touch
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
                            }
                            Text {
                                width: 58
                                height: parent.height
                                text: modelData.paramCount + " PAR"
                                color: chainRow.selected ? Style.panelDark : Style.textDim
                                font.pixelSize: Style.fontSmall
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                // PARAM AREA: the selected pipe's schema parameters as two
                // columns of arc-dial cards; a bool/enum parameter is a chip
                // that opens its choice list. Four cards fit, the rest scroll.
                Text {
                    width: parent.width
                    height: pipesPage.paramAreaHeight
                    visible: routing.pipeParams.length === 0
                    text: "NO PIPE SELECTED - PICK A TYPE AND PRESS ADD"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                // naming truth. GENERATOR is a first-class, source-less
                // FROM (head = CC LFO, the schema's generator role). Output
                // inserts are selected on the INSERTS strip above; their chains
                // accept every Wave-1/Wave-2 modifier (heads stay generator
                // only). A refusal line replaces the hint.
                Text {
                    width: parent.width
                    height: 14
                    text: routing.addHint !== ""
                          ? routing.addHint
                          : (routing.insertHint !== ""
                             ? routing.insertHint
                             : "GENERATOR needs no input (head = CC LFO) - INSERT n edits an output chain; its pipes are ordinary modifiers")
                    color: (routing.addHint !== "" || routing.insertHint !== "")
                           ? Style.record : Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                ListView {
                    id: paramList
                    width: parent.width
                    height: pipesPage.paramAreaHeight
                    visible: routing.pipeParams.length > 0
                    clip: true
                    cacheBuffer: 0
                    model: Math.ceil(routing.pipeParams.length / 2)
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: Style.gapTight
                    // Same for the parameter cards: the ring reaches every
                    // schema parameter, so scroll to the focused card's row.
                    function syncToFocus() {
                        const prefix = "shift.routing.pipes.param."
                        if (!app.focusId.startsWith(prefix)) return
                        const rest = app.focusId.substring(prefix.length)
                        const dot = rest.indexOf(".")
                        if (dot < 0) return
                        if (Number(rest.substring(0, dot)) !== routing.selectedPipe) return
                        const row = Math.floor(Number(rest.substring(dot + 1)) / 2)
                        if (row >= 0 && row < count) positionViewAtIndex(row, ListView.Contain)
                    }
                    Connections {
                        target: app
                        function onFocusChanged() { paramList.syncToFocus() }
                    }

                    delegate: Row {
                        readonly property int first: index * 2
                        readonly property bool hasFirst: first < routing.pipeParams.length
                        readonly property bool hasSecond: first + 1 < routing.pipeParams.length
                        width: parent.width
                        height: pipesPage.cardHeight
                        spacing: Style.gapTight

                        PipeParamCard {
                            width: (parent.width - Style.gapTight) / 2
                            height: parent.height
                            visible: hasFirst
                            param: hasFirst ? routing.pipeParams[first] : ({})
                            pipeIndex: routing.selectedPipe
                        }
                        PipeParamCard {
                            width: (parent.width - Style.gapTight) / 2
                            height: parent.height
                            visible: hasSecond
                            param: hasSecond ? routing.pipeParams[first + 1] : ({})
                            pipeIndex: routing.selectedPipe
                        }
                    }
                }
            }
        }
    }
}
