import QtQuick
import QtQuick.Controls
import Mnkybox 1.0
import "../components"

// System: link/clock/READY status, Qt Multimedia backend/device selector,
// metronome configuration, MIDI-learn capture and the in-app log tail.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"
    // The live audio line is "LIVE  <name>   <rate> Hz / <frames> frames", so
    // the rate/buffer are field 1 and the raw REQUEST/EFFECTIVE note stays on
    // DIAGNOSTICS.
    readonly property var liveFields: system.liveText.replace("LIVE  ", "").split("   ")

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
                width: parent.width - 126 - 132 - Style.gap * 2
                height: parent.height
                text: "SYSTEM  -  audio, clock, link"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            // Discoverability: the raw troubleshooting surface is one tap from
            // the primary page, not hidden behind the SHIFT face's last tab.
            TouchButton {
                width: 132
                height: parent.height
                text: "DIAGNOSTICS"
                textSize: Style.fontSmall
                focusId: "system.diagnostics"
                onClicked: app.openDiagnostics()
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
                        text: system.applyNote
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
                // the three picks are stored, not applied. The badge says
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

            }
        }

        Column {
            width: parent.width - (parent.width - Style.gap) * 0.48 - Style.gap
            height: parent.height
            spacing: Style.gapTight

            // readable summaries, not the raw protocol dump. The
            // counters and the log tail live on DIAGNOSTICS (SHIFT > DIAG).
            Column {
                width: parent.width
                spacing: 1
                Text {
                    width: parent.width
                    text: "AUDIO DEVICE"
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    height: Style.fontValue + 4
                    text: screen.liveFields[0]
                    color: Style.text
                    font.pixelSize: Style.fontLabel
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    // Rate / buffer, not the raw REQUEST -> EFFECTIVE note: it
                    // was the line that elided mid-sentence on the panel.
                    text: screen.liveFields.length > 1 ? screen.liveFields[1] : ""
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
            }

            Column {
                width: parent.width
                spacing: 1
                Text {
                    width: parent.width
                    text: "CLOCK"
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    height: Style.fontValue + 4
                    text: session.clockSourceText
                    color: Style.text
                    font.pixelSize: Style.fontLabel
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: app.transportState + "  " + app.tempoText
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    elide: Text.ElideRight
                }
            }

            Column {
                width: parent.width
                spacing: 1
                Text {
                    width: parent.width
                    text: "PICO"
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    height: Style.fontValue + 4
                    text: app.linkSummary
                    color: Style.text
                    font.pixelSize: Style.fontLabel
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: system.noPicoMode ? "USB MIDI only - no TRS, panel or LEDs"
                                            : "Panel, LEDs and TRS are wired"
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    elide: Text.ElideRight
                }
            }

            Column {
                width: parent.width
                spacing: 1
                Text {
                    width: parent.width
                    text: "STORAGE"
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    height: Style.fontValue + 4
                    text: tools.diskFreeText
                    color: tools.lowSpace ? Style.record : Style.text
                    font.pixelSize: Style.fontLabel
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: tools.lowSpace ? "LOW SPACE - free the take volume"
                                         : "take volume"
                    color: tools.lowSpace ? Style.record : Style.textDim
                    font.pixelSize: Style.fontSecondary
                    elide: Text.ElideRight
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
