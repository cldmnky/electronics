import QtQuick
import Mnkybox 1.0
import "../components"

// ARRANGE (docs/daw-plan.md W6/W7): the section band, the clip lanes, the
// playhead/loop band, SONG mode and the clip "..." sheet. The primary face is
// the arrangement; the SHIFT face keeps the section verbs and the grid/zoom
// controls. All values live in ArrangeViewModel; this file draws and forwards
// touches so the touch path and the panel ring drive the same state.
Item {
    id: page

    readonly property bool shiftFace: app.panelMode === "shift"
    readonly property int laneHeaderWidth: 112
    readonly property int laneHeight: 52
    readonly property real timelineWidth: Math.max(1, width - laneHeaderWidth)
    readonly property real pxPerBeat: timelineWidth / Math.max(0.001, arrange.viewBeats)
    // Section colour roles: a repeated cycle keeps adjacent sections distinct
    // without inventing a per-section colour field in the session.
    readonly property var sectionColors: [Style.accent, Style.ok, Style.nodePink,
                                          Style.textDim]
    property real lastPinchScale: 1.0

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

    Column {
        visible: !page.shiftFace
        anchors.fill: parent
        anchors.margins: Style.marginCompact
        spacing: Style.gapTight

        // ---- row 1: sections --------------------------------------------
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            TouchButton {
                width: 92
                height: parent.height
                text: "ADD SECTION"
                textSize: Style.fontSmall
                focusId: "arrange.addsection"
                onClicked: arrange.addSection()
            }
            TouchButton {
                width: 92
                height: parent.height
                text: "DUP SECTION"
                textSize: Style.fontSmall
                focusId: "arrange.dupsection"
                onClicked: arrange.duplicateSection()
            }
            TouchButton {
                width: 104
                height: parent.height
                text: "LOOP SECTION"
                textSize: Style.fontSmall
                focusId: "arrange.loopsection"
                onClicked: arrange.loopSection()
            }
            TouchButton {
                width: 76
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                focusId: "arrange.rename"
                onClicked: app.requestArrangeSectionRename()
            }
            Text {
                width: Math.max(70, parent.width - 92 - 92 - 104 - 76 - 84 - 64 -
                                Style.gapTight * 6)
                height: parent.height
                text: arrange.selectedSectionName
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "LEN " + arrange.lengthText
                textSize: Style.fontSmall
                focusId: "arrange.length"
                onClicked: arrange.openLengthPopup()
            }
            TouchButton {
                width: 64
                height: parent.height
                text: "LOOP"
                textSize: Style.fontSmall
                checked: arrange.loopOn
                focusId: "arrange.loop"
                onClicked: arrange.setLoopOn(!arrange.loopOn)
            }
        }

        // ---- row 2: grid, zoom, clip menu, render -----------------------
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            TouchButton {
                width: 62; height: parent.height; text: "BAR"; textSize: Style.fontSmall
                checked: arrange.grid === 0; focusId: "arrange.grid.0"
                onClicked: arrange.setGrid(0)
            }
            TouchButton {
                width: 62; height: parent.height; text: "1/2"; textSize: Style.fontSmall
                checked: arrange.grid === 1; focusId: "arrange.grid.1"
                onClicked: arrange.setGrid(1)
            }
            TouchButton {
                width: 62; height: parent.height; text: "1/4"; textSize: Style.fontSmall
                checked: arrange.grid === 2; focusId: "arrange.grid.2"
                onClicked: arrange.setGrid(2)
            }
            TouchButton {
                width: 62; height: parent.height; text: "1/16"; textSize: Style.fontSmall
                checked: arrange.grid === 3; focusId: "arrange.grid.3"
                onClicked: arrange.setGrid(3)
            }
            TouchButton {
                width: 66; height: parent.height; text: "Z-"; textSize: Style.fontSmall
                focusId: "arrange.zoomout"; onClicked: arrange.zoomOut()
            }
            TouchButton {
                width: 66; height: parent.height; text: "Z+"; textSize: Style.fontSmall
                focusId: "arrange.zoomin"; onClicked: arrange.zoomIn()
            }
            TouchButton {
                width: 92; height: parent.height; text: "CLIP ..."; textSize: Style.fontSmall
                active: arrange.hasClipSelection; focusId: "arrange.clipmenu"
                onClicked: arrange.openClipMenu(arrange.selectedClip)
            }
            TouchButton {
                width: Math.max(92, parent.width - 62 * 4 - 66 * 2 - 92 - 8 * Style.gapTight)
                height: parent.height
                text: "RENDER SONG"
                textSize: Style.fontSmall
                variant: "primary"
                focusId: "arrange.render"
                onClicked: app.renderSong()
            }
        }

        // ---- section band ------------------------------------------------
        Item {
            id: sectionBand
            width: parent.width
            height: Style.touch
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
                    width: Math.max(58, spanRatio * sectionBand.width)
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
                        font.pixelSize: Style.fontSmall
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
                text: "NO SECTIONS - ADD SECTION"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
        }

        // ---- ruler -------------------------------------------------------
        Item {
            id: ruler
            width: parent.width
            height: 18
            clip: true
            Repeater {
                model: Math.max(1, Math.ceil(arrange.viewBeats / 4) + 2)
                delegate: Text {
                    readonly property int bar:
                        Math.floor(arrange.viewStartBeats / 4) + index
                    x: page.laneHeaderWidth +
                       (bar * 4 - arrange.viewStartBeats) * page.pxPerBeat
                    width: 40
                    height: ruler.height
                    text: bar + 1
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
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
            width: parent.width
            height: Math.max(1, page.height - Style.marginCompact * 2 -
                                Style.touch * 2 - Style.touch - 18 -
                                Style.gapTight * 4)
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
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 12
                                    Text {
                                        width: parent.width
                                        text: (modelData.track + 1).toString().padStart(2, "0") +
                                              "  " + modelData.name
                                        color: Style.text
                                        font.pixelSize: Style.fontSmall
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.mode
                                        color: Style.accent
                                        font.pixelSize: Style.fontSmall
                                        font.weight: Font.DemiBold
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
                                            Math.max(44, (modelData.endBeats -
                                                          modelData.startBeats) *
                                                         page.pxPerBeat)
                                        readonly property bool active: moveDrag.active ||
                                                                       leftDrag.active ||
                                                                       rightDrag.active
                                        x: baseX + (moveDrag.active
                                                    ? page.snappedOffsetPx(moveDrag.translation.x)
                                                    : (leftDrag.active
                                                       ? page.snappedOffsetPx(leftDrag.translation.x)
                                                       : 0))
                                        width: Math.max(44, baseWidth +
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
                                            border.width: modelData.selected ? 2 : 1
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
                                            anchors.rightMargin: 20
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            color: Style.text
                                            font.pixelSize: Style.fontSmall
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
                                            // (the toolbar CLIP ... is the same verb).
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
                                        // Left/right trim handles: 22 px hit areas.
                                        Rectangle {
                                            width: 16
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
                                            width: 16
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
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: parent.width
                height: 14
                text: arrange.statusText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
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
    }

    ArrangeShiftFace {
        anchors.fill: parent
        visible: page.shiftFace
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
            height: 196
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
                    height: 24
                    text: "SONG LENGTH  LEN " + arrange.lengthText
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BARS -";
                                  textSize: Style.fontSmall;
                                  focusId: "arrange.length.decbar";
                                  onClicked: arrange.adjustLengthBars(-1) }
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BARS +";
                                  textSize: Style.fontSmall;
                                  focusId: "arrange.length.incbar";
                                  onClicked: arrange.adjustLengthBars(1) }
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BEATS -";
                                  textSize: Style.fontSmall;
                                  focusId: "arrange.length.decbeat";
                                  onClicked: arrange.adjustLengthBeats(-1) }
                    TouchButton { width: (parent.width - Style.gapTight) / 2;
                                  height: parent.height; text: "BEATS +";
                                  textSize: Style.fontSmall;
                                  focusId: "arrange.length.incbeat";
                                  onClicked: arrange.adjustLengthBeats(1) }
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    Item { width: parent.width - 96; height: 1 }
                    TouchButton { width: 96; height: parent.height; text: "DONE";
                                  textSize: Style.fontSmall; variant: "primary";
                                  focusId: "arrange.length.done";
                                  onClicked: arrange.closeLengthPopup() }
                }
            }
        }
    }
}
