import QtQuick
import Mnkybox 1.0
import "../components"

// X/Y performance, per track: grid 0 bends the selected track's FX1 slot, grid 1
// its FX2 slot. Delay X = feedback-filter cutoff, Y = width; reverb X = size,
// Y = level (USER-MANUAL section 10). One finger per grid, both grids at once:
// each MultiPointTouchArea forwards its TouchPoints to XyViewModel, so two
// fingers are tracked independently. The panel path is the same mapping: EDIT
// directions move the focused grid, SHIFT is the coarse step, PRESS leaves EDIT,
// FREEZE is a PRESS toggle over the track's reverb slot.
//
// A slot that holds nothing (or a CLAP plugin, which has no X/Y axes) shows the
// slot note and the picker chip instead of a position; a plugin slot's parameters
// are edited in TRACK FX. The track row reuses the mixer's four-per-page banks,
// so the same strip selection is one tap away.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: parent.width - 376
                height: parent.height
                // the permanent instruction is gone; the two visible
                // grids are the mode.
                text: "X/Y  -  " + xy.trackName + "  -  BOTH GRIDS"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
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
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                }
            }
            TouchButton {
                width: 128
                height: parent.height
                text: xy.freezeText
                textSize: Style.fontSmall
                checked: xy.frozen
                focusId: "xy.freeze"
                onClicked: xy.toggleFreeze()
            }
        }

        // Track selector: the selected track is the one both grids bend. The
        // bank is the mixer's, so the X/Y row and the mixer show the same four.
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            TouchButton {
                width: Style.touch
                height: parent.height
                text: "<"
                textSize: 18
                focusId: "xy.bank.0"
                onClicked: mixer.stepBank(-1)
            }
            TouchButton {
                width: Style.touch
                height: parent.height
                text: ">"
                textSize: 18
                focusId: "xy.bank.1"
                onClicked: mixer.stepBank(1)
            }
            Repeater {
                model: mixer
                delegate: TouchButton {
                    readonly property int stripSlot: model.slot
                    // The page readout at the row's end needs its own width,
                    // so four strips never push it off the screen.
                    width: visible ? (screen.width - Style.margin * 2 - Style.touch * 2 -
                                      Style.gapTight * 6 -
                                      (mixer.pageCount > 1 ? 112 : 0)) /
                                     mixer.stripsPerBank : 0
                    height: Style.touch
                    visible: index >= mixer.bank * mixer.stripsPerBank &&
                             index < (mixer.bank + 1) * mixer.stripsPerBank
                    text: model.name !== "" ? model.name
                                            : "IN " + String(model.slot + 1).padStart(2, "0")
                    textSize: Style.fontSmall
                    checked: model.slot === mixer.selectedStrip
                    focusId: "xy.track." + model.slot
                    onClicked: app.selectStrip(stripSlot)
                }
            }
            Text {
                width: mixer.pageCount > 1 ? 112 : 0
                height: parent.height
                visible: mixer.pageCount > 1
                text: mixer.bankText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Row {
            width: parent.width
            height: parent.height - Style.touch * 3 - Style.gap * 3
            spacing: Style.gap

            XyGrid {
                width: (parent.width - Style.gap) / 2
                height: parent.height
                gridIndex: 0
                title: xy.fx1.title
                axisX: xy.fx1.axisX
                axisY: xy.fx1.axisY
                px: xy.fx1.x
                py: xy.fx1.y
                xValue: xy.fx1.xValue
                yValue: xy.fx1.yValue
                bound: xy.fx1.bound
                note: xy.fx1.note
                focusId: "xy.delay"
                frozenGrid: false
            }
            XyGrid {
                width: (parent.width - Style.gap) / 2
                height: parent.height
                gridIndex: 1
                title: xy.fx2.title
                axisX: xy.fx2.axisX
                axisY: xy.fx2.axisY
                px: xy.fx2.x
                py: xy.fx2.y
                xValue: xy.fx2.xValue
                yValue: xy.fx2.yValue
                bound: xy.fx2.bound
                note: xy.fx2.note
                focusId: "xy.reverb"
                frozenGrid: xy.frozen
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: (parent.width - 8) / 2
                height: parent.height
                text: "FX1 PARAMS  " + xy.fx1.kind
                textSize: Style.fontSmall
                active: xy.fx1.bound
                focusId: "xy.delay.params"
                onClicked: app.openFxSlotParams(0)
            }
            TouchButton {
                width: (parent.width - 8) / 2
                height: parent.height
                text: "FX2 PARAMS  " + xy.fx2.kind
                textSize: Style.fontSmall
                active: xy.fx2.bound && !xy.frozen
                focusId: "xy.reverb.params"
                onClicked: app.openFxSlotParams(1)
            }
        }
    }

    // One X/Y grid. `px`/`py` are the normalized model position; the touch area
    // forwards every point id so two fingers on the two grids stay independent.
    // An unbound slot shows the note and the picker chip instead of the point.
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
        property bool bound: false
        property string note: ""
        property string focusId: ""
        property bool frozenGrid: false
        // The point stays fully inside the tile; the touch area uses the same
        // inset, so a finger at the tile edge still maps to 0 or 1.
        readonly property int inset: 15

        Rectangle {
            anchors.fill: parent
            color: Style.panel
            radius: Style.cornerPanel
            border.width: app.focusId === gridItem.focusId ? 2 : 0
            border.color: Style.text
        }

        Canvas {
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
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            width: gridItem.width - 16
        }

        Rectangle {
            id: point
            visible: gridItem.bound
            width: 30
            height: 30
            radius: 15
            x: gridItem.inset + gridItem.px * (gridItem.width - 2 * gridItem.inset) - width / 2
            y: gridItem.inset + (1.0 - gridItem.py) * (gridItem.height - 2 * gridItem.inset) - height / 2
            color: Style.accent
            border.width: 2
            border.color: Style.panelDark
            opacity: 0.9
        }

        // Unbound slot: name the state and offer the slot's picker, so the
        // screen never shows a grid that cannot move anything.
        Text {
            visible: !gridItem.bound
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40
            width: gridItem.width - 32
            text: gridItem.note
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        TouchButton {
            visible: !gridItem.bound
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: values.top
            anchors.bottomMargin: Style.gap
            width: 200
            height: Style.touch
            text: "PICK EFFECT"
            textSize: Style.fontSmall
            chevron: "down"
            focusId: gridItem.focusId + ".pick"
            onClicked: app.openFxSlot(gridItem.gridIndex)
        }

        Rectangle {
            visible: gridItem.frozenGrid
            anchors.centerIn: parent
            width: 116
            height: 26
            radius: Style.corner
            color: Style.accent
            Text {
                anchors.centerIn: parent
                text: "FROZEN"
                color: Style.panelDark
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
        }

        Row {
            id: values
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            height: 18
            Text {
                width: parent.width / 2
                height: parent.height
                text: gridItem.bound ? gridItem.axisX + " " + gridItem.xValue
                                     : gridItem.axisX
                color: Style.text
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width / 2
                height: parent.height
                text: gridItem.bound ? gridItem.axisY + " " + gridItem.yValue
                                     : gridItem.axisY
                color: Style.text
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }
        }

        // Touch: every point on this grid is forwarded with its Qt point id, so
        // one finger here and one on the other grid are two independent points.
        MultiPointTouchArea {
            anchors.fill: parent
            enabled: gridItem.bound
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
            enabled: gridItem.bound
            onTapped: (eventPoint) => {
                const p = eventPoint.position
                xy.setPoint(gridItem.gridIndex,
                            (p.x - gridItem.inset) / (gridItem.width - 2 * gridItem.inset),
                            1.0 - (p.y - gridItem.inset) / (gridItem.height - 2 * gridItem.inset))
            }
        }
    }
}
