import QtQuick
import Mnkybox 1.0
import "../components"

// RECORDER primary: transport, track selection, arm/start and the take list.
// SHIFT: take management (import/live/active/rename/delete) and punch markers.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 6

            Text {
                width: parent.width * 0.55
                height: parent.height
                text: recorder.transportText
                color: recorder.recording ? Style.record
                       : (app.rolling ? Style.accent : Style.text)
                font.pixelSize: 22
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                width: parent.width - parent.width * 0.55 - 6
                height: parent.height
                text: "ACTIVE  " + recorder.activeTake
                color: recorder.activeTake === "LIVE INPUT" ? Style.textDim : Style.accent
                font.pixelSize: Style.fontSmall
                font.bold: true
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }
        }

        Item {
            width: parent.width
            height: Style.touch * 2

            Repeater {
                model: 18
                delegate: TouchButton {
                    width: parent.width / 9
                    height: Style.touch
                    x: (index % 9) * width
                    y: Math.floor(index / 9) * Style.touch
                    text: String(index + 1).padStart(2, "0")
                    textSize: Style.fontSmall
                    checked: index === recorder.selectedTrack
                    focusId: "recorder.track." + index
                    onClicked: recorder.setSelectedTrack(index)
                }
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 3

            Text {
                width: 120
                height: parent.height
                text: recorder.trackName
                color: Style.text
                font.pixelSize: Style.fontBody
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            TouchButton {
                width: 70
                height: parent.height
                text: "ARM"
                textSize: Style.fontSmall
                checked: mixer.trackArm
                checkedColor: Style.record
                focusId: "recorder.arm"
                onClicked: mixer.toggleArm(recorder.selectedTrack)
            }
            TouchButton {
                width: 140
                height: parent.height
                text: recorder.buttonText
                textSize: Style.fontSmall
                idleColor: "#7f2b2a"
                focusId: "recorder.start"
                onClicked: recorder.startStop()
            }
            Text {
                width: parent.width - 330
                height: parent.height
                text: "SHIFT: takes + punch"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            width: parent.width
            height: 16
            text: recorder.statusText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            height: 14
            text: recorder.diskText + "  -  " + recorder.takeDirectory
            color: Style.textDim
            font.pixelSize: 10
            elide: Text.ElideMiddle
        }

        ListView {
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 - 44 - 88 - 44 - 34 - 15)
            clip: true
            model: recorder.takes
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: ListView.view.width
                height: Style.touch
                color: index === recorder.selectedRow ? Style.outline
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === ("recorder.take." + index) ? 2 : 0
                border.color: Style.accent

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 5
                    color: model.active ? Style.accent : "transparent"
                }
                Text {
                    x: 10
                    width: 56
                    height: parent.height
                    text: model.active ? "ACTIVE" : ""
                    color: Style.accent
                    font.pixelSize: 10
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    x: 72
                    width: parent.width * 0.52
                    height: parent.height
                    text: (model.imported ? "PTR  " : "") + model.name
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width: parent.width * 0.38
                    height: parent.height
                    text: model.duration + "  -  " + model.bytes
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TapHandler {
                    onTapped: recorder.selectRow(index)
                }
            }
        }
    }

    // SHIFT: management + punch
    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 3

            Text {
                width: 108
                height: parent.height
                text: "TAKES + PUNCH"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: 76
                height: parent.height
                text: "IMPORT"
                textSize: 10
                focusId: "shift.recorder.import"
                onClicked: app.requestImportTake()
            }
            TouchButton {
                width: 64
                height: parent.height
                text: "LIVE"
                textSize: 10
                checked: recorder.activeTake === "LIVE INPUT"
                focusId: "shift.recorder.live"
                onClicked: recorder.clearActive()
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "ACTIVE"
                textSize: 10
                active: recorder.selectedRow >= 0
                focusId: "shift.recorder.activate"
                onClicked: recorder.activateSelected()
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "RENAME"
                textSize: 10
                active: !recorder.recording && recorder.selectedRow >= 0
                focusId: "shift.recorder.rename"
                onClicked: app.requestRenameTake()
            }
            TouchButton {
                width: 68
                height: parent.height
                text: "DELETE"
                textSize: 10
                active: !recorder.recording && recorder.selectedRow >= 0
                idleColor: "#5b2222"
                focusId: "shift.recorder.delete"
                onClicked: app.requestDeleteTake()
            }
        }

        PunchScreen {
            width: parent.width
            height: parent.height - Style.touch - 3
            focusPrefix: "shift.punch"
            compact: true
        }
    }
}
