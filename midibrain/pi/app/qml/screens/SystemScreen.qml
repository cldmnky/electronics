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
        spacing: Style.gap

        Row {
            width: parent.width
            height: 28
            spacing: Style.gap
            Wordmark {
                width: 118
                height: parent.height
            }
            Text {
                width: parent.width - 126 - 250 - Style.gap
                height: parent.height
                text: "SYSTEM  -  link, clock, audio devices"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            // B10: the primary face names what its SHIFT face holds, so the
            // metronome, MIDI learn and TOOLS are not a hidden second screen.
            Text {
                width: 250
                height: parent.height
                text: "SHIFT > metronome - MIDI learn - TOOLS"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // No-Pico banner: which subsystems are live on a bare Pi and which are
        // physically absent (Pico-owned TRS/panel/LEDs). Hidden in hardware
        // mode, so the layout is unchanged there.
        Rectangle {
            id: noPicoBanner
            width: parent.width
            height: system.noPicoMode ? 58 : 0
            visible: system.noPicoMode
            color: Style.panel
            border.width: Style.outlineWidth
            border.color: Style.record
            radius: Style.corner

            Text {
                anchors.fill: parent
                anchors.margins: 5
                text: system.noPicoText
                color: Style.record
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 3
                verticalAlignment: Text.AlignVCenter
            }
        }

        Row {
            width: parent.width
            height: parent.height - 28 - Style.gap -
                    (noPicoBanner.visible ? noPicoBanner.height + Style.gap : 0)
            spacing: Style.gap
        Flickable {
            id: systemLeft
            objectName: "systemLeft"
            width: (parent.width - Style.gap) * 0.48
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
                    text: "AUDIO BACKEND"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    height: 12
                    text: "OUTPUT DEVICE - name / backend / channels"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                }

                DeviceComboBox {
                    id: backendBox
                    objectName: "systemOutputList"
                    width: parent.width
                    model: system.outputRows
                    focusId: "system.backend"
                    onActivated: (index) => system.backendIndex = index
                    Binding on currentIndex { value: system.backendIndex; when: !backendBox.popup.visible }
                }

                DeviceComboBox {
                    id: inputBox
                    width: parent.width
                    model: system.inputRows
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

                // No APPLY: the pickers above reopen the stream on every pick.
                // This row is the live outcome - the fallback note when the
                // requested device could not open, plus the rescan button.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: parent.width - 96
                        height: parent.height
                        text: system.applyNote !== "" ? system.applyNote
                                                      : "live: a picker change reopens audio"
                        color: system.applyNote !== "" ? Style.record : Style.textDim
                        font.pixelSize: Style.fontSmall
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: "REFRESH"
                        textSize: Style.fontSmall
                        focusId: "system.refresh"
                        onClicked: system.refreshDevices()
                    }
                }

                // Device output routing (session v2 deviceOut): which device
                // channel pair carries Main 1&2, OUT3&4 and the phones bus.
                // Defaults 1&2, 3&4, 5&6. The pairs are saved with the project;
                // the audio backend does not map them onto the interface yet.
                // D6: the three picks are stored, not applied. The badge says
                // so once and explains it on tap instead of hiding the caveat
                // in a 10 px note.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    Text {
                        width: parent.width - 116 - Style.gapTight
                        height: parent.height
                        text: "DEVICE OUT CHANNELS"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    TouchButton {
                        width: 116
                        height: Style.touch
                        text: "SAVED ONLY"
                        textSize: Style.fontSmall
                        variant: "secondary"
                        focusId: "system.devout.saved"
                        onClicked: app.showTextPopup(
                            "DEVICE OUT CHANNELS",
                            "Saved with the project (session deviceOut) and reloaded unchanged. " +
                            "The audio backend does not map these pairs onto the interface " +
                            "channels yet, so Main 1&2, OUT3&4 and the phones bus keep the " +
                            "device default.")
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: 84
                        height: parent.height
                        text: "MAIN 1&2"
                        color: Style.text
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchComboBox {
                        id: devoutMainBox
                        opacity: 0.55
                        objectName: "systemDevoutList"
                        width: parent.width - 88
                        height: parent.height
                        model: session.deviceOutPairLabels
                        focusId: "system.devout.main"
                        onActivated: (index) => {
                            session.deviceOutMain = index
                            app.showTextPopup("MAIN 1&2 DEVICE OUT", session.deviceOutPairLabels[index])
                        }
                        Binding on currentIndex { value: session.deviceOutMain; when: !devoutMainBox.popup.visible }
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: 84
                        height: parent.height
                        text: "OUT3&4"
                        color: Style.text
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchComboBox {
                        id: devoutOut34Box
                        opacity: 0.55
                        width: parent.width - 88
                        height: parent.height
                        model: session.deviceOutPairLabels
                        focusId: "system.devout.out34"
                        onActivated: (index) => {
                            session.deviceOutOut34 = index
                            app.showTextPopup("OUT3&4 DEVICE OUT", session.deviceOutPairLabels[index])
                        }
                        Binding on currentIndex { value: session.deviceOutOut34; when: !devoutOut34Box.popup.visible }
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: 84
                        height: parent.height
                        text: "PHONES"
                        color: Style.text
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchComboBox {
                        id: devoutPhonesBox
                        opacity: 0.55
                        width: parent.width - 88
                        height: parent.height
                        model: session.deviceOutPairLabels
                        focusId: "system.devout.phones"
                        onActivated: (index) => {
                            session.deviceOutPhones = index
                            app.showTextPopup("PHONES DEVICE OUT", session.deviceOutPairLabels[index])
                        }
                        Binding on currentIndex { value: session.deviceOutPhones; when: !devoutPhonesBox.popup.visible }
                    }
                }

                Text {
                    width: parent.width
                    height: 16
                    text: "session deviceOut - tap SAVED ONLY for what the backend does"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
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
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: system.backendHint
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }

        Column {
            width: parent.width - (parent.width - Style.gap) * 0.48 - Style.gap
            height: parent.height
            spacing: 3

            Text {
                width: parent.width
                height: 16
                text: "LINK / CLOCK / TRANSPORT"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
            TextArea {
                width: parent.width
                height: parent.height * 0.55
                readOnly: true
                text: system.statsText
                color: Style.text
                font.family: "monospace"
                font.pixelSize: Style.fontSmall
                wrapMode: TextEdit.NoWrap
                clip: true
                background: Rectangle {
                    color: Style.panelDark
                    radius: Style.corner
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
                font.weight: Font.DemiBold
            }
            TextArea {
                width: parent.width
                height: Math.max(40, parent.height - parent.height * 0.55 - 38)
                readOnly: true
                text: system.logText
                color: Style.textDim
                font.family: "monospace"
                font.pixelSize: Style.fontSmall
                wrapMode: TextEdit.NoWrap
                clip: true
                background: Rectangle {
                    color: Style.panelDark
                    radius: Style.corner
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
