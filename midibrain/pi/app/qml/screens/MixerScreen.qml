import QtQuick
import Mnkybox 1.0
import "../components"

// Mixer screen with two views of the same strips: MIXER (one setting across
// many tracks) and TRACK (one track with many settings). The mixer is the
// session's dynamic track list, four strips per page: ADD TRACK appends a strip,
// the expand chevron on a strip opens the full channel popout (where REMOVE
// TRACK lives, confirmed), and every selector opens its option list. SHIFT
// switches to the configure face: per-strip Gain/FX/CUE/OUT34/monitor for
// MIXER, and the TRACK I/O, sends and monitor summaries for TRACK.
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
                width: 84
                height: parent.height
                text: "MIXER"
                textSize: Style.fontSmall
                checked: !mixer.trackMode
                focusId: "mixer.mode.0"
                onClicked: mixer.trackMode = false
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "TRACK"
                textSize: Style.fontSmall
                checked: mixer.trackMode
                focusId: "mixer.mode.1"
                onClicked: mixer.trackMode = true
            }
            Row {
                width: 196
                height: parent.height
                spacing: Style.gapTight

                TouchButton {
                    width: Style.touch
                    height: parent.height
                    text: "<"
                    textSize: 18
                    focusId: "mixer.bank.0"
                    onClicked: mixer.stepBank(-1)
                }
                Text {
                    width: parent.width - Style.touch * 2 - Style.gapTight * 2
                    height: parent.height
                    text: mixer.bankText
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TouchButton {
                    width: Style.touch
                    height: parent.height
                    text: ">"
                    textSize: 18
                    focusId: "mixer.bank.1"
                    onClicked: mixer.stepBank(1)
                }
            }
            TouchButton {
                width: 110
                height: parent.height
                text: "ADD TRACK"
                textSize: Style.fontSmall
                variant: "primary"
                focusId: "mixer.add"
                onClicked: app.requestAddTrack()
            }
            Text {
                width: parent.width - 84 - 84 - 196 - 110 - Style.gap * 4
                height: parent.height
                text: (screen.shiftFace ? "SHIFT  " : "") +
                      (mixer.trackMode ? mixer.trackName
                                       : (mixer.empty ? "no tracks yet"
                                                      : "tap ^ on a strip for the channel"))
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

                    Text {
                        visible: mixer.empty
                        anchors.centerIn: parent
                        width: parent.width - 40
                        text: "EMPTY MIXER - PRESS ADD TRACK"
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: mixer
                        delegate: MixerStrip {
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? stripsArea.width / mixer.stripsPerBank : 0
                            height: stripsArea.height
                            x: visible ? (index % mixer.stripsPerBank) * width : 0
                            stripIndex: model.slot
                            stripName: model.name
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

                Rectangle {
                    width: Style.masterWidth
                    height: parent.height
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
                            height: Math.max(96, parent.height - 18 - 88 - Style.gapTight * 2)
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
                            height: 84
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

        // ---- MIXER SHIFT: configure sends and monitor ----------------------
        Item {
            visible: !mixer.trackMode && screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - Style.gap

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
                        stripIndex: model.slot
                        stripName: model.name
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
                    height: Style.touch

                    // The selector is the current page of four: the header's
                    // page buttons walk the same list the MIXER face banks.
                    Repeater {
                        model: mixer
                        delegate: TouchButton {
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? parent.width / mixer.stripsPerBank : 0
                            height: Style.touch
                            x: visible ? (index % mixer.stripsPerBank) * width : 0
                            text: model.name
                            textSize: Style.fontSmall
                            checked: model.slot === mixer.trackIndex
                            focusId: "track.select." + model.slot
                            onClicked: mixer.setTrackIndex(model.slot)
                        }
                    }

                    Text {
                        visible: mixer.empty
                        anchors.fill: parent
                        text: "no tracks yet - ADD TRACK on the MIXER face"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        verticalAlignment: Text.AlignVCenter
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
                        onClicked: mixer.setTrackArm(!mixer.trackArm)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "MUTE"
                        textSize: Style.fontSmall
                        checked: mixer.trackMute
                        focusId: "track.mute"
                        onClicked: mixer.setTrackMute(!mixer.trackMute)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "SOLO"
                        textSize: Style.fontSmall
                        checked: mixer.trackSolo
                        focusId: "track.solo"
                        onClicked: mixer.setTrackSolo(!mixer.trackSolo)
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

                // Per-track I/O assignment (session v2 inCh/inPair/outDst): both
                // pickers are tap-to-open option lists, so the face has no
                // steppers and every pick lands on the engine immediately.
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
                            width: parent.width - 44 - 64 - Style.gapTight * 2
                            height: parent.height
                            text: mixer.trackInputText
                            textSize: Style.fontSmall
                            variant: "ghost"
                            chevron: "down"
                            focusId: "shift.track.input"
                            onClicked: app.openInputSource(mixer.trackIndex)
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
                            width: parent.width - 52 - Style.gapTight
                            height: parent.height
                            text: mixer.trackOutputText
                            textSize: Style.fontSmall
                            variant: "ghost"
                            chevron: "down"
                            focusId: "shift.track.output"
                            onClicked: app.openOutputDest(mixer.trackIndex)
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
                            chevron: "down"
                            focusId: "shift.monitor.sends"
                            onClicked: app.openConfigFrom("sends", this)
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "MONITOR   " + ["PLAY", "AUTO", "BOTH", "MON"][mixer.trackMonitor]
                            textSize: Style.fontSmall
                            chevron: "down"
                            focusId: "shift.track.monitor"
                            onClicked: app.openConfigFrom("trackmonitor", this)
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "INSERT   " + mixer.channelInsertName
                            textSize: Style.fontSmall
                            chevron: "down"
                            focusId: "shift.track.insert"
                            onClicked: app.openInsert(mixer.trackIndex)
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
