import QtQuick
import QtQuick.Controls
import Mnkybox 1.0
import "../components"

// System: link/clock/READY status, Qt Multimedia backend/device selector,
// metronome configuration, MIDI-learn capture and the in-app log tail.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        // No-Pico banner: which subsystems are live on a bare Pi and which are
        // physically absent (Pico-owned TRS/panel/LEDs). Hidden in hardware
        // mode, so the layout is unchanged there.
        Rectangle {
            id: noPicoBanner
            width: parent.width
            height: system.noPicoMode ? 46 : 0
            visible: system.noPicoMode
            color: Style.panelDark
            border.width: Style.outlineWidth
            border.color: Style.record
            radius: Style.corner

            Text {
                anchors.fill: parent
                anchors.margins: 5
                text: system.noPicoText
                color: Style.record
                font.pixelSize: 10
                font.bold: true
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 3
                verticalAlignment: Text.AlignVCenter
            }
        }

        Row {
            width: parent.width
            height: parent.height - (noPicoBanner.visible ? noPicoBanner.height + 4 : 0)
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
    SystemShiftFace {
        visible: screen.shiftFace
        anchors.fill: parent
    }
}
