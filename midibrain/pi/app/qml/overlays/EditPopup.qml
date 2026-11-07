import QtQuick
import Mnkybox 1.0
import "../components"

// EDIT popup (docs/daw-plan.md W5): one take, one waveform (fills the height
// between the header and the controls, ~254 px at 800 x 480), an operation
// selector that shows only the selected operation's parameter, and the three
// verbs. It replaces the retired EDIT tab; every EDIT affordance opens it
// through AppController::openTakeEdit().
//
// State lives in TimelineViewModel (`timeline`): the view window, the region
// selection, the selected op and its drafts, the undo stack and the region
// edits. The popup only draws and forwards touches, so the touch path and the
// panel ring drive the same values.
//
// Layout is anchored, not hard-coded to today's chrome: header 44, waveform
// flexible (min 200), parameter row 52, operation selector 56, verbs 64.
// Stage 1 tokenizes these inline values.
Rectangle {
    id: root

    anchors.fill: parent
    visible: timeline.editOpen
    color: "#e6000000"
    z: 90

    readonly property int headerHeight: 44
    readonly property int paramHeight: 52
    readonly property int selectorHeight: 56
    readonly property int actionHeight: 64
    readonly property int rowGap: 2
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
    readonly property var opTitles: ["TRIM", "SPLIT", "GAIN", "FADE IN",
                                     "FADE OUT", "NORMALIZE", "DELETE", "UNDO"]

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

        // ---- header: title and meta; the controls sit below the tab row --
        // The tab row is the only interactive chrome the popup overlaps, so no
        // touch control lives in this 44 px band: a tap in it could also reach
        // the tab underneath (the header is text, the verbs are at the foot).
        Item {
            id: header
            x: 12
            y: 0
            width: parent.width - 24
            height: root.headerHeight

            Text {
                id: titleText
                width: parent.width - metaText.width - 8
                height: parent.height
                text: timeline.editTitle
                color: Style.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                id: metaText
                objectName: "edit.meta"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 310
                height: parent.height
                text: timeline.editMetaText
                color: Style.textDim
                font.pixelSize: 14
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // ---- waveform: flexible height, ~254 px at 800 x 480 ------------
        Item {
            id: waveHost
            x: 0
            y: header.height + root.rowGap
            width: root.width
            // The rows below are placed from the popup's bottom edge, so the
            // verb row (APPLY/UNDO/CLOSE) is on screen at every window height.
            // The old fixed 200 px floor could push it past the bottom on a
            // scaled/fullscreen window, leaving the op selector visible with
            // no apply path and no exit.
            height: Math.max(120, parameterRow.y - root.rowGap - y)
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
                        ctx.font = "13px sans-serif"
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
                    // IN/OUT handles: IN is the top tab, OUT the bottom tab, so
                    // the two 48 px grab areas can sit on the same beat without
                    // ever covering each other.
                    if (timeline.selectionActive) {
                        const xIn = xOf(timeline.inBeats)
                        const xOut = xOf(timeline.outBeats)
                        ctx.fillStyle = Style.accent
                        ctx.fillRect(xIn - 1, 0, 2, height)
                        ctx.fillRect(xOut - 1, 0, 2, height)
                        ctx.fillRect(xIn - 22, 0, 44, 22)
                        ctx.fillRect(xOut - 22, height - 22, 44, 22)
                        ctx.fillStyle = Style.panelDark
                        ctx.font = "14px sans-serif"
                        ctx.fillText("IN", xIn - 9, 16)
                        ctx.fillText("OUT", xOut - 13, height - 5)
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
                        if (app.focusId === "edit.in") {
                            ctx.strokeRect(focusX - 24, 0, 48, 48)
                        } else {
                            ctx.strokeRect(focusX - 24, height - 48, 48, 48)
                        }
                    }
                }
            }

            // Zoom: live in the waveform's top-right corner, below the tab
            // row, so a tap can never also hit the tab underneath.
            Row {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                spacing: Style.gapTight

                TouchButton {
                    width: Style.touch
                    height: Style.touch
                    text: "Z-"
                    textSize: 16
                    focusId: "edit.zoom.out"
                    onClicked: timeline.zoom(-1)
                }
                TouchButton {
                    width: Style.touch
                    height: Style.touch
                    text: "Z+"
                    textSize: 16
                    focusId: "edit.zoom.in"
                    onClicked: timeline.zoom(1)
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

            // IN/OUT handles: 48 x 48 grab tabs. IN sits on the top edge, OUT
            // on the bottom edge, so a close IN..OUT pair never stacks two
            // drag targets on one point. Each drag writes only its own boundary
            // (the model clamps at the other edge instead of moving it); the
            // panel encoder on edit.in/edit.out is the precision path.
            MouseArea {
                id: inHandle
                // The edge handles are the panel's edit.in / edit.out stops and
                // the headless tap gate's drag targets, so they carry the id.
                property string focusId: "edit.in"
                readonly property real handleWidth: 48
                readonly property real handleHeight: 48
                x: Math.max(0, Math.min(waveHost.width - handleWidth,
                                        timeline.ratioOfBeat(timeline.inBeats) * waveHost.width - handleWidth / 2))
                y: 0
                width: handleWidth
                height: handleHeight
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
                readonly property real handleWidth: 48
                readonly property real handleHeight: 48
                x: Math.max(0, Math.min(waveHost.width - handleWidth,
                                        timeline.ratioOfBeat(timeline.outBeats) * waveHost.width - handleWidth / 2))
                y: waveHost.height - handleHeight
                width: handleWidth
                height: handleHeight
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

            // Two-finger zoom of the WAVEFORM only. Qt's default pinch target
            // is the handler's parent, so without `target: null` the whole
            // popup was scaled and rotated by the gesture on top of the manual
            // zoom below (owner report: "the whole waveform widget window is
            // zoomed in and out. It also rotates."). target: null makes the
            // handler a gesture source only - the ArrangeScreen pattern - and
            // the rotation axis is pinned so no code path can rotate the
            // popup. Single-finger IN/OUT handle drags are untouched: a pinch
            // only activates with two points.
            PinchHandler {
                id: pinch
                target: null
                minimumRotation: 0
                maximumRotation: 0
                property real lastScale: 1
                onActiveChanged: if (active) lastScale = 1
                onScaleChanged: {
                    if (!active || scale <= 0) return
                    const factor = pinch.scale / pinch.lastScale
                    if (factor > 1.02 || factor < 0.98) {
                        // timeline.zoomBy multiplies the visible window, so the
                        // inverse gesture factor makes fingers-apart zoom in.
                        timeline.zoomBy(1.0 / factor)
                        pinch.lastScale = pinch.scale
                    }
                }
            }

            // SCANNING badge: the sidecar is being produced on the scanner
            // thread; the wave repaints when it lands.
            Rectangle {
                visible: timeline.editScanning
                anchors.centerIn: parent
                width: Math.max(180, scanText.implicitWidth + 24)
                height: 40
                radius: Style.corner
                color: Style.raised
                border.width: 2
                border.color: Style.accent
                Text {
                    id: scanText
                    anchors.centerIn: parent
                    text: timeline.editScanText + " - PEAKS"
                    color: Style.accent
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
        }

        // ---- parameter row: only the selected op's parameters ------------
        Rectangle {
            id: parameterRow
            x: 12
            y: selectorRow.y - root.paramHeight - root.rowGap
            width: root.width - 24
            height: root.paramHeight
            radius: Style.corner
            color: Style.panelDark
            border.width: 1
            border.color: Style.outline

            Text {
                id: rangeText
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: valueKnob.visible ? parent.width - valueKnob.width - 24
                                         : parent.width - 340
                height: parent.height
                text: timeline.editFooterText
                color: timeline.selectionActive ? Style.accent : Style.textDim
                font.pixelSize: 16
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                visible: !root.valueOp
                anchors.left: rangeText.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 8
                height: parent.height
                text: timeline.editHintText
                color: Style.textDim
                font.pixelSize: 14
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            // GAIN / FADE IN / FADE OUT: one chip for the selected op. The ring
            // already has an edit.value stop; it lands on this chip.
            KnobChip {
                id: valueKnob
                visible: root.valueOp
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 240
                height: parent.height - 4
                label: timeline.editOpName
                value: timeline.editOp === 2 ? timeline.editGainDb
                     : timeline.editOp === 3 ? timeline.editFadeInBeats
                     : timeline.editFadeOutBeats
                minimumValue: timeline.editOp === 2 ? -96 : 0
                maximumValue: 12
                stepSize: timeline.editOp === 2 ? 0.5 : 0.25
                dialScale: timeline.editOp === 2 ? 24 : 8
                suffix: timeline.editOp === 2 ? " dB" : " b"
                decimals: timeline.editOp === 2 ? 1 : 2
                popupLabel: timeline.editOpName
                popupKind: timeline.editOp === 2 ? "db" : "generic"
                focusId: "edit.value"
                onMoved: (value) => {
                    if (timeline.editOp === 2) timeline.setEditGainDb(value)
                    else if (timeline.editOp === 3) timeline.setEditFadeIn(value)
                    else if (timeline.editOp === 4) timeline.setEditFadeOut(value)
                }
            }
        }

        // ---- operation selector: one row, only the picked op's params ----
        Row {
            id: selectorRow
            x: 12
            y: actionRow.y - root.selectorHeight - root.rowGap
            width: root.width - 24
            height: root.selectorHeight
            spacing: 4

            Repeater {
                model: 8
                delegate: TouchButton {
                    width: (selectorRow.width - 4 * 7) / 8
                    height: selectorRow.height
                    text: root.opTitles[index]
                    textSize: 14
                    checked: timeline.editOp === index
                    focusId: "edit.op." + index
                    onClicked: {
                        timeline.selectEditOp(index)
                        app.showTextPopup("REGION OP", timeline.editOpName)
                    }
                }
            }
        }

        // ---- verbs: exact operation, UNDO, RENDER REGION -----------------
        Row {
            id: actionRow
            x: 12
            // Anchored to the bottom: the verbs are the exit and the apply path,
            // so no waveform/geometry change can scroll them off the screen.
            y: root.height - root.actionHeight - root.rowGap
            width: root.width - 24
            height: root.actionHeight
            spacing: 8

            TouchButton {
                id: applyButton
                width: parent.width - undoButton.width - renderButton.width -
                       closeButton.width - lastOpText.width - Style.gap * 4
                height: parent.height
                text: root.applyVerb
                textSize: 18
                variant: "primary"
                focusId: "edit.apply"
                onClicked: root.runOp()
            }
            TouchButton {
                id: undoButton
                width: 96
                height: parent.height
                text: "UNDO"
                textSize: 16
                variant: "ghost"
                active: timeline.canUndo
                focusId: "edit.undo"
                onClicked: root.undo()
            }
            TouchButton {
                id: renderButton
                width: 150
                height: parent.height
                text: "RENDER REGION"
                textSize: 14
                focusId: "edit.render"
                onClicked: app.renderRegionToTrack()
            }
            TouchButton {
                id: closeButton
                width: 92
                height: parent.height
                text: "CLOSE"
                textSize: 16
                variant: "ghost"
                focusId: "edit.close"
                onClicked: app.closeTakeEdit()
            }
            Text {
                id: lastOpText
                width: 150
                height: parent.height
                text: timeline.lastOpText
                color: Style.textDim
                font.pixelSize: 14
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    // Runs the selected op (or UNDO, which is the selector's last entry) and
    // names the outcome in the notice slot; a refusal keeps its reason on
    // screen.
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
}
