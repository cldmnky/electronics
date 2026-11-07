import QtQuick
import Mnkybox 1.0
import "../components"

// Full channel popout: the strip's expand affordance opens every control of
// one channel in one place - fader, pan, switches, sends, monitor, I/O, the FX
// rack entry and the EQ band summary - so a mixer move no longer needs the TRACK
// tab and the SHIFT face. Every field is live (no apply button); CLOSE only
// closes, REMOVE TRACK asks for confirmation and leaves the slot's settings and
// takes in the project. The TRACK tab stays as the compact single-channel face.
// FX1/FX2 are the per-track rack slots' wet; TRACK FX opens the rack popup.
Rectangle {
    id: root

    anchors.fill: parent
    visible: mixer.channelOpen
    color: "#9e000000"
    z: 90

    // Swallow touches outside the panel; the channel popout never dismisses on a
    // stray touch, exactly like the config popup.
    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        id: panel
        width: Math.min(760, root.width - 16)
        height: root.height - 16
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: Style.gapTight

            Row {
                width: parent.width
                // Stage 4 sheet header: the CLOSE control is 64 px and the
                // title sits at the same 12 px padding as every other sheet.
                height: Style.touchPrimary
                spacing: Style.gapTight

                Text {
                    width: parent.width - 160 - 92 - Style.gapTight * 2
                    height: parent.height
                    text: "CHANNEL  " + mixer.trackName
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // A4 entry point 1: the visible rename affordance, right of the
                // name. The strip's 600 ms name hold is the fast path.
                TouchButton {
                    width: 92
                    height: parent.height
                    text: "RENAME"
                    textSize: Style.fontSmall
                    variant: "ghost"
                    focusId: "channel.rename"
                    onClicked: app.requestRenameTrack(mixer.channelSlot)
                }
                TouchButton {
                    width: 160
                    height: parent.height
                    text: "CLOSE"
                    textSize: Style.fontLabel
                    variant: "primary"
                    focusId: "channel.close"
                    onClicked: app.closeChannel()
                }
            }

            Row {
                width: parent.width
                height: parent.height - Style.touchPrimary - Style.gapTight
                spacing: Style.gap

                // ---- left column: the strip --------------------------------
                Column {
                    width: (parent.width - Style.gap) * 0.36
                    height: parent.height
                    spacing: Style.gapTight

                    Row {
                        width: parent.width
                        height: parent.height - 4 * Style.touch - Style.gapTight * 4
                        spacing: 4

                        TouchSlider {
                            id: channelGain
                            width: parent.width - 26
                            height: parent.height
                            vertical: true
                            minimumValue: -60
                            maximumValue: 12
                            value: mixer.trackGainDb
                            focusId: "channel.gain"
                            popupLabel: mixer.trackName + " GAIN"
                            popupKind: "db"
                            Binding on value { value: mixer.trackGainDb; when: !channelGain.dragging }
                            onMoved: (value) => mixer.setTrackGainDb(value)
                        }
                        Column {
                            width: 22
                            height: parent.height
                            spacing: 4
                            Meter {
                                width: parent.width
                                height: parent.height - 18
                                level: mixer.trackLevel
                                clip: mixer.trackClip
                                vertical: true
                            }
                            Text {
                                width: parent.width
                                height: 14
                                // W1: the mode word, never a bare letter on the
                                // channel itself (the strip column keeps the
                                // three-letter form because it is 44 px).
                                text: mixer.selectedModeWord.substring(0, 3)
                                color: mixer.selectedMode === 1 ? Style.record
                                       : (mixer.selectedMode === 0 ? Style.textDim : Style.accent)
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: Style.touch
                        spacing: 4
                        KnobChip {
                            width: parent.width - 54
                            height: parent.height
                            label: "PAN"
                            value: mixer.trackPan
                            valueText: mixer.trackPanText
                            minimumValue: 0
                            maximumValue: 1
                            focusId: "channel.pan"
                            popupLabel: mixer.trackName + " PAN"
                            popupKind: "pan"
                            onMoved: (value) => mixer.setTrackPan(value)
                        }
                        Text {
                            width: 42
                            height: parent.height
                            text: mixer.trackPanText
                            color: Style.text
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Row {
                        width: parent.width
                        height: Style.touch
                        spacing: 4
                        TouchButton {
                            width: (parent.width - 8) / 3
                            height: parent.height
                            text: "MUTE"
                            textSize: Style.fontSmall
                            checked: mixer.trackMute
                            checkedColor: Style.textDim
                            checkedTextColor: Style.text
                            focusId: "channel.mute"
                            onClicked: mixer.setTrackMute(!mixer.trackMute)
                        }
                        TouchButton {
                            width: (parent.width - 8) / 3
                            height: parent.height
                            text: "SOLO"
                            textSize: Style.fontSmall
                            checked: mixer.trackSolo
                            checkedColor: Style.ok
                            checkedTextColor: Style.panelDark
                            focusId: "channel.solo"
                            onClicked: mixer.setTrackSolo(!mixer.trackSolo)
                        }
                        TouchButton {
                            width: (parent.width - 8) / 3
                            height: parent.height
                            text: "ARM"
                            textSize: Style.fontSmall
                            checked: mixer.trackArm
                            checkedColor: Style.record
                            focusId: "channel.arm"
                            onClicked: mixer.setTrackArm(!mixer.trackArm)
                        }
                    }

                    Row {
                        width: parent.width
                        height: Style.touch
                        spacing: 4
                        KnobChip {
                            width: parent.width
                            height: parent.height
                            label: "TRIM"
                            value: mixer.trackTrimDb
                            valueText: Number(mixer.trackTrimDb).toFixed(1)
                            suffix: " dB"
                            minimumValue: -24
                            maximumValue: 24
                            focusId: "channel.trim"
                            popupLabel: mixer.trackName + " TRIM"
                            popupKind: "db"
                            onMoved: (value) => mixer.setTrackTrimDb(value)
                        }
                    }

                    TouchButton {
                        width: parent.width
                        height: Style.touch
                        text: "REMOVE TRACK"
                        textSize: Style.fontSmall
                        variant: "danger"
                        focusId: "channel.remove"
                        onClicked: app.requestRemoveTrack(mixer.trackIndex)
                    }
                }

                // ---- right column: sends/FX wet | monitor, I/O, EQ ---------
                // Two sub-columns keep every control inside the panel at 480 px:
                // the FX wet and monitor-send chips on the left, the assignment
                // and the TRACK FX entry on the right.
                Row {
                    width: parent.width - (parent.width - Style.gap) * 0.36 - Style.gap
                    height: parent.height
                    spacing: Style.gap

                    Column {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: Style.gapTight

                        Text {
                            width: parent.width
                            height: 16
                            text: "SENDS / FX WET"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                        }
                        ChannelSliderRow {
                            width: parent.width
                            label: "FX1 WET"
                            value: mixer.trackFxWet1
                            focusId: "channel.fx1"
                            onMoved: (value) => mixer.setTrackFxWet1(value)
                        }
                        ChannelSliderRow {
                            width: parent.width
                            label: "FX2 WET"
                            value: mixer.trackFxWet2
                            focusId: "channel.fx2"
                            onMoved: (value) => mixer.setTrackFxWet2(value)
                        }
                        ChannelSliderRow {
                            width: parent.width
                            label: "CUE"
                            value: mixer.trackCue
                            focusId: "channel.cue"
                            onMoved: (value) => mixer.setTrackCue(value)
                        }
                        ChannelSliderRow {
                            width: parent.width
                            label: "OUT34"
                            value: mixer.trackOut34
                            focusId: "channel.out34"
                            onMoved: (value) => mixer.setTrackOut34(value)
                        }
                        Text {
                            width: parent.width
                            text: "Every control applies immediately; FX1/FX2 are this " +
                                  "track's rack slot wet, in series."
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    Column {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: Style.gapTight

                        Text {
                            width: parent.width
                            height: 16
                            text: "TRACK MODE"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                        }
                        Row {
                            width: parent.width
                            height: Style.touch
                            spacing: 4
                            TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "PLAY"; textSize: Style.fontSmall; checked: mixer.selectedMode === 0; focusId: "channel.mode.0"; onClicked: mixer.setSelectedMode(0) }
                            TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "ARM"; textSize: Style.fontSmall; checked: mixer.selectedMode === 1; checkedColor: Style.record; focusId: "channel.mode.1"; onClicked: mixer.setSelectedMode(1) }
                            TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "OVERDUB"; textSize: 10; checked: mixer.selectedMode === 2; focusId: "channel.mode.2"; onClicked: mixer.setSelectedMode(2) }
                            TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "LIVE"; textSize: Style.fontSmall; checked: mixer.selectedMode === 3; focusId: "channel.mode.3"; onClicked: mixer.setSelectedMode(3) }
                        }
                        // REC SOURCE (W2b): the ARM-area selector names where the
                        // take taps the channel; PRE is the dry capture.
                        Row {
                            width: parent.width
                            height: Style.touch
                            spacing: 4
                            TouchButton {
                                width: 96
                                height: parent.height
                                text: "REC SRC"
                                textSize: Style.fontSmall
                                variant: "ghost"
                                chevron: "down"
                                focusId: "channel.recsource"
                                onClicked: app.openTrackMode(mixer.trackIndex)
                            }
                            Text {
                                width: parent.width - 100
                                height: parent.height
                                text: "RECORDING TAPS " + mixer.selectedRecSourceText
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            width: parent.width
                            height: Style.touch
                            spacing: 4
                            TouchButton {
                                width: parent.width - 64 - 4
                                height: parent.height
                                text: mixer.trackInputText
                                textSize: Style.fontSmall
                                variant: "ghost"
                                chevron: "down"
                                focusId: "channel.input"
                                onClicked: app.openInputSource(mixer.trackIndex)
                            }
                            TouchButton {
                                width: 64
                                height: parent.height
                                text: mixer.trackInPair ? "PAIR" : "MONO"
                                textSize: Style.fontSmall
                                checked: mixer.trackInPair
                                onClicked: mixer.setTrackInPair(!mixer.trackInPair)
                            }
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "OUTPUT  " + mixer.trackOutputText
                            textSize: Style.fontSmall
                            variant: "ghost"
                            chevron: "down"
                            focusId: "channel.output"
                            onClicked: app.openOutputDest(mixer.trackIndex)
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "TRACK FX   " + mixer.channelFxSummary
                            textSize: Style.fontSmall
                            chevron: "up"
                            focusId: "channel.fx"
                            onClicked: app.openTrackFx(0)
                        }

                        Text {
                            width: parent.width
                            height: 16
                            text: "EQ BANDS"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            height: 46
                            text: mixer.channelEqSummary
                            color: Style.text
                            font.pixelSize: Style.fontSmall
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // A wet or send level in a narrow sub-column: an arc-dial chip with its own
    // readout, per the refresh law (no slider outside a wide fader).
    component ChannelSliderRow: KnobChip {
        height: Style.touch
        valueText: Math.round(value * 100)
        suffix: "%"
        minimumValue: 0
        maximumValue: 1
        popupKind: "percent"
    }
}
