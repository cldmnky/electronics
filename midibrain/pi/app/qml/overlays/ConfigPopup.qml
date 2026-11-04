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
        height: Math.min(430, root.height - 8)
        anchors.centerIn: parent
        color: Style.panel
        radius: Style.corner
        border.width: 2
        border.color: Style.accent

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Row {
                width: parent.width
                height: 24
                Text {
                    width: parent.width - 120
                    height: parent.height
                    text: config.title
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    width: 120
                    height: parent.height
                    text: "OK applies - EXIT cancels"
                    color: Style.textDim
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // ---- METRONOME -------------------------------------------------
            Column {
                visible: config.kind === "metronome"
                width: parent.width
                spacing: 4

                Text { width: parent.width; height: 16; text: "MODE"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "OFF"; checked: config.clickMode === 0; focusId: "config.mode.0"; onClicked: config.clickMode = 0 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "REC"; checked: config.clickMode === 1; focusId: "config.mode.1"; onClicked: config.clickMode = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "ON PLAY"; checked: config.clickMode === 2; focusId: "config.mode.2"; onClicked: config.clickMode = 2 }
                }
                Text { width: parent.width; height: 16; text: "OUTPUT"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true }
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
                    Text { width: 70; height: parent.height; text: "BARS"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true; verticalAlignment: Text.AlignVCenter }
                    TouchButton { width: 60; height: parent.height; text: "-"; textSize: 20; onClicked: config.clickBars = Math.max(1, config.clickBars - 1) }
                    Text { width: 100; height: parent.height; text: config.clickBars + " bar"; color: Style.text; font.pixelSize: Style.fontBody; font.bold: true; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                    TouchButton { width: 60; height: parent.height; text: "+"; textSize: 20; focusId: "config.bars"; onClicked: config.clickBars = Math.min(8, config.clickBars + 1) }
                }
                Text {
                    width: parent.width
                    text: "On play = click while the transport rolls; Record = only while recording. The output choice feeds the engine click bus."
                    color: Style.textDim
                    font.pixelSize: 10
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
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                ConfigSendRow { width: parent.width; label: "CUE"; source: config.sendCue; focusPrefix: "config.send.cue"; onPicked: (v) => config.sendCue = v }
                ConfigSendRow { width: parent.width; label: "FX1"; source: config.sendFx1; focusPrefix: "config.send.fx1"; onPicked: (v) => config.sendFx1 = v }
                ConfigSendRow { width: parent.width; label: "FX2"; source: config.sendFx2; focusPrefix: "config.send.fx2"; onPicked: (v) => config.sendFx2 = v }
                ConfigSendRow { width: parent.width; label: "OUT34"; source: config.sendOut34; focusPrefix: "config.send.out34"; onPicked: (v) => config.sendOut34 = v }
                ConfigSendRow { width: parent.width; label: "REC"; source: config.sendRec; focusPrefix: "config.send.rec"; onPicked: (v) => config.sendRec = v }
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
                    font.pixelSize: 10
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
                ConfigSlider { width: parent.width; label: "THRESH"; value: config.compThreshDb; minimumValue: -48; maximumValue: 0; suffix: " dB"; popupKind: "db"; focusId: "config.thresh"; onMoved: (v) => config.compThreshDb = v }
                ConfigSlider { width: parent.width; label: "RATIO"; value: config.compRatio; minimumValue: 1; maximumValue: 100; suffix: ":1"; popupKind: "ratio"; focusId: "config.ratio"; onMoved: (v) => config.compRatio = v }
                ConfigSlider { width: parent.width; label: "ATTACK"; value: config.compAttackMs; minimumValue: 0; maximumValue: 1000; suffix: " ms"; popupKind: "ms"; focusId: "config.attack"; onMoved: (v) => config.compAttackMs = v }
                ConfigSlider { width: parent.width; label: "RELEASE"; value: config.compReleaseMs; minimumValue: 0; maximumValue: 1000; suffix: " ms"; popupKind: "ms"; focusId: "config.release"; onMoved: (v) => config.compReleaseMs = v }
                ConfigSlider { width: parent.width; label: "MAKEUP"; value: config.compMakeupDb; minimumValue: -36; maximumValue: 36; suffix: " dB"; popupKind: "db"; focusId: "config.makeup"; onMoved: (v) => config.compMakeupDb = v }
                Text {
                    width: parent.width
                    text: "Main-bus compressor; never applied to OUT3&4. Threshold is dBFS, attack/release are milliseconds, makeup is output gain."
                    color: Style.textDim
                    font.pixelSize: 10
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
                    font.pixelSize: 10
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
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            // ---- REVERB (X/Y secondary parameters) ------------------------
            Column {
                visible: config.kind === "reverb"
                width: parent.width
                spacing: 4
                Text { width: parent.width; height: 16; text: "MODEL (applies its preset)"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true }
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
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            Item { width: 1; height: 2 }

            Row {
                width: parent.width
                height: Style.touch
                spacing: 4
                TouchButton {
                    width: (parent.width - 4) / 2
                    height: parent.height
                    text: "CANCEL"
                    focusId: "config.cancel"
                    idleColor: "#5b2222"
                    onClicked: app.configCancel()
                }
                TouchButton {
                    width: (parent.width - 4) / 2
                    height: parent.height
                    text: "OK"
                    checked: true
                    focusId: "config.ok"
                    onClicked: app.configAccept()
                }
            }
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
        Text { width: 64; height: parent.height; text: parent.label; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true; verticalAlignment: Text.AlignVCenter }
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
            font.pixelSize: 10
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
        Text { width: 64; height: parent.height; text: parent.label; color: Style.text; font.pixelSize: Style.fontBody; font.bold: true; verticalAlignment: Text.AlignVCenter }
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
        Text { width: 84; height: parent.height; text: parent.label; color: Style.textDim; font.pixelSize: Style.fontSmall; font.bold: true; verticalAlignment: Text.AlignVCenter }
        TouchButton { width: (parent.width - 88) / 2; height: parent.height; text: parent.first; checked: parent.selected === 0; focusId: parent.focusPrefix + ".0"; onClicked: parent.picked(0) }
        TouchButton { width: (parent.width - 88) / 2; height: parent.height; text: parent.second; checked: parent.selected === 1; focusId: parent.focusPrefix + ".1"; onClicked: parent.picked(1) }
    }
}
