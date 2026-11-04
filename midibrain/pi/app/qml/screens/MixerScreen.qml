import QtQuick
import Mnkybox 1.0
import "../components"

// Mixer screen with two views of the same strips: MIXER (one setting across
// many tracks) and TRACK (one track with many settings). SHIFT switches to the
// configure face: per-strip Gain/FX/CUE/OUT34/monitor for MIXER, and sends,
// pre/post, monitor and (pending) assignment for TRACK.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 3

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
            Text {
                width: parent.width - 198
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
            height: parent.height - Style.touch - 3

            Row {
                anchors.fill: parent
                spacing: 3

                Item {
                    id: stripsArea
                    width: parent.width - Style.masterWidth - 3
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
                    spacing: 3

                    Row {
                        width: parent.width
                        height: Style.touch
                        spacing: 2

                        TouchButton {
                            width: (parent.width - 2) / 2
                            height: parent.height
                            text: "1-9"
                            textSize: Style.fontSmall
                            checked: mixer.bank === 0
                            focusId: "mixer.bank.0"
                            onClicked: mixer.setBank(0)
                        }
                        TouchButton {
                            width: (parent.width - 2) / 2
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
                        height: parent.height - Style.touch - 3
                        color: Style.panelDark
                        radius: Style.corner
                        border.width: Style.outlineWidth
                        border.color: Style.outline

                        Column {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 4

                            Text {
                                width: parent.width
                                height: 18
                                text: "MASTER"
                                color: Style.text
                                font.pixelSize: Style.fontTitle
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }

                            TouchSlider {
                                id: masterGain
                                width: parent.width
                                height: 112
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
                                height: Math.max(40, parent.height - 18 - 112 - 8)
                                spacing: 5

                                Repeater {
                                    model: 2
                                    delegate: Item {
                                        width: (parent.width - 5) / 2
                                        height: parent.height

                                        Rectangle {
                                            anchors.fill: parent
                                            color: Style.panel
                                            border.width: Style.outlineWidth
                                            border.color: Style.outline
                                        }
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 1
                                            height: Math.max(0, (parent.height - 2) *
                                                             (index === 0 ? meterFill(mixer.meterLeft)
                                                                          : meterFill(mixer.meterRight)))
                                            color: (index === 0 ? mixer.meterLeft : mixer.meterRight) > 0.95
                                                   ? Style.record : Style.ok
                                        }
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
            height: parent.height - Style.touch - 3

            Column {
                width: parent.width
                height: parent.height
                spacing: 3

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    TouchButton {
                        width: 96
                        height: parent.height
                        text: "1-9"
                        textSize: Style.fontSmall
                        checked: mixer.bank === 0
                        onClicked: mixer.setBank(0)
                    }
                    TouchButton {
                        width: 96
                        height: parent.height
                        text: "10-18"
                        textSize: Style.fontSmall
                        checked: mixer.bank === 1
                        onClicked: mixer.setBank(1)
                    }
                    Text {
                        width: parent.width - 198
                        height: parent.height
                        text: "SEND / MONITOR CONFIG"
                        color: Style.accent
                        font.pixelSize: Style.fontSmall
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Item {
                    id: shiftStripsArea
                    width: parent.width
                    height: parent.height - Style.touch - 3

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
            height: parent.height - Style.touch - 3

            Column {
                width: parent.width
                height: parent.height
                spacing: 3

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
                    spacing: 4

                    TouchButton {
                        width: (parent.width - 8) / 3
                        height: parent.height
                        text: "REC"
                        textSize: Style.fontSmall
                        checked: mixer.trackArm
                        checkedColor: Style.record
                        focusId: "track.rec"
                        onClicked: mixer.toggleArm(mixer.trackIndex)
                    }
                    TouchButton {
                        width: (parent.width - 8) / 3
                        height: parent.height
                        text: "MUTE"
                        textSize: Style.fontSmall
                        checked: mixer.trackMute
                        checkedColor: Style.muteOn
                        focusId: "track.mute"
                        onClicked: mixer.toggleMute(mixer.trackIndex)
                    }
                    TouchButton {
                        width: (parent.width - 8) / 3
                        height: parent.height
                        text: "SOLO"
                        textSize: Style.fontSmall
                        checked: mixer.trackSolo
                        checkedColor: Style.soloOn
                        focusId: "track.solo"
                        onClicked: mixer.toggleSolo(mixer.trackIndex)
                    }
                }
            }
        }

        // ---- TRACK SHIFT: configure + assignment stubs --------------------
        Item {
            visible: mixer.trackMode && screen.shiftFace
            width: parent.width
            height: parent.height - Style.touch - 3

            Column {
                width: parent.width
                height: parent.height
                spacing: 3

                // Per-track I/O assignment (session v2 inCh/inPair/outDst). The
                // left picker walks none / IN 01..18 with the PAIR toggle for a
                // stereo pair (3/4, 5/6, ...); the right picker walks the four
                // output destinations. Panel: NAV_PRESS enters EDIT, the encoder
                // steps the focused picker.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 8

                    Row {
                        width: (parent.width - 8) / 2
                        height: parent.height
                        spacing: 2

                        Text {
                            width: 40
                            height: parent.height
                            text: "INPUT"
                            color: Style.textDim
                            font.pixelSize: 9
                            font.bold: true
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
                            width: parent.width - 40 - Style.touch * 2 - 70 - 8
                            height: parent.height
                            text: mixer.trackInputText
                            textSize: Style.fontSmall
                            idleColor: Style.panelDark
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
                            width: 68
                            height: parent.height
                            text: mixer.trackInPair ? "PAIR" : "MONO"
                            textSize: 9
                            checked: mixer.trackInPair
                            focusId: "shift.track.input.pair"
                            onClicked: {
                                mixer.trackInPair = !mixer.trackInPair
                                app.showTextPopup("INPUT SOURCE", mixer.trackInputText)
                            }
                        }
                    }

                    Row {
                        width: (parent.width - 8) / 2
                        height: parent.height
                        spacing: 2

                        Text {
                            width: 46
                            height: parent.height
                            text: "OUTPUT"
                            color: Style.textDim
                            font.pixelSize: 9
                            font.bold: true
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
                            width: parent.width - 46 - Style.touch * 2 - 6
                            height: parent.height
                            text: mixer.trackOutputText
                            textSize: Style.fontSmall
                            idleColor: Style.panelDark
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
                    spacing: 6

                    Column {
                        width: (parent.width - 6) / 2
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
                        width: (parent.width - 6) / 2
                        height: parent.height
                        spacing: 0
                        SendSourceRow {
                            width: parent.width
                            label: "CUE"
                            source: session.sendCue
                            focusPrefix: "shift.monitor.send.cue"
                            onPicked: (value) => session.sendCue = value
                        }
                        SendSourceRow {
                            width: parent.width
                            label: "FX1"
                            source: session.sendFx1
                            focusPrefix: "shift.monitor.send.fx1"
                            onPicked: (value) => session.sendFx1 = value
                        }
                        SendSourceRow {
                            width: parent.width
                            label: "FX2"
                            source: session.sendFx2
                            focusPrefix: "shift.monitor.send.fx2"
                            onPicked: (value) => session.sendFx2 = value
                        }
                        SendSourceRow {
                            width: parent.width
                            label: "OUT34"
                            source: session.sendOut34
                            focusPrefix: "shift.monitor.send.out34"
                            onPicked: (value) => session.sendOut34 = value
                        }
                        SendSourceRow {
                            width: parent.width
                            label: "REC"
                            source: session.sendRec
                            focusPrefix: "shift.monitor.send.rec"
                            onPicked: (value) => session.sendRec = value
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    Text {
                        width: 76
                        height: parent.height
                        text: "INPUT MON"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: (parent.width - 82) / 3
                        height: parent.height
                        text: "OFF"
                        textSize: Style.fontSmall
                        checked: mixer.trackMonitor === 0
                        checkedColor: Style.record
                        focusId: "shift.track.monitor.0"
                        onClicked: mixer.setTrackMonitor(0)
                    }
                    TouchButton {
                        width: (parent.width - 82) / 3
                        height: parent.height
                        text: "AUTO"
                        textSize: Style.fontSmall
                        checked: mixer.trackMonitor === 1
                        focusId: "shift.track.monitor.1"
                        onClicked: mixer.setTrackMonitor(1)
                    }
                    TouchButton {
                        width: (parent.width - 82) / 3
                        height: parent.height
                        text: "ON"
                        textSize: Style.fontSmall
                        checked: mixer.trackMonitor === 2
                        checkedColor: Style.ok
                        focusId: "shift.track.monitor.2"
                        onClicked: mixer.setTrackMonitor(2)
                    }
                }
            }
        }
    }

    // Label + horizontal slider row used by the TRACK views.
    component TrackSliderRow: Row {
        property string label: ""
        property real value: 0.0
        property real minimumValue: 0.0
        property real maximumValue: 1.0
        property string popupKind: "db"
        property string focusId: ""
        signal moved(real value)

        height: Style.touch
        spacing: 3

        Text {
            width: 46
            height: parent.height
            text: parent.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        TouchSlider {
            id: trackSlider
            width: parent.width - 49
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

    component SendSourceRow: Row {
        property string label: ""
        property int source: 1
        property string focusPrefix: ""
        signal picked(int value)

        height: Style.touch
        spacing: 2
        Text {
            width: 52
            height: parent.height
            text: parent.label
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        TouchButton {
            width: (parent.width - 56) / 2
            height: parent.height
            text: "PRE"
            textSize: 9
            checked: parent.source === 0
            focusId: parent.focusPrefix + ".pre"
            onClicked: parent.picked(0)
        }
        TouchButton {
            width: (parent.width - 56) / 2
            height: parent.height
            text: "POST"
            textSize: 9
            checked: parent.source === 1
            focusId: parent.focusPrefix + ".post"
            onClicked: parent.picked(1)
        }
    }

    // Meter fill in dB, floored at -48 dB like the engine meter mapping.
    function meterFill(peak) {
        if (!(peak > 0)) return 0
        const db = 20 * Math.log(peak) / Math.LN10
        if (db <= -48) return 0
        return Math.min(1, (db + 48) / 48)
    }
}
