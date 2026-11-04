import QtQuick
import Mnkybox 1.0
import "../components"

// Recorder: Pico transport readout, 18 arm toggles, take start/stop, rename and
// delete, and the take list scanned from the take directory. The take directory
// chooser from the JUCE app is not carried over: the default Music folder is
// shown and stays fixed until a directory picker is added.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 6

            Text {
                width: parent.width * 0.62
                height: parent.height
                text: recorder.transportText
                color: recorder.recording ? Style.record
                       : (app.rolling ? Style.accent : Style.text)
                font.pixelSize: 24
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                width: parent.width - parent.width * 0.62 - 6
                height: parent.height
                text: recorder.clockText
                color: Style.textDim
                font.pixelSize: Style.fontBody
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Item {
            width: parent.width
            height: Style.touch * 2 + 3

            Repeater {
                model: recorder.arms
                delegate: TouchButton {
                    width: Math.max(Style.touch, (screen.width - Style.margin * 2) / 9)
                    height: Style.touch
                    x: (index % 9) * width
                    y: Math.floor(index / 9) * Style.touch
                    text: String(index + 1)
                    textSize: Style.fontSmall
                    checked: model.armed
                    checkedColor: Style.record
                    focusId: "recorder.arm." + index
                    onClicked: mixer.toggleArm(index)
                }
            }
        }

        Item {
            width: parent.width
            height: Style.touch

            TouchButton {
                width: 150
                height: parent.height
                text: recorder.buttonText
                textSize: Style.fontBody
                idleColor: "#7f2b2a"
                focusId: "recorder.start"
                onClicked: recorder.startStop()
            }
            TouchButton {
                x: 156
                width: 90
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                active: !recorder.recording && recorder.selectedRow >= 0
                focusId: "recorder.rename"
                onClicked: app.requestRenameTake()
            }
            TouchButton {
                x: 252
                width: 90
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                active: !recorder.recording && recorder.selectedRow >= 0
                idleColor: "#5b2222"
                focusId: "recorder.delete"
                onClicked: app.requestDeleteTake()
            }
            Text {
                x: 350
                width: parent.width - 350
                height: parent.height
                text: recorder.statusText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            width: parent.width
            height: 16
            text: recorder.diskText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }

        ListView {
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 - Style.touch * 3 - 19 - 12)
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

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    width: parent.width / 2
                    height: parent.height
                    text: model.name
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width: parent.width / 2
                    height: parent.height
                    text: model.files + " files  -  " + model.duration + "  -  " + model.bytes
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

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Style.margin
        width: parent.width - Style.margin * 2
        height: 14
        text: "takes: " + recorder.takeDirectory
        color: Style.textDim
        font.pixelSize: 10
        elide: Text.ElideMiddle
    }
}
