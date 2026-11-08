import QtQuick
import Mnkybox 1.0
import "../components"

// ARRANGE: the section band, the clip lanes, the
// playhead/loop band, SONG mode and the clip "..." sheet. The primary face is
// the arrangement: one 56 px action row, then the section band, the ruler and
// the lanes. The section verbs live in the SECTIONS sheet and RENDER SONG in
// the SONG sheet, so the default view prioritises selecting, inspecting and
// moving clips. The SHIFT face keeps the one-tap section verbs and grid/zoom.
//
// All values live in ArrangeViewModel; this file draws and forwards touches so
// the touch path and the panel ring drive the same state. Sizes are inline
// (the shared size tokens): 56 px frequent controls, 44 px minimum, 8 px
// 16-18 px primary labels, 14 px secondary text. Lane height is min 64 px and
// grows to 72 px; the chrome above it keeps its current height and the lane
// area takes whatever vertical space is left, so a taller nav row later only
// makes the lane area shorter (it scrolls).
Item {
    id: page

    readonly property bool shiftFace: app.panelMode === "shift"
    readonly property int laneHeaderWidth: 128
    // Frequent controls: 56 px tall. Style.touch (44) stays the hard minimum.
    readonly property int rowHeight: 56
    readonly property int sectionBandHeight: 44
    readonly property int rulerHeight: 20
    readonly property int statusHeight: 16
    readonly property real timelineWidth: Math.max(1, width - laneHeaderWidth)
    readonly property real pxPerBeat: timelineWidth / Math.max(0.001, arrange.viewBeats)
    // Lanes: 64 px minimum, 72 px target; extra height fills the last lane and
    // the row scrolls once it cannot fit.
    readonly property real laneHeight: {
        const count = Math.max(1, arrange.lanes.length)
        return Math.max(64, Math.min(72, laneArea.height / count))
    }
    // Ruler label density: one label per bar when the bars are wide enough,
    // then every 2/4/8/16 bars as the timeline zooms out.
    readonly property int rulerBarStep: {
        const pxPerBar = Math.max(0.001, page.pxPerBeat * 4)
        const wanted = Math.max(1, Math.ceil(30 / pxPerBar))
        let step = 1
        while (step < wanted && step < 256) step *= 2
        return step
    }
    // Section colour roles: a repeated cycle keeps adjacent sections distinct
    // without inventing a per-section colour field in the session.
    readonly property var sectionColors: [Style.accent, Style.ok, Style.nodePink,
                                          Style.textDim]
    // The two sheet overlays are page-local state: the screen Loader rebuilds
    // this page on a tab change, so a sheet can never survive into another tab.
    property bool sectionSheetOpen: false
    property bool songSheetOpen: false
    property real lastPinchScale: 1.0

    onShiftFaceChanged: {
        page.sectionSheetOpen = false
        page.songSheetOpen = false
    }

    // The panel ring's ARRANGE list is fixed (AppController::rebuildFocusIds):
    // the section verbs are stops even while the SECTIONS sheet is closed. Open
    // the sheet when the ring lands on one of them so the focus ring is always
    // visible, and close it when the ring moves back to the arrangement.
    Connections {
        target: app
        function onFocusChanged() {
            if (page.shiftFace) return
            const id = app.focusId
            // RENAME is the visible section-name cell, so the sheet only
            // needs to open for the verbs that live inside it.
            page.sectionSheetOpen = (id === "arrange.addsection" ||
                                     id === "arrange.dupsection" ||
                                     id === "arrange.loopsection" ||
                                     id === "arrange.length")
        }
    }

    function beatAtX(x) {
        return arrange.viewStartBeats + x / pxPerBeat
    }

    function snappedOffsetPx(delta) {
        const gridPx = Math.max(1, arrange.gridText === "BAR" ? 4 * pxPerBeat
                              : arrange.gridText === "1/2" ? 2 * pxPerBeat
                              : arrange.gridText === "1/4" ? pxPerBeat
                              : 0.25 * pxPerBeat)
        return Math.round(delta / gridPx) * gridPx
    }

    Item {
        id: primary
        visible: !page.shiftFace
        anchors.fill: parent
        anchors.margins: Style.marginCompact

        // ---- action row: sections, grid, zoom, loop, clip, song ---------
        Item {
            id: actionRow
            width: parent.width
            height: page.rowHeight

            TouchButton {
                id: sectionsButton
                anchors.left: parent.left
                anchors.top: parent.top
                width: 118
                height: parent.height
                text: "SECTIONS"
                textSize: 16
                chevron: "down"
                focusId: "arrange.sectionmenu"
                onClicked: page.sectionSheetOpen = true
            }

            // The selected section name doubles as RENAME: the name is context
            // and the cell keeps the ring's arrange.rename stop visible without
            // spending a toolbar slot on a separate verb.
            Rectangle {
                id: sectionNameCell
                anchors.left: sectionsButton.right
                anchors.leftMargin: Style.gap
                anchors.right: (clipButton.visible ? clipButton.left : gridButton.left)
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                radius: Style.corner
                color: Style.panel
                border.width: app.focusId === "arrange.rename" ? 2 : 1
                border.color: app.focusId === "arrange.rename" ? Style.accent
                                                               : Style.outline
                property string focusId: "arrange.rename"

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Text {
                        width: parent.width
                        height: 22
                        text: arrange.selectedSectionName
                        color: Style.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        height: 16
                        text: "TAP TO RENAME"
                        color: Style.textDim
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                TapHandler {
                    onTapped: app.requestArrangeSectionRename()
                }
            }

            TouchButton {
                id: clipButton
                anchors.right: songButton.left
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                width: 92
                height: parent.height
                text: "CLIP ..."
                textSize: 16
                visible: arrange.hasClipSelection
                active: arrange.hasClipSelection
                focusId: "arrange.clipmenu"
                onClicked: arrange.openClipMenu(arrange.selectedClip)
            }

            TouchButton {
                id: songButton
                anchors.right: parent.right
                anchors.top: parent.top
                width: 96
                height: parent.height
                text: "SONG"
                textSize: 16
                chevron: "down"
                // The ring's arrange.render stop lives here: a ring press
                // renders immediately (AppController), while a touch opens the
                // SONG sheet that owns RENDER SONG plus loop/length. The id is
                // preserved because the ring list is fixed; see the handoff.
                focusId: "arrange.render"
                onClicked: page.songSheetOpen = true
            }

            TouchButton {
                id: loopButton
                anchors.right: songButton.left
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                width: 72
                height: parent.height
                text: "LOOP"
                textSize: 16
                checked: arrange.loopOn
                focusId: "arrange.loop"
                onClicked: arrange.setLoopOn(!arrange.loopOn)
            }

            TouchButton {
                id: zoomInButton
                anchors.right: loopButton.left
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                width: 56
                height: parent.height
                text: "Z+"
                textSize: 16
                focusId: "arrange.zoomin"
                onClicked: arrange.zoomIn()
            }
            TouchButton {
                id: zoomOutButton
                anchors.right: zoomInButton.left
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                width: 56
                height: parent.height
                text: "Z-"
                textSize: 16
                focusId: "arrange.zoomout"
                onClicked: arrange.zoomOut()
            }

            // One grid selector instead of four buttons: a touch cycles
            // BAR -> 1/2 -> 1/4 -> 1/16, and the panel's fixed grid.0..3 stops
            // all draw on this control while the ring writes the exact grid.
            TouchButton {
                id: gridButton
                anchors.right: zoomOutButton.left
                anchors.rightMargin: Style.gap
                anchors.top: parent.top
                width: 116
                height: parent.height
                text: "GRID " + arrange.gridText
                textSize: 16
                focusId: app.focusId.indexOf("arrange.grid.") === 0
                         ? app.focusId : "arrange.grid"
                onClicked: arrange.setGrid((arrange.grid + 1) % 4)
            }
        }

        // ---- section band ------------------------------------------------
        Item {
            id: sectionBand
            anchors.top: actionRow.bottom
            anchors.topMargin: 6
            width: parent.width
            height: page.sectionBandHeight
            clip: true
            Rectangle {
                anchors.fill: parent
                color: Style.panelDark
                radius: Style.corner
                border.width: 1
                border.color: Style.outline
            }
            Repeater {
                model: arrange.sections
                delegate: Rectangle {
                    readonly property real startRatio:
                        (modelData.startBeats - arrange.viewStartBeats) / arrange.viewBeats
                    readonly property real spanRatio:
                        (modelData.endBeats - modelData.startBeats) / arrange.viewBeats
                    x: Math.max(0, startRatio * sectionBand.width)
                    width: Math.max(64, spanRatio * sectionBand.width)
                    height: sectionBand.height
                    radius: Style.corner
                    color: modelData.selected ? Style.raised : Style.panel
                    border.width: modelData.selected ? 2 : 1
                    border.color: modelData.selected ? Style.accent
                                : page.sectionColors[modelData.index % page.sectionColors.length]
                    opacity: modelData.beyondLength ? 0.55 : 1.0
                    property string focusId: "arrange.section." + modelData.index

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name + "  " + modelData.bars
                        color: modelData.selected ? Style.text : Style.textDim
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    Rectangle {
                        visible: modelData.looping
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 4
                        height: 2
                        radius: 1
                        color: Style.accent
                    }
                    TapHandler {
                        onTapped: arrange.selectSection(modelData.index)
                    }
                }
            }
            Text {
                visible: arrange.sectionCount === 0
                anchors.centerIn: parent
                text: "NO SECTIONS - OPEN SECTIONS"
                color: Style.textDim
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }

        // ---- ruler -------------------------------------------------------
        Item {
            id: ruler
            anchors.top: sectionBand.bottom
            anchors.topMargin: 2
            width: parent.width
            height: page.rulerHeight
            clip: true
            Repeater {
                model: Math.max(1, Math.ceil(arrange.viewBeats / 4) + 2)
                delegate: Text {
                    readonly property int bar:
                        Math.floor(arrange.viewStartBeats / 4) + index
                    x: page.laneHeaderWidth +
                       (bar * 4 - arrange.viewStartBeats) * page.pxPerBeat
                    width: 56
                    height: ruler.height
                    visible: bar % page.rulerBarStep === 0
                    text: bar + 1
                    color: Style.textDim
                    font.pixelSize: 14
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: page.laneHeaderWidth
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Style.outline
            }
        }

        // ---- lanes -------------------------------------------------------
        Item {
            id: laneArea
            anchors.top: ruler.bottom
            anchors.bottom: parent.bottom
            anchors.bottomMargin: page.statusHeight + 2
            width: parent.width
            clip: true

            Flickable {
                id: laneFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: laneColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: laneColumn
                    width: laneFlick.width
                    Repeater {
                        model: arrange.lanes
                        delegate: Item {
                            id: lane
                            width: laneColumn.width
                            height: page.laneHeight
                            property bool isSelected: modelData.selected

                            Rectangle {
                                width: page.laneHeaderWidth - 4
                                height: parent.height - 2
                                anchors.verticalCenter: parent.verticalCenter
                                radius: Style.corner
                                color: lane.isSelected ? Style.raised : Style.panel
                                border.width: lane.isSelected ? 2 : 1
                                border.color: lane.isSelected ? Style.accent : Style.outline
                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 0
                                    Text {
                                        width: parent.width
                                        height: 22
                                        text: (modelData.track + 1).toString().padStart(2, "0") +
                                              "  " + modelData.name
                                        color: Style.text
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    Text {
                                        width: parent.width
                                        height: 18
                                        text: modelData.mode
                                        color: Style.accent
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                                TapHandler {
                                    onTapped: app.selectStrip(modelData.track)
                                }
                            }

                            Item {
                                id: laneTimeline
                                x: page.laneHeaderWidth
                                width: page.timelineWidth
                                height: parent.height
                                clip: true

                                Canvas {
                                    anchors.fill: parent
                                    Connections {
                                        target: arrange
                                        function onViewChanged() { gridCanvas.requestPaint() }
                                        function onGridChanged() { gridCanvas.requestPaint() }
                                    }
                                    id: gridCanvas
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        const step = arrange.gridText === "BAR" ? 4
                                                   : arrange.gridText === "1/2" ? 2
                                                   : arrange.gridText === "1/4" ? 1 : 0.25
                                        ctx.strokeStyle = Style.gridLine
                                        ctx.lineWidth = 1
                                        const first = Math.floor(arrange.viewStartBeats / step) * step
                                        for (let beat = first; beat <= arrange.viewStartBeats + arrange.viewBeats;
                                             beat += step) {
                                            const x = (beat - arrange.viewStartBeats) * page.pxPerBeat
                                            ctx.beginPath()
                                            ctx.moveTo(x, 0)
                                            ctx.lineTo(x, height)
                                            ctx.stroke()
                                        }
                                        ctx.strokeStyle = Qt.rgba(0.6, 0.65, 0.7, 0.28)
                                        const firstBar = Math.floor(arrange.viewStartBeats / 4) * 4
                                        for (let beat = firstBar; beat <= arrange.viewStartBeats + arrange.viewBeats;
                                             beat += 4) {
                                            const x = (beat - arrange.viewStartBeats) * page.pxPerBeat
                                            ctx.beginPath()
                                            ctx.moveTo(x, 0)
                                            ctx.lineTo(x, height)
                                            ctx.stroke()
                                        }
                                    }
                                }

                                // Empty lane = playhead jump (and transport seek).
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: (mouse) => {
                                        app.selectStrip(modelData.track)
                                        arrange.seekToBeat(page.beatAtX(mouse.x))
                                    }
                                }

                                Repeater {
                                    model: modelData.clips
                                    delegate: Item {
                                        id: clipItem
                                        readonly property real baseX:
                                            (modelData.startBeats - arrange.viewStartBeats) *
                                            page.pxPerBeat
                                        readonly property real baseWidth:
                                            Math.max(48, (modelData.endBeats -
                                                          modelData.startBeats) *
                                                         page.pxPerBeat)
                                        readonly property bool active: moveDrag.active ||
                                                                       leftDrag.active ||
                                                                       rightDrag.active
                                        readonly property real edgeWidth:
                                            Math.max(20, Math.min(28, width / 3))
                                        x: baseX + (moveDrag.active
                                                    ? page.snappedOffsetPx(moveDrag.translation.x)
                                                    : (leftDrag.active
                                                       ? page.snappedOffsetPx(leftDrag.translation.x)
                                                       : 0))
                                        width: Math.max(48, baseWidth +
                                                        (rightDrag.active
                                                         ? page.snappedOffsetPx(rightDrag.translation.x)
                                                         : (leftDrag.active
                                                            ? -page.snappedOffsetPx(leftDrag.translation.x)
                                                            : 0)))
                                        height: laneTimeline.height - 4
                                        y: 2
                                        z: modelData.selected ? 3 : 1
                                        opacity: modelData.muted ? 0.42 : 1.0
                                        property string focusId:
                                            "arrange.clip." + modelData.index

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Style.corner
                                            color: modelData.muted ? Style.panelDark
                                                   : (modelData.selected ? Style.raised : Style.panel)
                                            border.width: modelData.selected ? 3 : 1
                                            border.color: modelData.selected ? Style.accent : Style.outline
                                            clip: true

                                            TimelineWave {
                                                anchors.fill: parent
                                                anchors.margins: 2
                                                values: modelData.wave
                                                placeholderText: ""
                                                waveColor: modelData.muted ? Style.textDim
                                                           : (modelData.selected ? Style.accent
                                                                                 : Style.textDim)
                                                opacity: 0.55
                                            }
                                            // Loop arrow badge: drawn, never a text glyph
                                            // (UI strings stay ASCII).
                                            Canvas {
                                                visible: modelData.loop
                                                width: 18
                                                height: 18
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 3
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.clearRect(0, 0, width, height)
                                                    ctx.strokeStyle = Style.accent
                                                    ctx.lineWidth = 1.5
                                                    ctx.beginPath()
                                                    ctx.arc(width / 2, height / 2, 5, 0.6, 5.0)
                                                    ctx.stroke()
                                                    ctx.beginPath()
                                                    ctx.moveTo(width - 4, 5)
                                                    ctx.lineTo(width - 4, 9)
                                                    ctx.lineTo(width - 8, 9)
                                                    ctx.stroke()
                                                }
                                            }
                                        }
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 6
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            color: modelData.selected ? Style.accent : Style.text
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideMiddle
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        // Muted clips keep a dim diagonal marker
                                        // rather than disappearing.
                                        Rectangle {
                                            visible: modelData.muted
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 2
                                            color: Style.record
                                        }

                                        property real dragStartBeats: 0
                                        property real dragStartEnd: 0

                                        TapHandler {
                                            // First tap selects; a tap on the already
                                            // selected clip opens its "..." sheet
                                            // (the SECTIONS row's CLIP ... is the
                                            // same verb).
                                            onTapped: {
                                                if (arrange.selectedClip === modelData.index)
                                                    arrange.openClipMenu(modelData.index)
                                                else
                                                    arrange.selectClip(modelData.index)
                                            }
                                            onLongPressed: arrange.duplicateClipAt(modelData.index)
                                        }
                                        DragHandler {
                                            id: moveDrag
                                            target: null
                                            xAxis.enabled: true
                                            yAxis.enabled: false
                                            onActiveChanged: {
                                                if (active) {
                                                    clipItem.dragStartBeats = modelData.startBeats
                                                    arrange.beginClipDrag(modelData.index, 0)
                                                } else if (!leftDrag.active && !rightDrag.active) {
                                                    arrange.commitClipDrag(
                                                        modelData.index, 0,
                                                        clipItem.dragStartBeats +
                                                        moveDrag.translation.x / page.pxPerBeat)
                                                }
                                            }
                                        }
                                        // Edge trim handles: 20-28 px per side,
                                        // wide enough to grab at 64 px lanes.
                                        Rectangle {
                                            width: clipItem.edgeWidth
                                            height: parent.height
                                            anchors.left: parent.left
                                            color: "transparent"
                                            DragHandler {
                                                id: leftDrag
                                                target: null
                                                xAxis.enabled: true
                                                yAxis.enabled: false
                                                onActiveChanged: {
                                                    if (active) {
                                                        clipItem.dragStartBeats = modelData.startBeats
                                                        arrange.beginClipDrag(modelData.index, 1)
                                                    } else {
                                                        arrange.commitClipDrag(
                                                            modelData.index, 1,
                                                            clipItem.dragStartBeats +
                                                            leftDrag.translation.x / page.pxPerBeat)
                                                    }
                                                }
                                            }
                                        }
                                        Rectangle {
                                            width: clipItem.edgeWidth
                                            height: parent.height
                                            anchors.right: parent.right
                                            color: "transparent"
                                            DragHandler {
                                                id: rightDrag
                                                target: null
                                                xAxis.enabled: true
                                                yAxis.enabled: false
                                                onActiveChanged: {
                                                    if (active) {
                                                        clipItem.dragStartEnd = modelData.endBeats
                                                        arrange.beginClipDrag(modelData.index, 2)
                                                    } else {
                                                        arrange.commitClipDrag(
                                                            modelData.index, 2,
                                                            clipItem.dragStartEnd +
                                                            rightDrag.translation.x / page.pxPerBeat)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Empty song: the lanes may be empty or absent entirely, so the
            // state names the way in from here.
            Text {
                visible: arrange.clipCount === 0
                anchors.left: parent.left
                anchors.leftMargin: page.laneHeaderWidth + 12
                anchors.verticalCenter: parent.verticalCenter
                width: page.timelineWidth - 24
                text: "NO CLIPS YET - PUNCH A TAKE INTO SONG MODE"
                color: Style.textDim
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            // Loop band over the lanes, aligned with the ruler/timeline.
            Item {
                id: loopBandHost
                x: page.laneHeaderWidth
                y: 0
                width: page.timelineWidth
                height: laneArea.height
                visible: arrange.loopOn
                Rectangle {
                    id: loopBand
                    readonly property real startRatio:
                        (arrange.loopStartBeats - arrange.viewStartBeats) /
                        Math.max(0.001, arrange.viewBeats)
                    readonly property real endRatio:
                        (arrange.loopEndBeats - arrange.viewStartBeats) /
                        Math.max(0.001, arrange.viewBeats)
                    x: Math.max(0, startRatio * parent.width)
                    width: Math.max(2, (endRatio - startRatio) * parent.width)
                    height: parent.height
                    color: Qt.rgba(0.31, 0.76, 0.97, 0.16)
                    border.width: 1
                    border.color: Qt.rgba(0.31, 0.76, 0.97, 0.8)
                    opacity: 1.0
                    SequentialAnimation {
                        id: loopFlash
                        PropertyAnimation { target: loopBand; property: "opacity";
                                            to: 0.25; duration: 90 }
                        PropertyAnimation { target: loopBand; property: "opacity";
                                            to: 1.0; duration: 220 }
                    }
                    Connections {
                        target: arrange
                        function onLoopChanged() { loopFlash.restart() }
                    }
                    // Wrap pulse: a wrap is a seek to the loop start and must
                    // be visible, not only audible. app.loopWrapFlash is the
                    // controller's ~280 ms cue, shared with the transport strip.
                    Rectangle {
                        id: loopWrapPulse
                        anchors.fill: parent
                        color: Style.accent
                        opacity: 0.0
                        SequentialAnimation {
                            id: loopWrapAnimation
                            PropertyAnimation { target: loopWrapPulse; property: "opacity";
                                                to: 0.45; duration: 40 }
                            PauseAnimation { duration: 120 }
                            PropertyAnimation { target: loopWrapPulse; property: "opacity";
                                                to: 0.0; duration: 120 }
                        }
                        Connections {
                            target: app
                            function onLoopWrapChanged() {
                                if (app.loopWrapFlash) loopWrapAnimation.restart()
                            }
                        }
                    }
                }
            }

            // Playhead over the lanes.
            Rectangle {
                id: playhead
                x: page.laneHeaderWidth +
                   (arrange.playheadBeats - arrange.viewStartBeats) * page.pxPerBeat
                width: 2
                height: laneArea.height
                color: Style.accent
                visible: x >= page.laneHeaderWidth &&
                         x <= page.laneHeaderWidth + page.timelineWidth
            }

            PinchHandler {
                id: pinch
                target: null
                onActiveChanged: if (active) page.lastPinchScale = 1.0
                onScaleChanged: {
                    if (!active) return
                    const factor = pinch.scale / page.lastPinchScale
                    if (factor > 1.02 || factor < 0.98) {
                        arrange.zoomBy(factor)
                        page.lastPinchScale = pinch.scale
                    }
                }
            }
        }

        // ---- status line -------------------------------------------------
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: page.statusHeight
            text: arrange.statusText
            color: Style.textDim
            font.pixelSize: 14
            elide: Text.ElideRight
        }
    }

    ArrangeShiftFace {
        anchors.fill: parent
        visible: page.shiftFace
    }

    // ---- SECTIONS sheet: the section verbs in one 64 px grid -----------
    Rectangle {
        id: sectionSheet
        anchors.fill: parent
        visible: page.sectionSheetOpen
        color: "#9e000000"
        z: 18
        MouseArea {
            anchors.fill: parent
            onClicked: page.sectionSheetOpen = false
        }
        Rectangle {
            width: Math.min(560, page.width - 32)
            height: sheetColumn.implicitHeight + 24
            anchors.centerIn: parent
            radius: Style.cornerPanel
            color: Style.raised
            border.width: 2
            border.color: Style.accent

            Column {
                id: sheetColumn
                x: 12
                y: 12
                width: parent.width - 24
                spacing: Style.gap

                Row {
                    width: parent.width
                    height: 26
                    Text {
                        width: parent.width - 150
                        height: parent.height
                        text: "SECTIONS  " + arrange.selectedSectionName
                        color: Style.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    Text {
                        width: 150
                        height: parent.height
                        text: arrange.sectionCount + " SECTIONS"
                        color: Style.textDim
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Grid {
                    width: parent.width
                    columns: 2
                    spacing: Style.gap
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "ADD SECTION"
                        textSize: 16
                        focusId: "arrange.addsection"
                        onClicked: arrange.addSection()
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "DUP SECTION"
                        textSize: 16
                        focusId: "arrange.dupsection"
                        onClicked: arrange.duplicateSection()
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "LOOP SECTION"
                        textSize: 16
                        focusId: "arrange.loopsection"
                        onClicked: arrange.loopSection()
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "LEN " + arrange.lengthText
                        textSize: 16
                        focusId: "arrange.length"
                        onClicked: arrange.openLengthPopup()
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "RENAME"
                        textSize: 16
                        onClicked: app.requestArrangeSectionRename()
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: 64
                        text: "CLOSE"
                        textSize: 16
                        variant: "ghost"
                        onClicked: page.sectionSheetOpen = false
                    }
                }
            }
        }
    }

    // ---- SONG sheet: render + the song-wide verbs ----------------------
    Rectangle {
        id: songSheet
        anchors.fill: parent
        visible: page.songSheetOpen
        color: "#9e000000"
        z: 17
        MouseArea {
            anchors.fill: parent
            onClicked: page.songSheetOpen = false
        }
        Rectangle {
            width: Math.min(480, page.width - 32)
            height: songColumn.implicitHeight + 24
            anchors.centerIn: parent
            radius: Style.cornerPanel
            color: Style.raised
            border.width: 2
            border.color: Style.accent

            Column {
                id: songColumn
                x: 12
                y: 12
                width: parent.width - 24
                spacing: Style.gap

                Text {
                    width: parent.width
                    height: 26
                    text: "SONG ACTIONS"
                    color: Style.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                TouchButton {
                    width: parent.width
                    height: 64
                    text: "RENDER SONG"
                    textSize: 18
                    variant: "primary"
                    onClicked: {
                        page.songSheetOpen = false
                        app.renderSong()
                    }
                }
                Row {
                    width: parent.width
                    height: 56
                    spacing: Style.gap
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        text: "LEN " + arrange.lengthText
                        textSize: 14
                        onClicked: {
                            page.songSheetOpen = false
                            arrange.openLengthPopup()
                        }
                    }
                    TouchButton {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        text: arrange.loopOn ? "LOOP ON" : "LOOP OFF"
                        textSize: 14
                        checked: arrange.loopOn
                        onClicked: arrange.setLoopOn(!arrange.loopOn)
                    }
                }
                TouchButton {
                    width: parent.width
                    height: 56
                    text: "CLOSE"
                    textSize: 16
                    variant: "ghost"
                    onClicked: page.songSheetOpen = false
                }
            }
        }
    }

    // ---- LEN stepper popup ------------------------------------------------
    Rectangle {
        id: lengthPopup
        anchors.fill: parent
        visible: arrange.lengthPopupOpen
        color: "#9e000000"
        z: 20
        MouseArea { anchors.fill: parent; onClicked: {} }
        Rectangle {
            width: 380
            height: 232
            anchors.centerIn: parent
            radius: Style.cornerPanel
            color: Style.raised
            border.width: 2
            border.color: Style.accent
            Column {
                x: 12
                y: 10
                width: parent.width - 24
                spacing: Style.gapTight
                Text {
                    width: parent.width
                    height: 26
                    text: "SONG LENGTH  LEN " + arrange.lengthText
                    color: Style.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Row {
                    width: parent.width
                    height: 56
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BARS -";
                                  textSize: 16;
                                  focusId: "arrange.length.decbar";
                                  onClicked: arrange.adjustLengthBars(-1) }
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BARS +";
                                  textSize: 16;
                                  focusId: "arrange.length.incbar";
                                  onClicked: arrange.adjustLengthBars(1) }
                }
                Row {
                    width: parent.width
                    height: 56
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BEATS -";
                                  textSize: 16;
                                  focusId: "arrange.length.decbeat";
                                  onClicked: arrange.adjustLengthBeats(-1) }
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BEATS +";
                                  textSize: 16;
                                  focusId: "arrange.length.incbeat";
                                  onClicked: arrange.adjustLengthBeats(1) }
                }
                Row {
                    width: parent.width
                    height: 56
                    spacing: Style.gapTight
                    Item { width: parent.width - 96; height: 1 }
                    TouchButton { width: 96; height: parent.height; text: "DONE";
                                  textSize: 16; variant: "primary";
                                  focusId: "arrange.length.done";
                                  onClicked: arrange.closeLengthPopup() }
                }
            }
        }
    }
}
