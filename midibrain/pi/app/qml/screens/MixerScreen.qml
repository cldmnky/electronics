import QtQuick
import Mnkybox 1.0
import "../components"

// Mixer screen with two views of the same strips: MIXER (one setting across
// many tracks) and TRACK (one track with many settings). SHIFT switches to the
// configure face: per-strip Gain/FX/CUE/OUT34/monitor for MIXER, and the
// TRACK I/O, sends and monitor summaries for TRACK. Selection groups of more
// than two choices live in the shared ConfigPopup; the face shows the live
// summary.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: 96
                height: parent.height
                text: "MIXER"
                textSize: Style.fontSmall
                checked: !mixer.trackMode
                focusId: "mixer.mode.0"
                onClicked: mixer.trackMode = false
            }
            TouchButton {
                width: 96
                height: parent.height
                text: "TRACK"
                textSize: Style.fontSmall
                checked: mixer.trackMode
                focusId: "mixer.mode.1"
                onClicked: mixer.trackMode = true
            }
            Row {
                width: screen.shiftFace && !mixer.trackMode ? 176 : 0
                height: parent.height
                spacing: Style.gapTight
                visible: width > 0
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: "1-9"
                    textSize: Style.fontSmall
                    checked: mixer.bank === 0
                    focusId: "mixer.bank.0"
                    onClicked: mixer.setBank(0)
                }
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: "10-18"
                    textSize: Style.fontSmall
                    checked: mixer.bank === 1
                    focusId: "mixer.bank.1"
                    onClicked: mixer.setBank(1)
                }
            }
            Text {
                width: parent.width - 192 - (screen.shiftFace && !mixer.trackMode ? 184 : 0)
                height: parent.height
                text: (screen.shiftFace ? "SHIFT  " : "") +
                      (mixer.trackMode ? mixer.trackName : "one setting across tracks")
                color: screen.shiftFace ? Style.accent : Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // ---- MIXER primary: perform surface -------------------------------
        Item {
            visible: !mixer.trackMode && !screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - Style.gap

            Row {
                anchors.fill: parent
                spacing: Style.gap

                Item {
                    id: stripsArea
                    width: parent.width - Style.masterWidth - Style.gap
                    height: parent.height

                    Repeater {
                        model: mixer
                        delegate: MixerStrip {
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? stripsArea.width / mixer.stripsPerBank : 0
                            height: stripsArea.height
                            x: visible ? (index % mixer.stripsPerBank) * width : 0
                            stripIndex: index
                            gainDb: model.gainDb
                            pan: model.pan
                            panText: model.panText
                            inputText: model.inputText
                            level: model.level
                            clip: model.clip
                            monitor: model.monitor
                            monitorMark: model.monitorMark
                            mute: model.mute
                            solo: model.solo
                            arm: model.arm
                            selected: model.selected
                        }
                    }
                }

                Column {
                    width: Style.masterWidth
                    height: parent.height
                    spacing: Style.gap

                    Row {
                        width: parent.width
                        height: Style.touch
                        spacing: Style.gapTight

                        TouchButton {
                            width: (parent.width - Style.gapTight) / 2
                            height: parent.height
                            text: "1-9"
                            textSize: Style.fontSmall
                            checked: mixer.bank === 0
                            focusId: "mixer.bank.0"
                            onClicked: mixer.setBank(0)
                        }
                        TouchButton {
                            width: (parent.width - Style.gapTight) / 2
                            height: parent.height
                            text: "10-18"
                            textSize: Style.fontSmall
                            checked: mixer.bank === 1
                            focusId: "mixer.bank.1"
                            onClicked: mixer.setBank(1)
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: parent.height - Style.touch - Style.gap
                        color: Style.panel
                        radius: Style.cornerPanel
                        border.width: Style.outlineWidth
                        border.color: Style.outline

                        Column {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: Style.gapTight

                            Text {
                                width: parent.width
                                height: 18
                                text: "MASTER"
                                color: Style.text
                                font.pixelSize: Style.fontTitle
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                            }

                            TouchSlider {
                                id: masterGain
                                width: parent.width
                                height: 108
                                vertical: true
                                minimumValue: -60
                                maximumValue: 12
                                value: mixer.masterDb
                                focusId: "mixer.master"
                                popupLabel: "MASTER"
                                popupKind: "db"
                                Binding on value { value: mixer.masterDb; when: !masterGain.dragging }
                                onMoved: (value) => mixer.setMasterDb(value)
                            }

                            Row {
                                width: parent.width
                                height: Math.max(48, parent.height - 18 - 108 - 8)
                                spacing: 5

                                Repeater {
                                    model: 2
                                    delegate: Meter {
                                        width: (parent.width - 5) / 2
                                        height: parent.height
                                        level: index === 0 ? mixer.meterLeft : mixer.meterRight
                                        clip: (index === 0 ? mixer.meterLeft : mixer.meterRight) > 0.95
                                        vertical: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- MIXER SHIFT: configure sends and monitor ----------------------
        Item {
            visible: !mixer.trackMode && screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - Style.gap

            Column {
                width: parent.width
                height: parent.height
                spacing: Style.gap

                Item {
                    id: shiftStripsArea
                    width: parent.width
                    height: parent.height

                    Repeater {
                        model: mixer
                        delegate: MixerShiftStrip {
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? shiftStripsArea.width / mixer.stripsPerBank : 0
                            height: shiftStripsArea.height
                            x: visible ? (index % mixer.stripsPerBank) * width : 0
                            stripIndex: index
                        }
                    }
                }
            }
        }

        // ---- TRACK primary: essentials -----------------------------------
        Item {
            visible: mixer.trackMode && !screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - Style.gap

            Column {
                width: parent.width
                height: parent.height
                spacing: Style.gap

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
                            checked: index === mixer.trackIndex
                            focusId: "track.select." + index
                            onClicked: mixer.setTrackIndex(index)
                        }
                    }
                }

                TrackSliderRow {
                    width: parent.width
                    label: "VOL"
                    value: mixer.trackGainDb
                    minimumValue: -60
                    maximumValue: 12
                    popupKind: "db"
                    focusId: "track.gain"
                    onMoved: (value) => mixer.setTrackGainDb(value)
                }
                TrackSliderRow {
                    width: parent.width
                    label: "PAN"
                    value: mixer.trackPan
                    minimumValue: 0
                    maximumValue: 1
                    popupKind: "pan"
                    focusId: "track.pan"
                    onMoved: (value) => mixer.setTrackPan(value)
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gap

                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "REC"
                        textSize: Style.fontSmall
                        checked: mixer.trackArm
                        checkedColor: Style.record
                        focusId: "track.rec"
                        onClicked: mixer.toggleArm(mixer.trackIndex)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "MUTE"
                        textSize: Style.fontSmall
                        checked: mixer.trackMute
                        focusId: "track.mute"
                        onClicked: mixer.toggleMute(mixer.trackIndex)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "SOLO"
                        textSize: Style.fontSmall
                        checked: mixer.trackSolo
                        focusId: "track.solo"
                        onClicked: mixer.toggleSolo(mixer.trackIndex)
                    }
                }
            }
        }

        // ---- TRACK SHIFT: configure + assignment --------------------------
        Item {
            visible: mixer.trackMode && screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - Style.gap

            Column {
                width: parent.width
                height: parent.height
                spacing: Style.gap

                // Per-track I/O assignment (session v2 inCh/inPair/outDst). The
                // left picker walks none / IN 01..18 with the PAIR toggle for a
                // stereo pair (3/4, 5/6, ...); the right picker walks the four
                // output destinations. Panel: NAV_PRESS enters EDIT, the encoder
                // steps the focused picker.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gap

                    Row {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: Style.gapTight

                        Text {
                            width: 44
                            height: parent.height
                            text: "INPUT"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "<"
                            textSize: 18
                            focusId: "shift.track.input.dec"
                            onClicked: {
                                mixer.cycleTrackInCh(-1)
                                app.showTextPopup("INPUT SOURCE", mixer.trackInputText)
                            }
                        }
                        TouchButton {
                            width: parent.width - 44 - Style.touch * 2 - 68 -
                                   Style.gapTight * 3
                            height: parent.height
                            text: mixer.trackInputText
                            textSize: Style.fontSmall
                            variant: "ghost"
                            focusId: "shift.track.input"
                            onClicked: {
                                mixer.cycleTrackInCh(1)
                                app.showTextPopup("INPUT SOURCE", mixer.trackInputText)
                            }
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: ">"
                            textSize: 18
                            focusId: "shift.track.input.inc"
                            onClicked: {
                                mixer.cycleTrackInCh(1)
                                app.showTextPopup("INPUT SOURCE", mixer.trackInputText)
                            }
                        }
                        TouchButton {
                            width: 64
                            height: parent.height
                            text: mixer.trackInPair ? "PAIR" : "MONO"
                            textSize: Style.fontSmall
                            checked: mixer.trackInPair
                            focusId: "shift.track.input.pair"
                            onClicked: {
                                mixer.trackInPair = !mixer.trackInPair
                                app.showTextPopup("INPUT SOURCE", mixer.trackInputText)
                            }
                        }
                    }

                    Row {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: Style.gapTight

                        Text {
                            width: 52
                            height: parent.height
                            text: "OUTPUT"
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: "<"
                            textSize: 18
                            focusId: "shift.track.output.dec"
                            onClicked: {
                                mixer.cycleTrackOutDst(-1)
                                app.showTextPopup("OUTPUT DEST", mixer.trackOutputText)
                            }
                        }
                        TouchButton {
                            width: parent.width - 52 - Style.touch * 2 -
                                   Style.gapTight * 2
                            height: parent.height
                            text: mixer.trackOutputText
                            textSize: Style.fontSmall
                            variant: "ghost"
                            focusId: "shift.track.output"
                            onClicked: {
                                mixer.cycleTrackOutDst(1)
                                app.showTextPopup("OUTPUT DEST", mixer.trackOutputText)
                            }
                        }
                        TouchButton {
                            width: Style.touch
                            height: parent.height
                            text: ">"
                            textSize: 18
                            focusId: "shift.track.output.inc"
                            onClicked: {
                                mixer.cycleTrackOutDst(1)
                                app.showTextPopup("OUTPUT DEST", mixer.trackOutputText)
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch * 5
                    spacing: Style.gap

                    Column {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: 0
                        TrackSliderRow {
                            width: parent.width
                            label: "GAIN"
                            value: mixer.trackTrimDb
                            minimumValue: -24
                            maximumValue: 24
                            popupKind: "db"
                            focusId: "shift.track.gain"
                            onMoved: (value) => mixer.setTrackTrimDb(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "FX1"
                            value: mixer.trackSendA
                            popupKind: "percent"
                            focusId: "shift.track.fx1"
                            onMoved: (value) => mixer.setTrackSendA(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "FX2"
                            value: mixer.trackSendB
                            popupKind: "percent"
                            focusId: "shift.track.fx2"
                            onMoved: (value) => mixer.setTrackSendB(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "CUE"
                            value: mixer.trackCue
                            popupKind: "percent"
                            focusId: "shift.track.cue"
                            onMoved: (value) => mixer.setTrackCue(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "OUT34"
                            value: mixer.trackOut34
                            popupKind: "percent"
                            focusId: "shift.track.out34"
                            onMoved: (value) => mixer.setTrackOut34(value)
                        }
                    }

                    Column {
                        width: (parent.width - Style.gap) / 2
                        height: parent.height
                        spacing: Style.gap

                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "SENDS  " + config.sendSummary
                            textSize: Style.fontSmall
                            focusId: "shift.monitor.sends"
                            onClicked: app.openConfigFrom("sends", this)
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "MONITOR   " + ["PLAY", "AUTO", "BOTH", "MON"][mixer.trackMonitor]
                            textSize: Style.fontSmall
                            focusId: "shift.track.monitor"
                            onClicked: app.openConfigFrom("trackmonitor", this)
                        }
                        Text {
                            width: parent.width
                            text: "Send source is PRE (before the fader) or POST (after gain + fader). " +
                                  "Input monitor is the same four-way choice as the TRACK strip."
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    // Label + horizontal slider row used by the TRACK views. Faders stay
    // faders on the mixer/track surface; the popup/knob language is for
    // parameters on the FX, EQ and comp faces.
    component TrackSliderRow: Row {
        property string label: ""
        property real value: 0.0
        property real minimumValue: 0.0
        property real maximumValue: 1.0
        property string popupKind: "db"
        property string focusId: ""
        signal moved(real value)

        height: Style.touch
        spacing: Style.gap

        Text {
            width: 46
            height: parent.height
            text: parent.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        TouchSlider {
            id: trackSlider
            width: parent.width - 54
            height: parent.height
            minimumValue: parent.minimumValue
            maximumValue: parent.maximumValue
            value: parent.value
            focusId: parent.focusId
            popupLabel: parent.label
            popupKind: parent.popupKind
            Binding on value { value: trackSlider.parent.value; when: !trackSlider.dragging }
            onMoved: (value) => parent.moved(value)
        }
    }
}
