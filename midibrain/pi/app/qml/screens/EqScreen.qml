import QtQuick
import Mnkybox 1.0
import "../components"

// EQ: log-frequency x gain grid with four draggable nodes (the selected one is
// pink), the summed response curve, and the selected band's Gain/Q/Frequency/
// Type parameter chips. Track selector plus MAIN picks the subject; FULL hides
// the shell's tab row and transport strip (app.fullscreen). SHIFT is the
// explicit band editor: knob arcs for the focused band and one band-type
// summary that opens the shared ConfigPopup.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    function logFraction(value, minimum, maximum) {
        if (!(value > 0)) return 0
        return Math.max(0, Math.min(1, Math.log(value / minimum) / Math.log(maximum / minimum)))
    }
    function gainFraction() {
        if (eq.cutBand) return 0
        return Math.max(0, Math.min(1, (parseFloat(eq.gainText) + 24) / 48))
    }
    function qFraction() {
        return logFraction(parseFloat(eq.qText), 0.1, 10)
    }
    function freqFraction() {
        return logFraction(parseFloat(eq.freqText), 20, 20000)
    }

    // ---- normal face ------------------------------------------------------
    Column {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: 58
                height: parent.height
                text: "SUBJECT"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "MAIN"
                checked: eq.subject === eq.mainSubject
                focusId: "eq.subject.main"
                onClicked: eq.selectMain()
            }
            // B14: the steppers belong to TRACK, so they say so; the readout
            // names the subject ("Main" or "02 bass"), not a second MAIN.
            Text {
                width: 44
                height: parent.height
                text: "TRACK"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: Style.touch
                height: parent.height
                text: "-"
                textSize: 20
                onClicked: eq.stepSubject(-1)
            }
            Rectangle {
                width: 96
                height: parent.height
                color: Style.panelDark
                radius: Style.corner
                border.width: app.focusId === "eq.subject.track" ? 2 : Style.outlineWidth
                border.color: app.focusId === "eq.subject.track" ? Style.accent : Style.outline
                Text {
                    anchors.centerIn: parent
                    text: eq.subjectText
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                }
                TapHandler {
                    onTapped: app.showTextPopup("EQ SUBJECT", eq.subjectText)
                }
            }
            TouchButton {
                width: Style.touch
                height: parent.height
                text: "+"
                textSize: 20
                onClicked: eq.stepSubject(1)
            }
            // The band chip steps with a tap and opens the type list on a hold.
            TouchButton {
                width: 176
                height: parent.height
                text: eq.bandText + "  " + eq.typeText
                textSize: Style.fontSmall
                checked: false
                chevron: "down"
                focusId: "eq.band.select"
                onClicked: eq.stepBand(1)
                onLongClicked: app.openConfigFrom("eqtype", this)
            }
            TouchButton {
                width: parent.width - 58 - 74 - 44 - Style.touch * 2 - 96 - 176 -
                       Style.gap * 7
                height: parent.height
                text: app.fullscreen ? "EXIT FULL" : "FULLSCREEN"
                textSize: Style.fontSmall
                variant: "ghost"
                checked: app.fullscreen
                focusId: "eq.fullscreen"
                onClicked: app.setFullscreen(!app.fullscreen)
            }
        }

        Text {
            width: parent.width
            height: 14
            text: "band chip: tap = next band - hold = type  -  drag a node to move it"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }

        Item {
            id: grid
            width: parent.width
            height: parent.height - Style.touch * 2 - 14 - Style.gap * 3

            Rectangle {
                anchors.fill: parent
                color: Style.panel
                radius: Style.cornerPanel
                border.width: app.focusId === "eq.node." + eq.band ? 2 : Style.outlineWidth
                border.color: app.focusId === "eq.node." + eq.band ? Style.accent : Style.outline
            }

            readonly property var freqTicks: [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]

            Canvas {
                id: plot
                anchors.fill: parent
                anchors.margins: 1

                Connections {
                    target: eq
                    function onCurveChanged() { plot.requestPaint() }
                    function onChanged() { plot.requestPaint() }
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    // Log-frequency grid + the 0 dB axis.
                    const ticks = grid.freqTicks
                    ctx.lineWidth = 1
                    for (let i = 0; i < ticks.length; ++i) {
                        const x = Math.log(ticks[i] / 20) / Math.log(1000) * width
                        ctx.beginPath()
                        if (ticks[i] === 1000) {
                            ctx.strokeStyle = Style.outline.toString()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                        } else {
                            ctx.strokeStyle = Style.gridLine.toString()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                        }
                        ctx.stroke()
                    }
                    for (let db = 24; db >= -24; db -= 12) {
                        const y = (24 - db) / 48 * height
                        ctx.beginPath()
                        ctx.strokeStyle = db === 0 ? Style.outline.toString()
                                                   : Style.gridLine.toString()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }
                    // Summed response of the four bands (EqModel impulse response).
                    const curve = eq.curve
                    if (curve.length > 1) {
                        ctx.beginPath()
                        ctx.strokeStyle = Style.accent.toString()
                        ctx.lineWidth = 2
                        for (let i = 0; i < curve.length; ++i) {
                            const x = i / (curve.length - 1) * width
                            const y = (1.0 - curve[i]) * height
                            if (i === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                    // Frequency labels along the bottom edge.
                    ctx.fillStyle = Style.textDim.toString()
                    ctx.font = "11px sans-serif"
                    for (let i = 0; i < ticks.length; ++i) {
                        const x = Math.log(ticks[i] / 20) / Math.log(1000) * width
                        const label = ticks[i] >= 1000 ? (ticks[i] / 1000) + "k"
                                                       : String(ticks[i])
                        ctx.fillText(label, Math.min(width - 22, x + 2), height - 3)
                    }
                    ctx.fillText("+24", 3, 12)
                    ctx.fillText("-24", 3, height - 14)
                }
            }

            // One draggable node per band. Positions come from the model, so a
            // touch drag and the panel EDIT path move the same numbers.
            Repeater {
                model: eq.nodes
                delegate: Rectangle {
                    readonly property int bandIndex: index
                    readonly property bool selected: modelData.selected
                    width: selected ? 30 : 24
                    height: width
                    radius: width / 2
                    x: modelData.x * grid.width - width / 2
                    y: (1.0 - modelData.y) * grid.height - height / 2
                    color: selected ? Style.nodePink : Style.panel
                    border.width: selected ? 3 : 2
                    border.color: selected ? Style.nodePink : Style.textDim
                    opacity: modelData.type === 0 ? 0.55 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: parent.bandIndex + 1
                        color: parent.selected ? Style.panelDark : Style.text
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                    }
                }
            }

            DragHandler {
                id: nodeDrag
                target: null
                property int node: -1

                function normalized(nx, ny) {
                    return [Math.max(0.0, Math.min(1.0, nx / grid.width)),
                            Math.max(0.0, Math.min(1.0, 1.0 - ny / grid.height))]
                }

                onActiveChanged: {
                    if (active) {
                        const p = normalized(centroid.position.x, centroid.position.y)
                        node = eq.nodeAt(p[0], p[1])
                        if (node >= 0) eq.selectBand(node)
                    } else {
                        node = -1
                        // The readout keeps the top band again (no finger).
                        app.touchAnchorY = -1
                    }
                }

                onCentroidChanged: {
                    if (!active || node < 0) return
                    const p = normalized(centroid.position.x, centroid.position.y)
                    // Fullscreen the grid reaches the top edge, so the readout
                    // needs the finger's window position to stay out of its way.
                    app.touchAnchorY = grid.mapToItem(null, centroid.position.x,
                                                      centroid.position.y).y
                    eq.dragBand(node, p[0], p[1])
                }
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            CornerChip {
                width: (parent.width - Style.gap * 3) / 4
                height: parent.height
                label: "GAIN"
                value: eq.cutBand ? "-- (cut)" : eq.gainText
                knobFraction: screen.gainFraction()
                focusId: "eq.band.gain"
                onMinus: eq.adjustGain(-0.5)
                onPlus: eq.adjustGain(0.5)
            }
            CornerChip {
                width: (parent.width - Style.gap * 3) / 4
                height: parent.height
                label: "Q"
                value: eq.qText
                knobFraction: screen.qFraction()
                focusId: "eq.band.q"
                onMinus: eq.adjustQ(-0.05)
                onPlus: eq.adjustQ(0.05)
            }
            CornerChip {
                width: (parent.width - Style.gap * 3) / 4
                height: parent.height
                label: "FREQ"
                value: eq.freqText.replace(" kHz", "k")
                knobFraction: screen.freqFraction()
                focusId: "eq.band.freq"
                onMinus: eq.adjustFreq(-1.0 / 12.0)
                onPlus: eq.adjustFreq(1.0 / 12.0)
            }
            CornerChip {
                width: (parent.width - Style.gap * 3) / 4
                height: parent.height
                label: "TYPE"
                value: eq.typeText
                knobFraction: eq.nodes.length > eq.band
                              ? eq.nodes[eq.band].type / 5 : 0
                focusId: "eq.band.type"
                onMinus: eq.cycleType(-1)
                onPlus: eq.cycleType(1)
                onPick: app.openConfigFrom("eqtype", this)
            }
        }
    }

    // ---- SHIFT face: the explicit band editor ------------------------------
    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: parent.width - 256 - Style.gap
                height: parent.height
                text: "SHIFT - " + eq.subjectText + " " + eq.bandText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: 58
                height: parent.height
                text: "MAIN"
                textSize: Style.fontSmall
                checked: eq.subject === eq.mainSubject
                focusId: "shift.eq.subject.main"
                onClicked: eq.selectMain()
            }
            TouchButton {
                width: 190
                height: parent.height
                text: "TRACK  " + eq.subjectText
                textSize: Style.fontSmall
                focusId: "shift.eq.subject.track"
                onClicked: app.showTextPopup("EQ SUBJECT", eq.subjectText)
            }
        }

        ConfigRow {
            width: parent.width
            height: 52
            label: "GAIN"
            value: eq.cutBand ? "-- (cut)" : eq.gainText
            knobFraction: screen.gainFraction()
            shadowFocusId: "shift.eq.band.gain"
            onMinus: eq.adjustGain(-0.5)
            onPlus: eq.adjustGain(0.5)
        }
        ConfigRow {
            width: parent.width
            height: 52
            label: "Q"
            value: eq.qText
            knobFraction: screen.qFraction()
            shadowFocusId: "shift.eq.band.q"
            onMinus: eq.adjustQ(-0.05)
            onPlus: eq.adjustQ(0.05)
        }
        ConfigRow {
            width: parent.width
            height: 52
            label: "FREQUENCY"
            value: eq.freqText
            knobFraction: screen.freqFraction()
            shadowFocusId: "shift.eq.band.freq"
            onMinus: eq.adjustFreq(-1.0 / 12.0)
            onPlus: eq.adjustFreq(1.0 / 12.0)
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "BAND TYPE   " + eq.typeText
            textSize: Style.fontSmall
            focusId: "shift.eq.band.type"
            onClicked: app.openConfigFrom("eqtype", this)
        }

        Text {
            width: parent.width
            text: "Drag a node on the grid; the panel's directions move the selected node in EDIT (SHIFT = coarse frequency / Q on a cut band). Cut filters have no gain."
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
    }

    // One shared chip: label, value, a circular value arc and -/+ steppers.
    // Tapping the TYPE chip opens the band-type ConfigPopup.
    component CornerChip: Rectangle {
        id: chip
        property string label: ""
        property string value: ""
        property real knobFraction: 0.0
        property string focusId: ""
        signal minus()
        signal plus()
        signal pick()

        color: Style.panel
        radius: Style.corner
        border.width: app.focusId === chip.focusId ? 2 : Style.outlineWidth
        border.color: app.focusId === chip.focusId ? Style.accent : Style.outline

        KnobDial {
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            fraction: chip.knobFraction
        }
        Text {
            x: 34
            y: 2
            text: chip.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Text {
            x: 34
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 34 - 96
            text: chip.value
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 6
            y: 3
            text: "..."
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            visible: chip.focusId === "eq.band.type"
        }
        // C7: 48 px per stepper, so the EQ corners meet the same target rule
        // as every other control (the value field gives up the width).
        TouchButton {
            width: 48
            height: parent.height
            x: parent.width - 96
            text: "-"
            textSize: 16
            onClicked: chip.minus()
        }
        TouchButton {
            width: 48
            height: parent.height
            x: parent.width - 48
            text: "+"
            textSize: 16
            onClicked: chip.plus()
        }
        TapHandler {
            // Only the TYPE chip is a button; the value chips are steppers.
            enabled: chip.focusId === "eq.band.type"
            onTapped: chip.pick()
        }
    }

    component ConfigRow: Row {
        id: row
        property string label: ""
        property string value: ""
        property real knobFraction: 0.0
        property string shadowFocusId: ""
        signal minus()
        signal plus()

        spacing: Style.gap
        Text {
            width: 92
            height: parent.height
            text: row.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        KnobDial {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            height: 44
            fraction: row.knobFraction
        }
        Rectangle {
            width: parent.width - 92 - 44 - 145 - Style.gap * 3
            height: 44
            anchors.verticalCenter: parent.verticalCenter
            color: Style.panelDark
            radius: Style.corner
            border.width: app.focusId === row.shadowFocusId ? 2 : Style.outlineWidth
            border.color: app.focusId === row.shadowFocusId ? Style.accent : Style.outline
            Text {
                anchors.centerIn: parent
                text: row.value
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
            }
        }
        TouchButton {
            width: 68
            height: Style.touch
            text: "-"
            textSize: 18
            onClicked: row.minus()
        }
        TouchButton {
            width: 69
            height: Style.touch
            text: "+"
            textSize: 18
            onClicked: row.plus()
        }
    }
}
