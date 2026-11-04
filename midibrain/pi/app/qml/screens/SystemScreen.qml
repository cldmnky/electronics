import QtQuick
import QtQuick.Controls
import Mnkybox 1.0
import "../components"

// System: link/clock/READY status, Qt Multimedia backend/device selector,
// metronome configuration, MIDI-learn capture and the in-app log tail.
Item {
    id: screen

    Row {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 6

        Flickable {
            id: systemLeft
            objectName: "systemLeft"
            width: (parent.width - 6) * 0.48
            height: parent.height
            clip: true
            contentWidth: width
            contentHeight: leftColumn.height
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AlwaysOn
                width: 8
            }

            Column {
                id: leftColumn
                width: parent.width
                spacing: 3

                Text {
                    width: parent.width
                    height: 16
                    text: "AUDIO BACKEND - Qt Multimedia"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                }

                TouchComboBox {
                    id: backendBox
                    width: parent.width
                    model: system.backendLabels
                    focusId: "system.backend"
                    onActivated: (index) => system.backendIndex = index
                    Binding on currentIndex { value: system.backendIndex; when: !backendBox.popup.visible }
                }

                TouchComboBox {
                    id: inputBox
                    width: parent.width
                    model: system.inputLabels
                    focusId: "system.input"
                    onActivated: (index) => system.inputIndex = index
                    Binding on currentIndex { value: system.inputIndex; when: !inputBox.popup.visible }
                }

                TouchComboBox {
                    id: bufferBox
                    width: parent.width
                    model: system.bufferLabels
                    focusId: "system.buffer"
                    onActivated: (index) => system.bufferIndex = index
                    Binding on currentIndex { value: system.bufferIndex; when: !bufferBox.popup.visible }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    TouchButton {
                        width: (parent.width - 4) / 2
                        height: parent.height
                        text: "APPLY"
                        focusId: "system.apply"
                        onClicked: system.apply()
                    }
                    TouchButton {
                        width: (parent.width - 4) / 2
                        height: parent.height
                        text: "REFRESH"
                        focusId: "system.refresh"
                        onClicked: system.refreshDevices()
                    }
                }

                Text {
                    width: parent.width
                    height: 16
                    text: system.liveText
                    color: Style.text
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: 34
                    text: system.noteText
                    color: Style.textDim
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    height: 16
                    text: "METRONOME - click config"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    TouchButton {
                        width: 84
                        height: parent.height
                        text: "CLICK"
                        checked: app.clickEnabled
                        focusId: "system.click"
                        onClicked: app.setClickEnabled(!app.clickEnabled)
                    }
                    TouchSlider {
                        id: clickLevel
                        width: Math.max(60, parent.width - 232)
                        height: Style.touch
                        minimumValue: 0
                        maximumValue: 1
                        value: app.clickLevel
                        focusId: "system.click.level"
                        Binding on value { value: app.clickLevel; when: !clickLevel.dragging }
                        onMoved: (value) => app.setClickLevel(value)
                    }
                    TouchButton {
                        width: 44
                        height: parent.height
                        text: "-"
                        textSize: 20
                        focusId: "system.click.bars"
                        onClicked: app.setClickBars(app.clickBars - 1)
                    }
                    Text {
                        width: 44
                        height: parent.height
                        text: app.clickBars + " bar"
                        color: Style.text
                        font.pixelSize: Style.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                    }
                    TouchButton {
                        width: 44
                        height: parent.height
                        text: "+"
                        textSize: 20
                        focusId: "system.click.bars"
                        onClicked: app.setClickBars(app.clickBars + 1)
                    }
                }

                Text {
                    width: parent.width
                    height: 16
                    text: "MIDI LEARN - capture a control change"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    TouchComboBox {
                        id: learnTarget
                        width: parent.width - 148
                        model: learn.targets
                        focusId: "learn.target"
                        onActivated: (index) => learn.targetIndex = index
                        Binding on currentIndex { value: learn.targetIndex; when: !learnTarget.popup.visible }
                    }
                    TouchButton {
                        width: 144
                        height: parent.height
                        text: learn.capturing ? "WAIT FOR CC" : "ARM CAPTURE"
                        textSize: Style.fontSmall
                        checked: learn.capturing
                        focusId: "learn.arm"
                        onClicked: learn.capturing ? learn.cancelCapture() : learn.startCapture()
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: parent.width - 88
                        height: parent.height
                        text: learn.statusText
                        color: Style.textDim
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 84
                        height: parent.height
                        text: "CLEAR"
                        textSize: 10
                        focusId: "learn.clear"
                        onClicked: learn.clearAll()
                    }
                }

                Repeater {
                    model: learn.bindings
                    delegate: Row {
                        width: leftColumn.width
                        height: Style.touch
                        spacing: 4

                        Text {
                            width: parent.width - 92
                            height: parent.height
                            text: modelData
                            color: Style.text
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        TouchButton {
                            width: 88
                            height: parent.height
                            text: "REMOVE"
                            textSize: 10
                            checked: false
                            focusId: "learn.binding." + index
                            onClicked: learn.removeBinding(index)
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: system.backendHint
                    color: Style.textDim
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                }
            }
        }

        Column {
            width: parent.width - (parent.width - 6) * 0.48 - 6
            height: parent.height
            spacing: 3

            Text {
                width: parent.width
                height: 16
                text: "LINK / CLOCK / TRANSPORT"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
            }
            TextArea {
                width: parent.width
                height: parent.height * 0.55
                readOnly: true
                text: system.statsText
                color: Style.text
                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: TextEdit.NoWrap
                clip: true
                background: Rectangle {
                    color: Style.panelDark
                    border.width: Style.outlineWidth
                    border.color: Style.outline
                }
            }
            Text {
                width: parent.width
                height: 16
                text: "LOG (last 120 lines)"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
            }
            TextArea {
                width: parent.width
                height: Math.max(40, parent.height - parent.height * 0.55 - 38)
                readOnly: true
                text: system.logText
                color: Style.textDim
                font.family: "monospace"
                font.pixelSize: 10
                wrapMode: TextEdit.NoWrap
                clip: true
                background: Rectangle {
                    color: Style.panelDark
                    border.width: Style.outlineWidth
                    border.color: Style.outline
                }
            }
        }
    }
}
