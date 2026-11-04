import QtQuick
import Mnkybox 1.0
import "../components"

// X/Y performance: two grids over the built-in send effects. Delay X = feedback
// filter cutoff, Y = width; reverb X = size, Y = level (USER-MANUAL section 10).
// One finger per grid, both grids at once: each MultiPointTouchArea forwards its
// TouchPoints to XyViewModel, so two fingers are tracked independently. The
// panel path is the same mapping: EDIT directions move the focused point,
// SHIFT is the coarse step, PRESS leaves EDIT, FREEZE is a PRESS toggle.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        Row {
            width: parent.width
            height: Style.touch
            spacing: 4

            Text {
                width: parent.width - 376
                height: parent.height
                text: "X/Y PERFORMANCE - one finger per grid, both at once"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Rectangle {
                width: 224
                height: parent.height
                color: Style.panelDark
                radius: Style.corner
                border.width: xy.touchCount > 0 ? 2 : Style.outlineWidth
                border.color: xy.touchCount > 0 ? Style.accent : Style.outline
                Text {
                    anchors.centerIn: parent
                    text: xy.touchText
                    color: xy.touchCount > 0 ? Style.text : Style.textDim
                    font.pixelSize: 10
                    font.bold: true
                }
            }
            TouchButton {
                width: 128
                height: parent.height
                text: xy.freezeText
                textSize: Style.fontSmall
                checked: xy.frozen
                checkedColor: Style.nodePink
                focusId: "xy.freeze"
                onClicked: xy.toggleFreeze()
            }
        }

        Row {
            width: parent.width
            height: parent.height - Style.touch * 2 - 8
            spacing: 4

            XyGrid {
                width: (parent.width - 4) / 2
                height: parent.height
                gridIndex: 0
                title: "DELAY"
                axisX: "X CUTOFF"
                axisY: "Y WIDTH"
                px: xy.delayX
                py: xy.delayY
                xValue: xy.delayXText
                yValue: xy.delayYText
                focusId: "xy.delay"
                frozenGrid: false
            }
            XyGrid {
                width: (parent.width - 4) / 2
                height: parent.height
                gridIndex: 1
                title: "REVERB"
                axisX: "X SIZE"
                axisY: "Y LEVEL"
                px: xy.reverbX
                py: xy.reverbY
                xValue: xy.reverbXText
                yValue: xy.reverbYText
                focusId: "xy.reverb"
                frozenGrid: xy.frozen
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 4

            TouchButton {
                width: (parent.width - 8) / 2
                height: parent.height
                text: "DELAY PARAMS  " + xy.delayXText + " / " + xy.delayYText
                textSize: Style.fontSmall
                focusId: "xy.delay.params"
                onClicked: app.openConfig("delay")
            }
            TouchButton {
                width: (parent.width - 8) / 2
                height: parent.height
                text: "REVERB PARAMS  " + xy.reverbXText + " / " + xy.reverbYText
                textSize: Style.fontSmall
                active: !xy.frozen
                focusId: "xy.reverb.params"
                onClicked: app.openConfig("reverb")
            }
        }
    }

    // One X/Y grid. `px`/`py` are the normalized model position; the touch area
    // forwards every point id so two fingers on the two grids stay independent.
    component XyGrid: Item {
        id: gridItem
        property int gridIndex: 0
        property string title: ""
        property string axisX: ""
        property string axisY: ""
        property real px: 0.5
        property real py: 0.5
        property string xValue: ""
        property string yValue: ""
        property string focusId: ""
        property bool frozenGrid: false
        // The point stays fully inside the tile; the touch area uses the same
        // inset, so a finger at the tile edge still maps to 0 or 1.
        readonly property int inset: 15

        Rectangle {
            anchors.fill: parent
            color: Style.panelDark
            radius: Style.corner
            border.width: app.focusId === gridItem.focusId ? 2 : Style.outlineWidth
            border.color: app.focusId === gridItem.focusId ? Style.accent : Style.outline
        }

        Canvas {
            id: gridLines
            anchors.fill: parent
            anchors.margins: 1
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = 1
                for (let i = 1; i < 4; ++i) {
                    ctx.beginPath()
                    ctx.strokeStyle = Style.gridLine.toString()
                    ctx.moveTo(i * width / 4, 0)
                    ctx.lineTo(i * width / 4, height)
                    ctx.moveTo(0, i * height / 4)
                    ctx.lineTo(width, i * height / 4)
                    ctx.stroke()
                }
                // Centre cross: the default 0.5 / 0.5 position.
                ctx.beginPath()
                ctx.strokeStyle = Style.outline.toString()
                ctx.moveTo(width / 2, 0)
                ctx.lineTo(width / 2, height)
                ctx.moveTo(0, height / 2)
                ctx.lineTo(width, height / 2)
                ctx.stroke()
            }
        }

        Text {
            x: 8
            y: 6
            text: gridItem.title
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.bold: true
        }

        Rectangle {
            id: point
            width: 30
            height: 30
            radius: 15
            x: gridItem.inset + gridItem.px * (gridItem.width - 2 * gridItem.inset) - width / 2
            y: gridItem.inset + (1.0 - gridItem.py) * (gridItem.height - 2 * gridItem.inset) - height / 2
            color: gridItem.frozenGrid ? Style.nodePink : Style.accent
            border.width: 2
            border.color: Style.panelDark
            opacity: 0.9
        }

        Rectangle {
            visible: gridItem.frozenGrid
            anchors.centerIn: parent
            width: 116
            height: 26
            radius: Style.corner
            color: Style.nodePink
            Text {
                anchors.centerIn: parent
                text: "FROZEN"
                color: Style.panelDark
                font.pixelSize: Style.fontSmall
                font.bold: true
            }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            height: 18
            Text {
                width: parent.width / 2
                height: parent.height
                text: gridItem.axisX + " " + gridItem.xValue
                color: Style.text
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                width: parent.width / 2
                height: parent.height
                text: gridItem.axisY + " " + gridItem.yValue
                color: Style.text
                font.pixelSize: 10
                font.bold: true
                horizontalAlignment: Text.AlignRight
            }
        }

        // Touch: every point on this grid is forwarded with its Qt point id, so
        // one finger here and one on the other grid are two independent points.
        MultiPointTouchArea {
            anchors.fill: parent
            minimumTouchPoints: 1
            maximumTouchPoints: 4
            mouseEnabled: false
            readonly property real spanX: gridItem.width - 2 * gridItem.inset
            readonly property real spanY: gridItem.height - 2 * gridItem.inset
            onPressed: (points) => {
                for (let i = 0; i < points.length; ++i) {
                    xy.touchBegin(gridItem.gridIndex, points[i].pointId,
                                  (points[i].x - gridItem.inset) / spanX,
                                  1.0 - (points[i].y - gridItem.inset) / spanY)
                }
            }
            onUpdated: (points) => {
                for (let i = 0; i < points.length; ++i) {
                    xy.touchMove(gridItem.gridIndex, points[i].pointId,
                                 (points[i].x - gridItem.inset) / spanX,
                                 1.0 - (points[i].y - gridItem.inset) / spanY)
                }
            }
            onReleased: (points) => {
                for (let i = 0; i < points.length; ++i) {
                    xy.touchEnd(gridItem.gridIndex, points[i].pointId)
                }
            }
            onCanceled: (points) => {
                for (let i = 0; i < points.length; ++i) {
                    xy.touchEnd(gridItem.gridIndex, points[i].pointId)
                }
            }
        }

        // Tap-to-place for a single mouse click in the desktop run.
        TapHandler {
            onTapped: (eventPoint) => {
                const p = eventPoint.position
                xy.setPoint(gridItem.gridIndex,
                            (p.x - gridItem.inset) / (gridItem.width - 2 * gridItem.inset),
                            1.0 - (p.y - gridItem.inset) / (gridItem.height - 2 * gridItem.inset))
            }
        }
    }
}
