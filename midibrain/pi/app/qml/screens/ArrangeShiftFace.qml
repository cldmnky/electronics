import QtQuick
import Mnkybox 1.0
import "../components"

// ARRANGE SHIFT face: section verbs plus grid/zoom. The primary face keeps the
// section band, the lanes and the clip sheet; this face is the panel-friendly
// verb page (every stop is a 44 px touch target).
Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Text {
            width: parent.width
            height: 18
            text: "SHIFT > ARRANGE - SECTIONS, GRID, ZOOM"
            color: Style.accent
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "ADD SECTION"
                textSize: Style.fontSmall
                focusId: "shift.arrange.addsection"
                onClicked: arrange.addSection()
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "DUP SECTION"
                textSize: Style.fontSmall
                focusId: "shift.arrange.dupsection"
                onClicked: arrange.duplicateSection()
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "LOOP SECTION"
                textSize: Style.fontSmall
                focusId: "shift.arrange.loopsection"
                onClicked: arrange.loopSection()
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                focusId: "shift.arrange.rename"
                onClicked: app.requestArrangeSectionRename()
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            TouchButton {
                width: 120
                height: parent.height
                text: "LEN " + arrange.lengthText
                textSize: Style.fontSmall
                focusId: "shift.arrange.length"
                onClicked: arrange.openLengthPopup()
            }
            TouchButton {
                width: 100
                height: parent.height
                text: "LOOP " + (arrange.loopOn ? "ON" : "OFF")
                textSize: Style.fontSmall
                checked: arrange.loopOn
                focusId: "shift.arrange.loop"
                onClicked: arrange.setLoopOn(!arrange.loopOn)
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "Z-"
                textSize: Style.fontSmall
                focusId: "shift.arrange.zoomout"
                onClicked: arrange.zoomOut()
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "Z+"
                textSize: Style.fontSmall
                focusId: "shift.arrange.zoomin"
                onClicked: arrange.zoomIn()
            }
            TouchButton {
                width: parent.width - 120 - 100 - 84 - 84 - Style.gapTight * 4
                height: parent.height
                text: "RENDER SONG"
                textSize: Style.fontSmall
                variant: "primary"
                focusId: "shift.arrange.render"
                onClicked: app.renderSong()
            }
        }

        Text {
            width: parent.width
            height: 16
            text: "GRID"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "BAR"
                textSize: Style.fontSmall
                checked: arrange.grid === 0
                focusId: "shift.arrange.grid.0"
                onClicked: arrange.setGrid(0)
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "1/2"
                textSize: Style.fontSmall
                checked: arrange.grid === 1
                focusId: "shift.arrange.grid.1"
                onClicked: arrange.setGrid(1)
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "1/4"
                textSize: Style.fontSmall
                checked: arrange.grid === 2
                focusId: "shift.arrange.grid.2"
                onClicked: arrange.setGrid(2)
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 3) / 4
                height: parent.height
                text: "1/16"
                textSize: Style.fontSmall
                checked: arrange.grid === 3
                focusId: "shift.arrange.grid.3"
                onClicked: arrange.setGrid(3)
            }
        }

        Text {
            width: parent.width
            text: "SONG " + (app.songMode ? "ON - the arrangement plays" : "OFF - free take loops") +
                  "  -  GRID " + arrange.gridText + "  -  LEN " + arrange.lengthText +
                  "  -  " + arrange.loopText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: "Section verbs duplicate the whole section (markers and clips) and shift the material after it. LOOP SECTION sets the loop window to the selected section; LOOP ON with the whole song selected loops the song. Punch markers are loop-relative when SONG mode is on."
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
