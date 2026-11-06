import QtQuick
import Mnkybox 1.0
import "../components"

// EDIT popup (docs/daw-plan.md W5): one take, one 800 x 260 waveform, the ops
// strip and the two verbs. It replaces the retired EDIT tab; every EDIT
// affordance opens it through AppController::openTakeEdit().
//
// State lives in TimelineViewModel (`timeline`): the view window, the region
// selection, the selected op and its drafts, the undo stack and the region
// edits. The popup only draws and forwards touches, so the touch path and the
// panel ring drive the same values.
//
// Layout is fixed: header 44, waveform exactly 800 x 260 (the owner's "the
// waveform display is really small" fix), region footer 14, ops strip 64,
// verbs 44, hint 14.
Rectangle {
    id: root

    anchors.fill: parent
    visible: timeline.editOpen
    color: "#e6000000"
    z: 90

    readonly property int waveHeight: 260
    readonly property real waveBeats: Math.max(0.0001, timeline.viewBeats)
    readonly property real regionLength: Math.max(0.0001, timeline.previewRegionLengthBeats)
    readonly property bool valueOp: timeline.editOp === 2 || timeline.editOp === 3 ||
                                    timeline.editOp === 4
    readonly property bool needsRange: timeline.editOp === 0 || timeline.editOp === 6
    readonly property bool previewShape: !root.needsRange && root.valueOp
    // C9: the button states its verb and the header repeats it; UNDO is its own
    // verb, so it never reads "APPLY UNDO".
    readonly property string applyVerb: timeline.editOpName === "UNDO"
                                        ? "UNDO"
                                        : "APPLY " + timeline.editOpName

    // A touch outside the panel never dismisses: the popup is the workspace, not
    // a menu, and an accidental miss must not throw away a draft.
    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        anchors.fill: parent
        color: Style.raised
        border.width: 2
        border.color: Style.accent

        // ---- header -----------------------------------------------------
        Item {
            x: 12
            y: 4
            width: parent.width - 24
            height: 40

            Text {
                width: parent.width - 132 - 198
                height: parent.height
                text: timeline.editTitle
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                anchors.right: closeButton.left
                anchors.rightMargin: 8
                width: 190
                visible: text !== ""
                height: parent.height
                text: timeline.editMetaText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                id: closeButton
                anchors.right: parent.right
                anchors.top: parent.top
                width: 88
                height: parent.height
                text: "CLOSE"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: "edit.close"
                onClicked: app.closeTakeEdit()
            }
        }

        // ---- waveform 800 x 260 -----------------------------------------
        Item {
            id: waveHost
            x: 0
            y: 48
            width: root.width
            height: root.waveHeight
            clip: true

            Rectangle {
                anchors.fill: parent
                color: Style.panelDark
            }

            // Bar/beat grid of the current zoom window.
            Canvas {
                id: grid
                anchors.fill: parent
                Connections {
                    target: timeline
                    function onViewChanged() { grid.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!(timeline.viewBeats > 0)) return
                    const xOf = function(beat) {
                        return (beat - timeline.viewStartBeats) /
                               timeline.viewBeats * width
                    }
                    const firstBar = Math.floor(timeline.viewStartBeats / 4) * 4
                    ctx.strokeStyle = Style.gridLine
                    ctx.lineWidth = 1
                    for (let beat = firstBar;
                         beat <= timeline.viewStartBeats + timeline.viewBeats + 4;
                         beat += 1) {
                        if (beat < 0) continue
                        const x = xOf(beat)
                        if (x < 0 || x > width) continue
                        const bar = Math.abs(beat % 4) < 1e-9
                        ctx.globalAlpha = bar ? 0.9 : 0.35
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1.0
                }
            }

            TimelineWave {
                anchors.fill: parent
                peaks: timeline.editWave
                placeholderText: timeline.editScanning ? "" : "NO PEAKS YET"
                playheadRatio: {
                    if (!(timeline.viewBeats > 0)) return -1
                    return (timeline.editPlayheadBeats - timeline.viewStartBeats) /
                           timeline.viewBeats
                }
                showEnvelope: root.previewShape
                gainDb: timeline.editGainDb
                fadeInRatio: timeline.editFadeInBeats / root.regionLength
                fadeOutRatio: timeline.editFadeOutBeats / root.regionLength
                showGhostEnvelope: root.previewShape && timeline.previewRegionFound
                ghostGainDb: timeline.previewRegionGainDb
                ghostFadeInRatio: timeline.previewRegionFadeInBeats / root.regionLength
                ghostFadeOutRatio: timeline.previewRegionFadeOutBeats / root.regionLength
                envelopeStartRatio: timeline.ratioOfBeat(timeline.previewRegionStartBeats)
                envelopeSpanRatio: timeline.previewRegionLengthBeats / root.waveBeats
            }

            // Region, handles, loop/punch guides, the op's own marker.
            Canvas {
                id: overlay
                anchors.fill: parent
                Connections {
                    target: timeline
                    function onSelectionChanged() { overlay.requestPaint() }
                    function onCursorChanged() { overlay.requestPaint() }
                    function onViewChanged() { overlay.requestPaint() }
                    function onPlayheadChanged() { overlay.requestPaint() }
                    function onEditOpChanged() { overlay.requestPaint() }
                    function onLoopChanged() { overlay.requestPaint() }
                    function onPreviewChanged() { overlay.requestPaint() }
                }
                Connections {
                    target: app
                    function onFocusChanged() { overlay.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!(timeline.viewBeats > 0)) return
                    const xOf = function(beat) {
                        return (beat - timeline.viewStartBeats) /
                               timeline.viewBeats * width
                    }
                    // Loop brace (session takes.loop) - a guide, not editable
                    // here.
                    if (timeline.loopEnabled) {
                        const start = timeline.loopEndBeats > timeline.loopStartBeats
                                      ? timeline.loopStartBeats : 0
                        const end = timeline.loopEndBeats > timeline.loopStartBeats
                                    ? timeline.loopEndBeats
                                    : timeline.takeBeats
                        const x0 = xOf(start)
                        const x1 = xOf(end)
                        ctx.strokeStyle = Style.ok
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(x0, 2)
                        ctx.lineTo(x1, 2)
                        ctx.moveTo(x0, 2)
                        ctx.lineTo(x0, 9)
                        ctx.moveTo(x1, 2)
                        ctx.lineTo(x1, 9)
                        ctx.stroke()
                    }
                    // Punch points (session punch) - guides as well.
                    const punches = timeline.punchMarkers
                    for (let i = 0; i < punches.length; ++i) {
                        const marker = punches[i]
                        if (!marker.enabled) continue
                        const x = xOf(marker.beat)
                        if (x < -2 || x > width + 2) continue
                        ctx.strokeStyle = marker.label === "IN" ? Style.record : Style.accent
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(x, 10)
                        ctx.lineTo(x, height - 10)
                        ctx.stroke()
                        ctx.fillStyle = marker.label === "IN" ? Style.record : Style.accent
                        ctx.font = "9px sans-serif"
                        ctx.fillText(marker.label, x + 2, 18)
                    }
                    // Region selection and the op preview shading.
                    if (timeline.selectionActive) {
                        const x0 = xOf(timeline.inBeats)
                        const x1 = xOf(timeline.outBeats)
                        ctx.fillStyle = Qt.rgba(0.31, 0.76, 0.97, 0.16)
                        ctx.fillRect(x0, 0, Math.max(1, x1 - x0), height)
                        if (timeline.editOp === 0) {
                            // TRIM keeps this range: dim what it removes.
                            ctx.fillStyle = Qt.rgba(0.02, 0.03, 0.04, 0.55)
                            ctx.fillRect(0, 0, Math.max(0, x0), height)
                            ctx.fillRect(x1, 0, Math.max(0, width - x1), height)
                        } else if (timeline.editOp === 6) {
                            // DELETE REGION marks what disappears.
                            ctx.fillStyle = Qt.rgba(0.95, 0.42, 0.42, 0.20)
                            ctx.fillRect(x0, 0, Math.max(1, x1 - x0), height)
                        }
                        ctx.strokeStyle = Style.accent
                        ctx.lineWidth = 2
                        ctx.strokeRect(x0, 1, Math.max(1, x1 - x0), height - 2)
                    }
                    // SPLIT cuts at the edit cursor.
                    const cursorX = xOf(timeline.cursorBeats)
                    if (timeline.editOp === 1 && cursorX >= 0 && cursorX <= width) {
                        ctx.strokeStyle = Style.record
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(cursorX, 0)
                        ctx.lineTo(cursorX, height)
                        ctx.stroke()
                    }
                    // IN/OUT handles: the grab tabs are what the drag targets
                    // cover, so the touch target is visible.
                    if (timeline.selectionActive) {
                        const xIn = xOf(timeline.inBeats)
                        const xOut = xOf(timeline.outBeats)
                        ctx.fillStyle = Style.accent
                        ctx.fillRect(xIn - 1, 0, 2, height)
                        ctx.fillRect(xOut - 1, 0, 2, height)
                        ctx.fillRect(xIn - 6, 0, 12, 10)
                        ctx.fillRect(xOut - 6, 0, 12, 10)
                        ctx.fillStyle = Style.panelDark
                        ctx.font = "9px sans-serif"
                        ctx.fillText("IN", xIn - 7, 8)
                        ctx.fillText("OUT", xOut - 9, 8)
                    }
                    // Playhead.
                    const playX = xOf(timeline.editPlayheadBeats)
                    if (playX >= 0 && playX <= width) {
                        ctx.strokeStyle = Style.accent
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(playX, 0)
                        ctx.lineTo(playX, height)
                        ctx.stroke()
                    }
                    // Panel focus ring on the two edge handles.
                    if (app.focusId === "edit.in" || app.focusId === "edit.out") {
                        const focusX = xOf(app.focusId === "edit.in" ? timeline.inBeats
                                                                     : timeline.outBeats)
                        ctx.strokeStyle = Style.accent
                        ctx.lineWidth = 2
                        ctx.strokeRect(focusX - 22, 0, 44, height)
                    }
                }
            }

            // Touch on the waveform: drag selects IN..OUT, a tap seeks (Bluebox
            // rule: the touched point becomes the playhead).
            MouseArea {
                anchors.fill: parent
                property bool dragged: false
                property real pressX: 0
                onPressed: (mouse) => {
                    dragged = false
                    pressX = mouse.x
                    if (timeline.editWave.length === 0) {
                        // No sidecar yet: a touch only names the state.
                        app.showTextPopup("EDIT", timeline.editScanText)
                    }
                }
                onPositionChanged: (mouse) => {
                    if (!pressed) return
                    if (!dragged && Math.abs(mouse.x - pressX) < 6) return
                    if (!dragged) {
                        dragged = true
                        timeline.beginSelection(timeline.selectedTrack,
                                                timeline.beatAtRatio(pressX / width))
                    }
                    timeline.updateSelection(timeline.beatAtRatio(mouse.x / width))
                    app.showValuePopup("REGION OUT", "generic", timeline.outBeats)
                }
                onReleased: (mouse) => {
                    if (dragged) {
                        timeline.endSelection()
                        app.hideValuePopup()
                    } else {
                        timeline.seekTo(timeline.beatAtRatio(mouse.x / width))
                        app.showTextPopup("PLAYHEAD",
                                          timeline.beatTextOf(timeline.editPlayheadBeats))
                    }
                    dragged = false
                }
            }

            // IN/OUT handles: 44 px drag targets (the visible tab is 12 px).
            MouseArea {
                id: inHandle
                // The edge handles are the panel's edit.in / edit.out stops and
                // the headless tap gate's drag targets, so they carry the id.
                property string focusId: "edit.in"
                x: Math.max(0, Math.min(waveHost.width - width,
                                        timeline.ratioOfBeat(timeline.inBeats) * waveHost.width - width / 2))
                width: Style.touch
                height: waveHost.height
                enabled: timeline.selectionActive
                property bool dragging: false
                onPressed: dragging = true
                onPositionChanged: (mouse) => {
                    if (!dragging) return
                    const point = mapToItem(waveHost, mouse.x, mouse.y)
                    const beat = timeline.beatAtRatio(point.x / waveHost.width)
                    app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                    app.showValuePopup("REGION IN", "generic", timeline.setInBeats(beat))
                }
                onReleased: {
                    dragging = false
                    app.touchAnchorY = -1
                    app.hideValuePopup()
                }
                onCanceled: {
                    dragging = false
                    app.touchAnchorY = -1
                }
            }
            MouseArea {
                id: outHandle
                property string focusId: "edit.out"
                x: Math.max(0, Math.min(waveHost.width - width,
                                        timeline.ratioOfBeat(timeline.outBeats) * waveHost.width - width / 2))
                width: Style.touch
                height: waveHost.height
                enabled: timeline.selectionActive
                property bool dragging: false
                onPressed: dragging = true
                onPositionChanged: (mouse) => {
                    if (!dragging) return
                    const point = mapToItem(waveHost, mouse.x, mouse.y)
                    const beat = timeline.beatAtRatio(point.x / waveHost.width)
                    app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                    app.showValuePopup("REGION OUT", "generic", timeline.setOutBeats(beat))
                }
                onReleased: {
                    dragging = false
                    app.touchAnchorY = -1
                    app.hideValuePopup()
                }
                onCanceled: {
                    dragging = false
                    app.touchAnchorY = -1
                }
            }

            // Zoom: buttons in the waveform's top corners, plus pinch.
            Row {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                spacing: Style.gapTight

                TouchButton {
                    width: Style.touch
                    height: Style.touch
                    text: "Z-"
                    textSize: Style.fontSmall
                    focusId: "edit.zoom.out"
                    onClicked: timeline.zoom(-1)
                }
                TouchButton {
                    width: Style.touch
                    height: Style.touch
                    text: "Z+"
                    textSize: Style.fontSmall
                    focusId: "edit.zoom.in"
                    onClicked: timeline.zoom(1)
                }
            }

            PinchHandler {
                id: pinch
                property real lastScale: 1
                onActiveChanged: if (active) lastScale = 1
                onScaleChanged: {
                    if (!active || scale <= 0) return
                    const delta = lastScale / scale
                    lastScale = scale
                    timeline.zoomBy(delta)
                }
            }

            // SCANNING badge: the sidecar is being produced on the scanner
            // thread; the wave repaints when it lands.
            Rectangle {
                visible: timeline.editScanning
                anchors.centerIn: parent
                width: Math.max(160, scanText.implicitWidth + 24)
                height: 32
                radius: Style.corner
                color: Style.raised
                border.width: 2
                border.color: Style.accent
                Text {
                    id: scanText
                    anchors.centerIn: parent
                    text: timeline.editScanText + " - PEAKS"
                    color: Style.accent
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                }
            }
        }

        // ---- region footer ----------------------------------------------
        Text {
            x: 12
            y: 312
            width: parent.width - 24
            height: 14
            text: timeline.editFooterText
            color: timeline.selectionActive ? Style.accent : Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // ---- ops strip ---------------------------------------------------
        Row {
            x: 12
            y: 330
            width: parent.width - 24
            height: 64
            spacing: Style.gapTight

            Repeater {
                model: 8
                delegate: EditOpCard {
                    width: (parent.width - Style.gapTight * 7) / 8
                    height: parent.height
                    op: index
                }
            }
        }

        // ---- verbs -------------------------------------------------------
        Row {
            x: 12
            y: 400
            width: parent.width - 24
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: 236
                height: parent.height
                text: root.applyVerb
                textSize: Style.fontSmall
                variant: "primary"
                focusId: "edit.apply"
                onClicked: root.runOp()
            }
            TouchButton {
                width: 96
                height: parent.height
                text: "UNDO"
                textSize: Style.fontSmall
                variant: "ghost"
                active: timeline.canUndo
                focusId: "edit.undo"
                onClicked: root.undo()
            }
            TouchButton {
                width: 250
                height: parent.height
                text: "RENDER REGION TO TRACK"
                textSize: Style.fontSmall
                focusId: "edit.render"
                onClicked: app.renderRegionToTrack()
            }
            Text {
                width: parent.width - 236 - 96 - 250 - Style.gap * 3
                height: parent.height
                text: timeline.lastOpText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // ---- hint --------------------------------------------------------
        Text {
            x: 12
            y: 450
            width: parent.width - 24
            height: 14
            text: timeline.editHintText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }
    }

    // Runs the selected op (or UNDO, which is the strip's last card) and names
    // the outcome in the notice slot; a refusal keeps its reason on screen.
    function runOp() {
        const error = timeline.applyEditOp()
        if (error === "") {
            app.announce("info", "APPLIED " + timeline.editOpName + " - " +
                                 timeline.regionCount() + " REGION(S)")
        } else {
            app.announce("warn", error)
            app.showTextPopup("NOT APPLIED", error)
        }
    }

    function undo() {
        if (timeline.undo()) {
            app.announce("info", "EDIT UNDO - " + timeline.regionCount() + " REGION(S)")
        } else {
            app.announce("warn", "EDIT UNDO - nothing to undo")
        }
    }

    // One card of the ops strip. A value card carries the draft knob (arc +
    // value, per the slider law: no slider in a tight cell); a verb card only
    // selects its op. UNDO runs immediately.
    component EditOpCard: Rectangle {
        id: card

        property int op: 0

        readonly property bool isUndo: card.op === 7
        readonly property bool isValueOp: card.op === 2 || card.op === 3 || card.op === 4
        readonly property bool selected: timeline.editOp === card.op
        readonly property bool focused: app.focusId === card.focusId
        readonly property string focusId: "edit.op." + card.op
        readonly property string title: ["TRIM", "SPLIT", "GAIN", "FADE IN",
                                         "FADE OUT", "NORMALIZE", "DELETE REGION",
                                         "UNDO"][card.op]
        readonly property real fraction: {
            if (card.op === 2) return Math.max(0.0, Math.min(1.0,
                                              (timeline.editGainDb + 24) / 36))
            if (card.op === 3) return Math.max(0.0, Math.min(1.0,
                                              timeline.editFadeInBeats / 4))
            if (card.op === 4) return Math.max(0.0, Math.min(1.0,
                                              timeline.editFadeOutBeats / 4))
            return card.selected ? 1.0 : 0.0
        }
        readonly property string valueText: {
            if (card.op === 0) return timeline.selectionActive
                                     ? timeline.regionCount() + " REG" : "NO IN/OUT"
            if (card.op === 1) return "AT " + timeline.beatTextOf(timeline.cursorBeats)
            if (card.op === 2) return timeline.editGainDb.toFixed(1) + " dB"
            if (card.op === 3) return timeline.editFadeInBeats.toFixed(2) + " b"
            if (card.op === 4) return timeline.editFadeOutBeats.toFixed(2) + " b"
            if (card.op === 5) return "-1 dBFS"
            if (card.op === 6) return timeline.selectionActive ? "IN..OUT" : "NO IN/OUT"
            return timeline.undoText
        }

        color: card.selected ? Style.raised : Style.panelDark
        radius: Style.corner
        border.width: card.focused || card.selected ? 2 : Style.outlineWidth
        border.color: card.focused || card.selected ? Style.accent : Style.outline

        Column {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 0

            Text {
                width: parent.width
                height: 15
                text: card.title
                color: card.selected ? Style.accent : Style.text
                // DELETE REGION is the one long label; 9 px keeps it whole in a
                // 92 px card (ASCII, single line, no elide).
                font.pixelSize: card.title.length > 11 ? 9 : Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            KnobDial {
                width: 22
                height: 22
                anchors.horizontalCenter: parent.horizontalCenter
                fraction: card.fraction
                dimmed: !card.selected
            }
            Text {
                width: parent.width
                height: parent.height - 15 - 22
                text: card.valueText
                color: Style.textDim
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        MouseArea {
            anchors.fill: parent
            property real pressY: 0
            property bool dragged: false
            property real lastY: 0
            onPressed: (mouse) => {
                pressY = mouse.y
                lastY = mouse.y
                dragged = false
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                if (!dragged && Math.abs(mouse.y - pressY) < 5) return
                dragged = true
                timeline.selectEditOp(card.op)
                const delta = (lastY - mouse.y) / 12
                lastY = mouse.y
                app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
                if (card.op === 2) {
                    timeline.setEditGainDb(timeline.editGainDb + delta * 0.5)
                    app.showValuePopup("REGION GAIN", "db", timeline.editGainDb)
                } else if (card.op === 3) {
                    timeline.setEditFadeIn(timeline.editFadeInBeats + delta * 0.25)
                    app.showValuePopup("FADE IN", "generic", timeline.editFadeInBeats)
                } else if (card.op === 4) {
                    timeline.setEditFadeOut(timeline.editFadeOutBeats + delta * 0.25)
                    app.showValuePopup("FADE OUT", "generic", timeline.editFadeOutBeats)
                }
            }
            onReleased: {
                app.touchAnchorY = -1
                if (!dragged) {
                    if (card.isUndo) root.undo()
                    else {
                        timeline.selectEditOp(card.op)
                        app.showTextPopup("REGION OP", timeline.editOpName)
                    }
                }
                dragged = false
            }
            onCanceled: {
                app.touchAnchorY = -1
                dragged = false
            }
        }
    }
}
