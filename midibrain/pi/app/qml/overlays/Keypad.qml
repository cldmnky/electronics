import QtQuick
import Mnkybox 1.0
import "../components"

// Touch keyboard for project and take names: digits + QWERTY + -/_ + space,
// cursor movement, BACK, CLEAR, CANCEL and OK. Every key keeps the 44 px touch
// size. Validation runs in the controller; an error keeps the overlay open.
Rectangle {
    id: root

    anchors.fill: parent
    color: "#9e000000"
    visible: app.keypadOpen
    z: 100

    property string buffer: ""
    property int cursor: 0

    function insert(text) {
        if (buffer.length >= 48) return
        buffer = buffer.substring(0, cursor) + text + buffer.substring(cursor)
        cursor += text.length
    }
    function backspace() {
        if (cursor <= 0) return
        buffer = buffer.substring(0, cursor - 1) + buffer.substring(cursor)
        cursor -= 1
    }

    Connections {
        target: app
        function onKeypadChanged() {
            if (app.keypadOpen) {
                root.buffer = app.keypadInitial
                root.cursor = root.buffer.length
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: app.keypadCancel()
    }

    Rectangle {
        id: panel
        width: Math.min(760, root.width - 24)
        height: Math.min(392, root.height - 12)
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: Style.outlineWidth
        border.color: Style.outline

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: Style.gapTight

            Text {
                width: parent.width
                height: 18
                text: app.keypadTitle
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }

            Rectangle {
                width: parent.width
                height: 30
                color: Style.panelDark
                radius: Style.corner
                border.width: 1
                border.color: Style.outline

                Text {
                    id: previewText
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.buffer === "" ? "type a name" : root.buffer
                    color: root.buffer === "" ? Style.textDim : Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    width: parent.width - 20
                }
                Rectangle {
                    visible: root.buffer !== ""
                    x: previewText.x + previewMetrics.width
                    y: parent.height / 2 - 9
                    width: 2
                    height: 18
                    color: Style.accent
                }
                TextMetrics {
                    id: previewMetrics
                    font: previewText.font
                    text: root.buffer.substring(0, root.cursor)
                }
            }

            Text {
                width: parent.width
                height: app.keypadError === "" ? 0 : 16
                visible: app.keypadError !== ""
                text: app.keypadError
                color: Style.record
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Repeater {
                model: ["1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-_<>/."]
                delegate: Row {
                    id: keyRow
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    property string rowText: modelData

                    Repeater {
                        model: keyRow.rowText.length
                        delegate: TouchButton {
                            width: (keyRow.width - (keyRow.rowText.length - 1) * 3) /
                                   keyRow.rowText.length
                            height: keyRow.height
                            text: keyRow.rowText.charAt(index)
                            textSize: 16
                            onClicked: {
                                const character = keyRow.rowText.charAt(index)
                                if (character === "<") {
                                    if (root.cursor > 0) root.cursor -= 1
                                } else if (character === ">") {
                                    if (root.cursor < root.buffer.length) root.cursor += 1
                                } else {
                                    root.insert(character)
                                }
                            }
                        }
                    }
                }
            }

            Row {
                id: actionRow
                width: parent.width
                height: Style.touch
                spacing: 3

                readonly property real unit: (width - spacing * 7) / 8

                TouchButton {
                    width: actionRow.unit * 2
                    height: actionRow.height
                    text: "SPACE"
                    textSize: Style.fontSmall
                    onClicked: root.insert(" ")
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: "<"
                    textSize: 18
                    onClicked: if (root.cursor > 0) root.cursor -= 1
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: ">"
                    textSize: 18
                    onClicked: if (root.cursor < root.buffer.length) root.cursor += 1
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: "BACK"
                    textSize: Style.fontSmall
                    onClicked: root.backspace()
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: "CLEAR"
                    textSize: Style.fontSmall
                    onClicked: { root.buffer = ""; root.cursor = 0 }
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: "CANCEL"
                    textSize: Style.fontSmall
                    variant: "ghost"
                    onClicked: app.keypadCancel()
                }
                TouchButton {
                    width: actionRow.unit
                    height: actionRow.height
                    text: "OK"
                    variant: "primary"
                    textSize: Style.fontBody
                    onClicked: app.keypadAccept(root.buffer)
                }
            }
        }
    }
}
