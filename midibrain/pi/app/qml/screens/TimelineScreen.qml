import QtQuick
import Mnkybox 1.0
import "../components"

// EDIT timeline (10th tab): six tracks per page, time left to right, waveform
// thumbnails from the `.peaks` sidecars, the session loop brace and the punch
// markers (display only). Touch-drag sets a region; the panel cursor path is
// timeline.cursor / timeline.in / timeline.out and OPS opens the shared
// ConfigPopup with the waveform preview.
Item {
    id: screen

    readonly property int laneHeight: 44
    readonly property int labelWidth: 62

    Connections {
        target: app
        // C4: the lane and cursor focus stops are visible now, so a NAV step
        // never lands on an invisible control.
        function onFocusChanged() { overlay.requestPaint() }
    }

    Connections {
        target: timeline
        function onLanesChanged() { ruler.requestPaint(); overlay.requestPaint() }
        function onSelectionChanged() { overlay.requestPaint() }
        function onCursorChanged() { overlay.requestPaint() }
        function onLoopChanged() { overlay.requestPaint() }
        function onViewChanged() { ruler.requestPaint(); overlay.requestPaint() }
        function onPlayheadChanged() { overlay.requestPaint() }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Style.marginCompact
        spacing: Style.gapHair

        Row {
            width: parent.width
            height: 20
            spacing: Style.gap

            Text {
                width: parent.width * 0.34
                height: parent.height
                text: timeline.selectedTrackName
                color: Style.text
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width * 0.36
                height: parent.height
                text: timeline.selectionText
                color: timeline.selectionActive ? Style.accent : Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width - parent.width * 0.7 - 12
                height: parent.height
                text: timeline.opsText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapHair

            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "P<"
                textSize: Style.fontSmall
                focusId: "timeline.page.prev"
                onClicked: timeline.prevPage()
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "P>"
                textSize: Style.fontSmall
                focusId: "timeline.page.next"
                onClicked: timeline.nextPage()
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "V<"
                textSize: Style.fontSmall
                focusId: "timeline.view.prev"
                onClicked: timeline.scrollView(-1)
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "V>"
                textSize: Style.fontSmall
                focusId: "timeline.view.next"
                onClicked: timeline.scrollView(1)
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "Z-"
                textSize: Style.fontSmall
                focusId: "timeline.zoom.out"
                onClicked: timeline.zoom(-1)
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "Z+"
                textSize: Style.fontSmall
                focusId: "timeline.zoom.in"
                onClicked: timeline.zoom(1)
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "IN"
                textSize: Style.fontSmall
                checked: timeline.selectionActive
                focusId: "timeline.in"
                onClicked: {
                    timeline.setInAtCursor()
                    app.showTextPopup("REGION IN", timeline.beatTextOf(timeline.inBeats))
                }
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "OUT"
                textSize: Style.fontSmall
                checked: timeline.selectionActive
                focusId: "timeline.out"
                onClicked: {
                    timeline.setOutAtCursor()
                    app.showTextPopup("REGION OUT", timeline.beatTextOf(timeline.outBeats))
                }
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "OPS"
                textSize: Style.fontSmall
                focusId: "timeline.ops"
                onClicked: app.openConfig("timelineops")
            }
            TouchButton {
                width: (parent.width - 27) / 10
                height: parent.height
                text: "UNDO"
                textSize: Style.fontSmall
                variant: "ghost"
                active: timeline.canUndo
                focusId: "timeline.undo"
                onClicked: {
                    if (timeline.undo()) app.showTextPopup("EDIT UNDO", timeline.undoText)
                    else app.showTextPopup("EDIT UNDO", "nothing to undo")
                }
            }
        }

        Canvas {
            id: ruler
            width: parent.width
            height: 16
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.fillStyle = Style.panelDark
                ctx.fillRect(0, 0, width, height)
                if (timeline.viewBeats <= 0) return
                const firstBar = Math.floor(timeline.viewStartBeats / 4) * 4
                ctx.strokeStyle = Style.outline
                ctx.fillStyle = Style.textDim
                ctx.font = "9px sans-serif"
                for (let beat = firstBar; beat <= timeline.viewStartBeats + timeline.viewBeats; beat += 4) {
                    const x = (beat - timeline.viewStartBeats) / timeline.viewBeats * width
                    if (x < -1 || x > width + 1) continue
                    ctx.beginPath()
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x, height)
                    ctx.stroke()
                    ctx.fillText(String(beat / 4 + 1), x + 2, 10)
                }
                const punches = timeline.punchMarkers
                for (let i = 0; i < punches.length; ++i) {
                    const marker = punches[i]
                    if (!marker.enabled) continue
                    const x = (marker.beat - timeline.viewStartBeats) / timeline.viewBeats * width
                    if (x < -1 || x > width + 1) continue
                    ctx.fillStyle = marker.label === "IN" ? Style.record : Style.accent
                    ctx.fillRect(x - 1, 0, 3, height)
                    ctx.fillText(marker.label, x + 2, height - 2)
                }
            }
        }

        Item {
            id: laneArea
            width: parent.width
            height: parent.height - 20 - Style.touch - 16 - 14 - Style.gapHair * 4

            Column {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: timeline.lanes
                    delegate: Rectangle {
                        width: laneArea.width
                        height: screen.laneHeight
                        color: modelData.selected ? Style.panel : Style.panelDark
                        radius: Style.corner
                        border.width: modelData.selected ? 2 : Style.outlineWidth
                        border.color: modelData.selected ? Style.accent : Style.outline
                        clip: true

                        // D9: the lane label carries a TAKE chip when the lane
                        // plays a take, so the label answers "active or live
                        // input?" without opening the chooser.
                        Column {
                            x: 0
                            width: screen.labelWidth
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0

                            Text {
                                width: screen.labelWidth
                                text: modelData.label
                                color: modelData.selected ? Style.accent : Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                width: screen.labelWidth
                                text: modelData.name
                                color: modelData.selected ? Style.accent : Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: modelData.hasTake
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 30
                                height: 11
                                radius: 2
                                color: Style.panelDark
                                border.width: 1
                                border.color: Style.accent
                                Text {
                                    anchors.centerIn: parent
                                    text: "TAKE"
                                    color: Style.accent
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                }
                            }
                        }

                        Item {
                            id: waveArea
                            x: screen.labelWidth
                            width: parent.width - screen.labelWidth
                            height: parent.height
                            clip: true

                            TimelineWave {
                                anchors.fill: parent
                                anchors.margins: 1
                                values: modelData.wave
                                // D9: the empty lane names the action, not
                                // just the state.
                                placeholderText: modelData.hasTake
                                                 ? "NO PEAKS"
                                                 : "NO ACTIVE TAKE - tap the lane label to pick a take"
                                waveColor: modelData.selected ? Style.accent : Style.textDim
                                playheadRatio: timeline.viewBeats > 0
                                               ? (timeline.playheadBeats - timeline.viewStartBeats) /
                                                 timeline.viewBeats : -1
                            }

                            Repeater {
                                model: modelData.regions
                                delegate: Rectangle {
                                    readonly property real startBeat: Number(modelData.start)
                                    readonly property real lengthBeats: Number(modelData.length)
                                    x: (startBeat - timeline.viewStartBeats) /
                                       timeline.viewBeats * waveArea.width
                                    width: Math.max(2, lengthBeats / timeline.viewBeats * waveArea.width)
                                    y: 2
                                    height: parent.height - 4
                                    visible: x + width > 0 && x < waveArea.width
                                    color: Qt.rgba(0.31, 0.76, 0.97, 0.14)
                                    border.width: 1
                                    border.color: modelData.normalize ? Style.accent : Style.accent
                                }
                            }
                        }
                    }
                }
            }

            Canvas {
                id: overlay
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (timeline.viewBeats <= 0) return
                    const xOf = function(beat) {
                        return screen.labelWidth +
                               (beat - timeline.viewStartBeats) / timeline.viewBeats *
                               (width - screen.labelWidth)
                    }
                    const selectedLane = timeline.selectedTrack - timeline.page * timeline.tracksPerPage
                    const laneTop = selectedLane * screen.laneHeight
                    const laneBottom = laneTop + screen.laneHeight
                    // Loop brace for the selected track (takes.loop, display only).
                    if (timeline.loopEnabled && selectedLane >= 0 &&
                            selectedLane < timeline.tracksPerPage) {
                        const start = timeline.loopEndBeats > timeline.loopStartBeats
                                      ? timeline.loopStartBeats : 0
                        const end = timeline.loopEndBeats > timeline.loopStartBeats
                                    ? timeline.loopEndBeats : timeline.totalBeats
                        const x0 = Math.max(screen.labelWidth, xOf(start))
                        const x1 = Math.min(width, xOf(end))
                        if (x1 > x0) {
                            ctx.strokeStyle = Style.ok
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            ctx.moveTo(x0, laneTop + 2)
                            ctx.lineTo(x1, laneTop + 2)
                            ctx.moveTo(x0, laneTop + 2)
                            ctx.lineTo(x0, laneTop + 9)
                            ctx.moveTo(x1, laneTop + 2)
                            ctx.lineTo(x1, laneTop + 9)
                            ctx.stroke()
                        }
                    }
                    // Punch windows (session punch markers; not editable here).
                    const punches = timeline.punchMarkers
                    let punchIn = null
                    let punchOut = null
                    for (let i = 0; i < punches.length; ++i) {
                        const marker = punches[i]
                        if (!marker.enabled) continue
                        const x = xOf(marker.beat)
                        ctx.strokeStyle = marker.label === "IN" ? Style.record : Style.accent
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                        if (marker.label === "IN") punchIn = x
                        if (marker.label === "OUT") punchOut = x
                    }
                    if (punchIn !== null && punchOut !== null && punchOut > punchIn) {
                        ctx.fillStyle = Qt.rgba(0.94, 0.33, 0.31, 0.08)
                        ctx.fillRect(punchIn, 0, punchOut - punchIn, height)
                    }
                    // Region selection on the selected lane.
                    if (timeline.selectionActive && selectedLane >= 0 &&
                            selectedLane < timeline.tracksPerPage) {
                        const x0 = Math.max(screen.labelWidth, xOf(timeline.inBeats))
                        const x1 = Math.min(width, xOf(timeline.outBeats))
                        if (x1 >= x0) {
                            ctx.fillStyle = Qt.rgba(0.31, 0.76, 0.97, 0.20)
                            ctx.fillRect(x0, laneTop, Math.max(1, x1 - x0),
                                         screen.laneHeight)
                            ctx.strokeStyle = Style.accent
                            ctx.lineWidth = 2
                            ctx.strokeRect(x0, laneTop, Math.max(1, x1 - x0),
                                           screen.laneHeight)
                        }
                    }
                    // C4: the panel ring on a lane is a 2 px accent border
                    // around the whole lane; the cursor stop is a dashed box
                    // around the cursor column.
                    if (app.focusId.indexOf("timeline.lane.") === 0) {
                        const focusTrack = Number(app.focusId.substring(14))
                        const focusLane = focusTrack - timeline.page * timeline.tracksPerPage
                        if (focusLane >= 0 && focusLane < timeline.tracksPerPage) {
                            ctx.strokeStyle = Style.accent
                            ctx.lineWidth = 2
                            ctx.strokeRect(1, focusLane * screen.laneHeight + 1,
                                           width - 2, screen.laneHeight - 2)
                        }
                    }
                    if (app.focusId === "timeline.cursor") {
                        const boxX = xOf(timeline.cursorBeats)
                        ctx.strokeStyle = Style.accent
                        ctx.lineWidth = 2
                        for (let y = 0; y < height; y += 8) {
                            ctx.beginPath()
                            ctx.moveTo(boxX - 3, y)
                            ctx.lineTo(boxX - 3, y + 4)
                            ctx.moveTo(boxX + 3, y)
                            ctx.lineTo(boxX + 3, y + 4)
                            ctx.stroke()
                        }
                    }
                    // Panel cursor: a dashed tick column.
                    const cursorX = xOf(timeline.cursorBeats)
                    if (cursorX >= screen.labelWidth && cursorX <= width) {
                        ctx.strokeStyle = Style.text
                        ctx.lineWidth = 1
                        for (let y = 0; y < height; y += 8) {
                            ctx.beginPath()
                            ctx.moveTo(cursorX, y)
                            ctx.lineTo(cursorX, y + 4)
                            ctx.stroke()
                        }
                    }
                    // Playhead.
                    const playX = xOf(timeline.playheadBeats)
                    if (playX >= screen.labelWidth && playX <= width) {
                        // Playhead shade: the part of the take still ahead of
                        // the transport reads dimmer than the played part.
                        ctx.fillStyle = Qt.rgba(0.02, 0.03, 0.04, 0.35)
                        ctx.fillRect(playX, 0, Math.max(0, width - playX), height)
                        ctx.strokeStyle = Style.accent
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(playX, 0)
                        ctx.lineTo(playX, height)
                        ctx.stroke()
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => {
                    const track = timeline.page * timeline.tracksPerPage +
                                  Math.floor(mouse.y / screen.laneHeight)
                    const beat = timeline.viewStartBeats +
                                 Math.max(0, mouse.x - screen.labelWidth) /
                                 Math.max(1, width - screen.labelWidth) * timeline.viewBeats
                    timeline.beginSelection(Math.min(17, Math.max(0, track)), beat)
                }
                onPositionChanged: (mouse) => {
                    if (!pressed) return
                    const beat = timeline.viewStartBeats +
                                 Math.max(0, mouse.x - screen.labelWidth) /
                                 Math.max(1, width - screen.labelWidth) * timeline.viewBeats
                    timeline.updateSelection(beat)
                }
                onReleased: timeline.endSelection()
            }

            // A10: the lane label is its own 44 px target above the drag area.
            // Tapping it opens "TAKE FOR TRACK nn"; the drag area keeps the
            // whole waveform width.
            Column {
                width: screen.labelWidth
                anchors.left: parent.left
                anchors.top: parent.top
                spacing: 0

                Repeater {
                    model: timeline.lanes
                    delegate: Item {
                        width: screen.labelWidth
                        height: screen.laneHeight
                        TapHandler {
                            onTapped: app.openTakeSheet(modelData.track)
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width
            height: 14
            text: "DRAG A REGION  -  IN/OUT SET THE EDGE AT THE CURSOR  -  TAP A LANE LABEL TO PICK ITS TAKE"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }
    }

    // A10: TAKE FOR TRACK nn sheet. The lane label opens it; a row makes that
    // take ACTIVE (the waveform repaints from its .peaks sidecar), and LIVE
    // INPUT clears the pointer.
    Rectangle {
        id: takeSheet

        visible: app.takeSheetOpen
        anchors.fill: parent
        color: "#cc000000"
        z: 60

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Rectangle {
            width: Math.min(560, parent.width - 24)
            height: Math.min(parent.height - 8, 24 + 16 + Style.touch + 8 +
                            Math.min(5, recorder.takes.count) * Style.touch +
                            Style.gap * 4)
            anchors.centerIn: parent
            color: Style.raised
            radius: Style.cornerPanel
            border.width: 2
            border.color: Style.accent

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: Style.gap

                Row {
                    width: parent.width
                    height: 24

                    Text {
                        width: parent.width - 92
                        height: parent.height
                        text: "TAKE FOR TRACK " +
                              String(app.takeSheetTrack + 1).padStart(2, "0")
                        color: Style.text
                        font.pixelSize: Style.fontTitle
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: "CLOSE"
                        textSize: Style.fontSmall
                        variant: "ghost"
                        focusId: "take.sheet.close"
                        onClicked: app.closeTakeSheet()
                    }
                }

                Text {
                    width: parent.width
                    height: 16
                    text: "tap a take to make it ACTIVE - the lane repaints from its peaks"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: parent.width
                    height: Style.touch
                    radius: Style.corner
                    color: Style.panelDark
                    border.width: app.focusId === "take.sheet.live" ? 2 : Style.outlineWidth
                    border.color: app.focusId === "take.sheet.live" ? Style.accent : Style.outline

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        width: parent.width - 16
                        height: parent.height
                        text: "LIVE INPUT"
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TapHandler {
                        onTapped: app.chooseLiveInput()
                    }
                }

                ListView {
                    width: parent.width
                    height: Math.min(5, recorder.takes.count) * Style.touch
                    clip: true
                    model: recorder.takes
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: Style.touch
                        radius: Style.corner
                        color: model.active ? Style.raised : Style.panelDark
                        border.width: app.focusId === ("take.sheet.row." + index) ? 2
                                     : (model.active ? 2 : Style.outlineWidth)
                        border.color: app.focusId === ("take.sheet.row." + index) ||
                                      model.active ? Style.accent : Style.outline

                        Text {
                            x: 10
                            width: 64
                            height: parent.height
                            text: model.active ? "ACTIVE" : ""
                            color: Style.accent
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 78
                            width: parent.width - 78 - 90
                            height: parent.height
                            text: model.name
                            color: Style.text
                            font.pixelSize: Style.fontBody
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideMiddle
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            width: 80
                            height: parent.height
                            text: model.duration
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        TapHandler {
                            onTapped: app.chooseTakeRow(index)
                        }
                    }
                }
            }
        }
    }
}
