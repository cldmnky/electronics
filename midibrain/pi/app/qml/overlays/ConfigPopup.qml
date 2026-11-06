import QtQuick
import Mnkybox 1.0
import "../components"

// Generic configuration modal: summary buttons open a draft editor, OK applies,
// CANCEL/EXIT discards. The background swallows touch but never dismisses, so a
// stray touch cannot lose edits. Panel focus is trapped by AppController while
// config.open is true.
Rectangle {
    id: root

    anchors.fill: parent
    visible: config.open
    color: "#9e000000"
    z: 95

    // Swallow touches outside the panel; no dismiss.
    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        id: panel
        width: Math.min(744, root.width - 16)
        // Content height, capped at the screen: a short kind (MAIN COMP) gets a
        // short panel instead of an empty lower half, and OK/CANCEL stay the
        // last row of the content, so they stay bottom-anchored in every kind.
        height: Math.min(root.height - 8, content.implicitHeight + 24)
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent

        // 120 ms scale-fade from the summary button that opened the popup.
        // A direct controller call (screenshot runner, panel path) has no
        // anchor and grows from the panel centre instead.
        property real popScale: config.open ? 1.0 : 0.94
        opacity: config.open ? 1.0 : 0.0
        transform: Scale {
            origin.x: app.configOriginX >= 0 ? (app.configOriginX - panel.x)
                                             : panel.width / 2
            origin.y: app.configOriginY >= 0 ? (app.configOriginY - panel.y)
                                             : panel.height / 2
            xScale: panel.popScale
            yScale: panel.popScale
        }
        Behavior on popScale {
            NumberAnimation { duration: Style.motionPopup; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: Style.motionPopup; easing.type: Easing.OutCubic }
        }

        Column {
            id: content
            x: 12
            y: 12
            width: panel.width - 24
            spacing: Style.gap

            Row {
                width: parent.width
                height: 24
                Text {
                    width: parent.width - 190
                    height: parent.height
                    text: config.title
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    width: 190
                    height: parent.height
                    text: "OK APPLIES - EXIT CANCELS"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // ---- METRONOME -------------------------------------------------
            Column {
                visible: config.kind === "metronome"
                width: parent.width
                spacing: 4

                Text { width: parent.width; height: 16; text: "MODE"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "OFF"; checked: config.clickMode === 0; focusId: "config.mode.0"; onClicked: config.clickMode = 0 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "REC"; checked: config.clickMode === 1; focusId: "config.mode.1"; onClicked: config.clickMode = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "ON PLAY"; checked: config.clickMode === 2; focusId: "config.mode.2"; onClicked: config.clickMode = 2 }
                }
                Text { width: parent.width; height: 16; text: "OUTPUT"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "MAIN"; checked: config.clickOutput === 0; focusId: "config.out.0"; onClicked: config.clickOutput = 0 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "PHONES"; checked: config.clickOutput === 1; focusId: "config.out.1"; onClicked: config.clickOutput = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "OUT34"; checked: config.clickOutput === 2; focusId: "config.out.2"; onClicked: config.clickOutput = 2 }
                }
                ConfigSlider {
                    width: parent.width
                    label: "GAIN"
                    value: config.clickGainDb
                    minimumValue: -60
                    maximumValue: 20
                    suffix: " dB"
                    popupKind: "db"
                    focusId: "config.gain"
                    onMoved: (value) => config.clickGainDb = value
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    Text { width: 70; height: parent.height; text: "BARS"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
                    TouchButton { width: 60; height: parent.height; text: "-"; textSize: 20; onClicked: config.clickBars = Math.max(1, config.clickBars - 1) }
                    Text { width: 100; height: parent.height; text: config.clickBars + " bar"; color: Style.text; font.pixelSize: Style.fontBody; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                    TouchButton { width: 60; height: parent.height; text: "+"; textSize: 20; focusId: "config.bars"; onClicked: config.clickBars = Math.min(8, config.clickBars + 1) }
                }
                Text {
                    width: parent.width
                    text: "On play = click while the transport rolls; Record = only while recording. The output choice feeds the engine click bus."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- SEND SOURCE ----------------------------------------------
            Column {
                visible: config.kind === "sends"
                width: parent.width
                spacing: 2
                Text {
                    width: parent.width
                    height: 18
                    text: "PRE = gain before the fader; POST = after gain + fader. One choice per destination."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
                ConfigSendRow { width: parent.width; label: "CUE"; source: config.sendCue; focusPrefix: "config.send.cue"; onPicked: (v) => config.sendCue = v }
                ConfigSendRow { width: parent.width; label: "FX1"; source: config.sendFx1; focusPrefix: "config.send.fx1"; onPicked: (v) => config.sendFx1 = v }
                ConfigSendRow { width: parent.width; label: "FX2"; source: config.sendFx2; focusPrefix: "config.send.fx2"; onPicked: (v) => config.sendFx2 = v }
                ConfigSendRow { width: parent.width; label: "OUT34"; source: config.sendOut34; focusPrefix: "config.send.out34"; onPicked: (v) => config.sendOut34 = v }
                ConfigSendRow { width: parent.width; label: "REC"; source: config.sendRec; focusPrefix: "config.send.rec"; onPicked: (v) => config.sendRec = v }
            }

            // ---- INPUT MONITOR (TRACK SHIFT summary) ----------------------
            Column {
                visible: config.kind === "trackmonitor"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 18
                    text: "Track " + String(mixer.trackIndex + 1).padStart(2, "0") +
                          " input monitoring"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "PLAY"; checked: config.trackMonitor === 0; focusId: "config.trackmonitor.0"; onClicked: config.trackMonitor = 0 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "AUTO"; checked: config.trackMonitor === 1; focusId: "config.trackmonitor.1"; onClicked: config.trackMonitor = 1 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "BOTH"; checked: config.trackMonitor === 2; focusId: "config.trackmonitor.2"; onClicked: config.trackMonitor = 2 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "MON"; checked: config.trackMonitor === 3; focusId: "config.trackmonitor.3"; onClicked: config.trackMonitor = 3 }
                }
                Text {
                    width: parent.width
                    text: "PLAY monitors the active take only; AUTO switches to live input when the track is armed or has no take; BOTH layers live input over the take; MON monitors live input only."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- MONITOR ---------------------------------------------------
            Column {
                visible: config.kind === "monitor"
                width: parent.width
                spacing: 4
                ConfigChoiceRow { width: parent.width; label: "PHONES"; first: "CUE"; second: "MAIN"; selected: config.phonesSrc; focusPrefix: "config.phones"; onPicked: (v) => config.phonesSrc = v }
                ConfigChoiceRow { width: parent.width; label: "CUE STYLE"; first: "FADER"; second: "TOGGLE"; selected: config.cueStyle; focusPrefix: "config.cuestyle"; onPicked: (v) => config.cueStyle = v }
                ConfigChoiceRow { width: parent.width; label: "OUT3&4"; first: "STEREO"; second: "MONO"; selected: config.out34Mode; focusPrefix: "config.out34"; onPicked: (v) => config.out34Mode = v }
                Text {
                    width: parent.width
                    text: "Phones selects the CUE bus or the compressed Main duplicate. CUE style switches the per-track CUE send between a level fader and a >0 toggle. OUT3&4 is the uncompressed monitor bus."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- MAIN COMP -------------------------------------------------
            Column {
                visible: config.kind === "comp"
                width: parent.width
                spacing: 3
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: 110; height: parent.height; text: config.compOn ? "COMP ON" : "COMP OFF"; checked: config.compOn; focusId: "config.on"; onClicked: config.compOn = !config.compOn }
                    TouchButton { width: (parent.width - 118) / 2; height: parent.height; text: "PEAK"; textSize: Style.fontSmall; checked: config.compMode === 0; focusId: "config.mode.0"; onClicked: config.compMode = 0 }
                    TouchButton { width: (parent.width - 118) / 2; height: parent.height; text: "RMS"; textSize: Style.fontSmall; checked: config.compMode === 1; focusId: "config.mode.1"; onClicked: config.compMode = 1 }
                }
                Row {
                    width: parent.width
                    height: 84
                    spacing: Style.gapTight

                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 4) / 5
                        height: parent.height
                        label: "THRESH"
                        value: config.compThreshDb
                        minimumValue: -48
                        maximumValue: 0
                        suffix: " dB"
                        popupKind: "db"
                        focusId: "config.thresh"
                        onMoved: (v) => config.compThreshDb = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 4) / 5
                        height: parent.height
                        label: "RATIO"
                        value: config.compRatio
                        minimumValue: 1
                        maximumValue: 100
                        suffix: ":1"
                        popupKind: "ratio"
                        focusId: "config.ratio"
                        onMoved: (v) => config.compRatio = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 4) / 5
                        height: parent.height
                        label: "ATTACK"
                        value: config.compAttackMs
                        minimumValue: 0
                        maximumValue: 1000
                        suffix: " ms"
                        popupKind: "ms"
                        focusId: "config.attack"
                        onMoved: (v) => config.compAttackMs = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 4) / 5
                        height: parent.height
                        label: "RELEASE"
                        value: config.compReleaseMs
                        minimumValue: 0
                        maximumValue: 1000
                        suffix: " ms"
                        popupKind: "ms"
                        focusId: "config.release"
                        onMoved: (v) => config.compReleaseMs = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 4) / 5
                        height: parent.height
                        label: "MAKEUP"
                        value: config.compMakeupDb
                        minimumValue: -36
                        maximumValue: 36
                        suffix: " dB"
                        popupKind: "db"
                        focusId: "config.makeup"
                        onMoved: (v) => config.compMakeupDb = v
                    }
                }
                Text {
                    width: parent.width
                    text: "Main-bus compressor; never applied to OUT3&4. Threshold is dBFS, attack/release are milliseconds, makeup is output gain."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- EQ BAND TYPE ---------------------------------------------
            Column {
                visible: config.kind === "eqtype"
                width: parent.width
                spacing: 4
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "NONE"; checked: config.eqType === 0; focusId: "config.eqtype.0"; onClicked: config.eqType = 0 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "LCUT"; checked: config.eqType === 1; focusId: "config.eqtype.1"; onClicked: config.eqType = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "LSHELF"; checked: config.eqType === 2; focusId: "config.eqtype.2"; onClicked: config.eqType = 2 }
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "PARAM"; checked: config.eqType === 3; focusId: "config.eqtype.3"; onClicked: config.eqType = 3 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "HSHELF"; checked: config.eqType === 4; focusId: "config.eqtype.4"; onClicked: config.eqType = 4 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "HCUT"; checked: config.eqType === 5; focusId: "config.eqtype.5"; onClicked: config.eqType = 5 }
                }
                Text {
                    width: parent.width
                    text: "None passes the band; LCut and HCut are cut filters with no gain; the shelves and Param use gain, frequency and Q."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- DELAY (X/Y secondary parameters) -------------------------
            Column {
                visible: config.kind === "delay"
                width: parent.width
                spacing: 4
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: config.delayBeat ? "BEAT ON" : "BEAT OFF"; checked: config.delayBeat === 1; focusId: "config.delay.beat"; onClicked: config.delayBeat = config.delayBeat ? 0 : 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: config.delayPing ? "PING ON" : "PING OFF"; checked: config.delayPing === 1; focusId: "config.delay.ping"; onClicked: config.delayPing = config.delayPing ? 0 : 1 }
                    Row {
                        width: (parent.width - 8) / 3
                        height: parent.height
                        spacing: 2
                        TouchButton { width: Style.touch; height: parent.height; text: "-"; textSize: 18; focusId: "config.delay.div.prev"; onClicked: config.delayDivision = Math.max(0, config.delayDivision - 1) }
                        TouchButton { width: parent.width - Style.touch * 2 - 4; height: parent.height; text: config.divisionText; textSize: Style.fontSmall; focusId: "config.delay.division"; onClicked: config.delayDivision = (config.delayDivision + 1) % 8 }
                        TouchButton { width: Style.touch; height: parent.height; text: "+"; textSize: 18; focusId: "config.delay.div.next"; onClicked: config.delayDivision = Math.min(7, config.delayDivision + 1) }
                    }
                }
                ConfigSlider { width: parent.width; label: "FEEDBACK"; value: config.delayFeedback; minimumValue: 0; maximumValue: 0.95; suffix: ""; popupKind: "percent"; focusId: "config.delay.feedback"; onMoved: (v) => config.delayFeedback = v }
                ConfigSlider { width: parent.width; label: "MIX"; value: config.delayMix; minimumValue: 0; maximumValue: 1; suffix: ""; popupKind: "percent"; focusId: "config.delay.mix"; onMoved: (v) => config.delayMix = v }
                Text {
                    width: parent.width
                    text: "The grid owns cutoff (X) and width (Y). These are the delay's other controls: Beat syncs Time to the clock and Division picks the note value."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- REVERB (X/Y secondary parameters) ------------------------
            Column {
                visible: config.kind === "reverb"
                width: parent.width
                spacing: 4
                Text { width: parent.width; height: 16; text: "MODEL (applies its preset)"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "CORE"; checked: config.reverbModel === 0; focusId: "config.reverb.model.0"; onClicked: config.reverbModel = 0 }
                    TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "ROOM"; checked: config.reverbModel === 1; focusId: "config.reverb.model.1"; onClicked: config.reverbModel = 1 }
                    TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "HALL"; checked: config.reverbModel === 2; focusId: "config.reverb.model.2"; onClicked: config.reverbModel = 2 }
                    TouchButton { width: (parent.width - 12) / 4; height: parent.height; text: "PLATE"; checked: config.reverbModel === 3; focusId: "config.reverb.model.3"; onClicked: config.reverbModel = 3 }
                }
                ConfigSlider { width: parent.width; label: "PREDELAY"; value: config.reverbPredelayMs; minimumValue: 0; maximumValue: 200; suffix: " ms"; popupKind: "ms"; focusId: "config.reverb.predelay"; onMoved: (v) => config.reverbPredelayMs = v }
                ConfigSlider { width: parent.width; label: "DECAY"; value: config.reverbDecay; minimumValue: 0; maximumValue: 0.98; suffix: ""; popupKind: "generic"; focusId: "config.reverb.decay"; onMoved: (v) => config.reverbDecay = v }
                ConfigSlider { width: parent.width; label: "DAMPING"; value: config.reverbDamping; minimumValue: 0; maximumValue: 1; suffix: ""; popupKind: "generic"; focusId: "config.reverb.damping"; onMoved: (v) => config.reverbDamping = v }
                Text {
                    width: parent.width
                    text: "The grid owns size (X) and level (Y). Freeze is the panel/touch macro: longest decay, fully wet, no damping, largest size; releasing restores these values."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- PIPE CHANNEL SCOPE --------------------------------------
            Column {
                visible: config.kind === "pipechannels"
                width: parent.width
                spacing: 3
                Text {
                    width: parent.width
                    height: 16
                    text: config.pipeScopeContext
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: 26
                    text: "SCOPE  " + config.pipeChannelText
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: 2
                    delegate: Row {
                        property int row: index
                        width: parent.width
                        height: Style.touch
                        spacing: 2
                        Repeater {
                            model: 8
                            delegate: TouchButton {
                                property int channel: row * 8 + index
                                width: (parent.width - 14) / 8
                                height: parent.height
                                text: String(channel + 1)
                                textSize: Style.fontSmall
                                checked: ((config.pipeChannelMask >> channel) & 1) === 1
                                focusId: "config.pipe.ch." + channel
                                onClicked: config.togglePipeChannel(channel)
                            }
                        }
                    }
                }
                Text { width: parent.width; height: 14; text: "PRESETS"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    TouchButton { width: (parent.width - 12) / 5; height: parent.height; text: "ALL"; textSize: Style.fontSmall; focusId: "config.pipe.preset.0"; onClicked: config.setPipeChannelPreset(0) }
                    TouchButton { width: (parent.width - 12) / 5; height: parent.height; text: "1-4"; textSize: Style.fontSmall; focusId: "config.pipe.preset.1"; onClicked: config.setPipeChannelPreset(1) }
                    TouchButton { width: (parent.width - 12) / 5; height: parent.height; text: "5-8"; textSize: Style.fontSmall; focusId: "config.pipe.preset.2"; onClicked: config.setPipeChannelPreset(2) }
                    TouchButton { width: (parent.width - 12) / 5; height: parent.height; text: "9-12"; textSize: Style.fontSmall; focusId: "config.pipe.preset.3"; onClicked: config.setPipeChannelPreset(3) }
                    TouchButton { width: (parent.width - 12) / 5; height: parent.height; text: "13-16"; textSize: Style.fontSmall; focusId: "config.pipe.preset.4"; onClicked: config.setPipeChannelPreset(4) }
                }
                Text {
                    width: parent.width
                    text: "The pipe processes only the selected MIDI channels; every other channel passes that pipe unchanged. 0xFFFF is all channels and is what a new pipe starts with."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- EDIT TIMELINE REGION OPS --------------------------------
            Column {
                visible: config.kind === "timelineops"
                width: parent.width
                spacing: 2

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "TRIM"; textSize: Style.fontSmall; checked: config.timelineOp === 0; focusId: "config.timeline.op.0"; onClicked: config.timelineOp = 0 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "SPLIT"; textSize: Style.fontSmall; checked: config.timelineOp === 1; focusId: "config.timeline.op.1"; onClicked: config.timelineOp = 1 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "GAIN"; textSize: Style.fontSmall; checked: config.timelineOp === 2; focusId: "config.timeline.op.2"; onClicked: config.timelineOp = 2 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "FADES"; textSize: Style.fontSmall; checked: config.timelineOp === 3; focusId: "config.timeline.op.3"; onClicked: config.timelineOp = 3 }
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 3
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "NORM"; textSize: Style.fontSmall; checked: config.timelineOp === 4; focusId: "config.timeline.op.4"; onClicked: config.timelineOp = 4 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "DELETE"; textSize: Style.fontSmall; checked: config.timelineOp === 5; focusId: "config.timeline.op.5"; onClicked: config.timelineOp = 5 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "CLEAR"; textSize: Style.fontSmall; checked: config.timelineOp === 6; focusId: "config.timeline.op.6"; onClicked: config.timelineOp = 6 }
                    TouchButton { width: (parent.width - 9) / 4; height: parent.height; text: "RENDER"; textSize: Style.fontSmall; checked: config.timelineOp === 7; focusId: "config.timeline.op.7"; onClicked: config.timelineOp = 7 }
                }
                ConfigSlider {
                    width: parent.width
                    label: "GAIN"
                    value: config.timelineGainDb
                    minimumValue: -96
                    maximumValue: 12
                    suffix: " dB"
                    popupKind: "db"
                    focusId: "config.timeline.gain"
                    onMoved: (value) => config.timelineGainDb = value
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    ConfigSlider {
                        width: (parent.width - Style.gapTight) / 2
                        label: "FADE IN"
                        value: config.timelineFadeInBeats
                        minimumValue: 0
                        maximumValue: 16
                        suffix: " b"
                        popupKind: "generic"
                        focusId: "config.timeline.fadein"
                        onMoved: (value) => config.timelineFadeInBeats = value
                    }
                    ConfigSlider {
                        width: (parent.width - Style.gapTight) / 2
                        label: "FADE OUT"
                        value: config.timelineFadeOutBeats
                        minimumValue: 0
                        maximumValue: 16
                        suffix: " b"
                        popupKind: "generic"
                        focusId: "config.timeline.fadeout"
                        onMoved: (value) => config.timelineFadeOutBeats = value
                    }
                }
                Item {
                    width: parent.width
                    height: 64
                    Rectangle {
                        anchors.fill: parent
                        color: Style.panelDark
                        radius: Style.corner
                        border.width: Style.outlineWidth
                        border.color: Style.outline
                    }
                    TimelineWave {
                        anchors.fill: parent
                        anchors.margins: 3
                        values: timeline.previewWave
                        showEnvelope: true
                        gainDb: config.timelineGainDb
                        fadeInRatio: timeline.previewLengthBeats > 0
                                      ? config.timelineFadeInBeats / timeline.previewLengthBeats : 0
                        fadeOutRatio: timeline.previewLengthBeats > 0
                                      ? config.timelineFadeOutBeats / timeline.previewLengthBeats : 0
                        placeholderText: "NO PEAKS - PREVIEW UNAVAILABLE"
                    }
                }
                Text {
                    width: parent.width
                    height: 14
                    text: timeline.previewText
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: visible ? 20 : 0
                    visible: config.timelineError !== ""
                    text: config.timelineError
                    color: Style.record
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: 16
                    text: "TRIM/DELETE use IN..OUT. RENDER is explicit and refused: audio::TakePlayer::renderRegionsToTake() is missing."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
            }

            Item { width: 1; height: 2 }

            Row {
                width: parent.width
                height: Style.touch
                spacing: 4
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: "CANCEL"
                    variant: "danger"
                    focusId: "config.cancel"
                    onClicked: app.configCancel()
                }
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: "OK"
                    variant: "primary"
                    focusId: "config.ok"
                    onClicked: app.configAccept()
                }
            }
        }
    }

    component ConfigKnob: Item {
        id: knobRoot
        property string label: ""
        property real value: 0
        property real minimumValue: 0
        property real maximumValue: 1
        property string suffix: ""
        property string popupKind: "generic"
        property string focusId: ""
        signal moved(real value)

        readonly property bool focused: app.focusId === focusId && focusId !== ""

        Rectangle {
            anchors.fill: parent
            color: Style.panel
            radius: Style.corner
            border.width: parent.focused ? 2 : Style.outlineWidth
            border.color: parent.focused ? Style.accent : Style.outline
        }
        TouchKnob {
            id: compDial
            anchors.top: parent.top
            anchors.topMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            width: 46
            height: 46
            showText: false
            value: parent.value
            minimumValue: parent.minimumValue
            maximumValue: parent.maximumValue
            focusId: ""
            popupLabel: parent.label
            popupKind: parent.popupKind
            Binding on value { value: knobRoot.value; when: !compDial.dragging }
            onMoved: (value) => parent.moved(value)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: valueLabel.top
            text: parent.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Text {
            id: valueLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            text: (parent.maximumValue > 20 ? Number(parent.value).toFixed(0)
                                            : Number(parent.value).toFixed(1)) + parent.suffix
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
    }

    component ConfigSlider: Row {
        property string label: ""
        property real value: 0
        property real minimumValue: 0
        property real maximumValue: 1
        property string suffix: ""
        property string popupKind: "generic"
        property string focusId: ""
        signal moved(real value)

        height: Style.touch
        spacing: 3
        Text { width: 64; height: parent.height; text: parent.label; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
        TouchSlider {
            id: popupSlider
            width: parent.width - 132
            height: parent.height
            minimumValue: parent.minimumValue
            maximumValue: parent.maximumValue
            value: parent.value
            focusId: parent.focusId
            popupLabel: parent.label
            popupKind: parent.popupKind
            Binding on value { value: popupSlider.parent.value; when: !popupSlider.dragging }
            onMoved: (value) => parent.moved(value)
        }
        Text {
            width: 62
            height: parent.height
            text: (parent.maximumValue > 20 ? Number(parent.value).toFixed(0)
                                            : Number(parent.value).toFixed(1)) + parent.suffix
            color: Style.text
            font.pixelSize: Style.fontSmall
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    component ConfigSendRow: Row {
        property string label: ""
        property int source: 1
        property string focusPrefix: ""
        signal picked(int value)

        height: Style.touch
        spacing: 3
        Text { width: 64; height: parent.height; text: parent.label; color: Style.text; font.pixelSize: Style.fontBody; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
        TouchButton { width: (parent.width - 68) / 2; height: parent.height; text: "PRE"; checked: parent.source === 0; focusId: parent.focusPrefix + ".pre"; onClicked: parent.picked(0) }
        TouchButton { width: (parent.width - 68) / 2; height: parent.height; text: "POST"; checked: parent.source === 1; focusId: parent.focusPrefix + ".post"; onClicked: parent.picked(1) }
    }

    component ConfigChoiceRow: Row {
        property string label: ""
        property string first: ""
        property string second: ""
        property int selected: 0
        property string focusPrefix: ""
        signal picked(int value)

        height: Style.touch
        spacing: 3
        Text { width: 84; height: parent.height; text: parent.label; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
        TouchButton { width: (parent.width - 88) / 2; height: parent.height; text: parent.first; checked: parent.selected === 0; focusId: parent.focusPrefix + ".0"; onClicked: parent.picked(0) }
        TouchButton { width: (parent.width - 88) / 2; height: parent.height; text: parent.second; checked: parent.selected === 1; focusId: parent.focusPrefix + ".1"; onClicked: parent.picked(1) }
    }
}
