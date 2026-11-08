import QtQuick
import Mnkybox 1.0
import "../components"

// Generic configuration modal. Every setting is live: a tap writes the model
// immediately, OK/CLOSE keeps it and CANCEL restores what the popup opened with.
// The selector kinds are tap-to-open option lists (input source, output
// destination, FX slot, delay division, reverb model); ADD TRACK is the one kind
// that creates something on OK. The delay/reverb kinds edit one per-track rack
// slot (AppController points them at the selected track's FX1/FX2). The background swallows touch but never
// dismisses, so a stray touch cannot drop a change. Panel focus is trapped by
// AppController while config.open is true.
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
                    width: parent.width - 250
                    height: parent.height
                    text: config.title
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    width: 250
                    height: parent.height
                    text: config.footerHint
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
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "REC ONLY"; textSize: Style.fontSmall; checked: config.clickMode === 1; focusId: "config.mode.1"; onClicked: config.clickMode = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "ON PLAY"; checked: config.clickMode === 2; focusId: "config.mode.2"; onClicked: config.clickMode = 2 }
                }
                Text { width: parent.width; height: 16; text: "OUTPUT"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "MAIN"; checked: config.clickOutput === 0; focusId: "config.out.0"; onClicked: config.clickOutput = 0 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "PHONES"; checked: config.clickOutput === 1; focusId: "config.out.1"; onClicked: config.clickOutput = 1 }
                    TouchButton { width: (parent.width - 8) / 3; height: parent.height; text: "OUT3&4"; checked: config.clickOutput === 2; focusId: "config.out.2"; onClicked: config.clickOutput = 2 }
                }
                Row {
                    width: parent.width
                    height: 84
                    spacing: Style.gapTight
                    ConfigKnob {
                        width: 120
                        height: parent.height
                        label: "LEVEL"
                        value: config.clickGainDb
                        minimumValue: -60
                        maximumValue: 20
                        suffix: " dB"
                        popupKind: "db"
                        focusId: "config.gain"
                        onMoved: (value) => config.clickGainDb = value
                    }
                    ConfigKnob {
                        width: 120
                        height: parent.height
                        label: "COUNT-IN"
                        value: config.clickBars
                        minimumValue: 1
                        maximumValue: 8
                        suffix: " bars"
                        decimals: 0
                        popupKind: "generic"
                        focusId: "config.bars"
                        onMoved: (v) => config.clickBars = Math.round(v)
                    }
                    Text {
                        width: parent.width - 252
                        height: parent.height
                        text: "GAIN IS THE CLICK LEVEL; BARS IS ITS LENGTH WHILE THE TRANSPORT ROLLS."
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
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
                    height: 30
                    text: "PRE = gain before the fader; POST = after gain + fader. One choice per " +
                          "destination. FX sends are retired - the FX1/FX2 rows are legacy " +
                          "session values, the rack runs per track."
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
            // ---- TRACK MODE + REC SOURCE ------------------------------------
            // The mode words replace the old monitor letters: LIVE processes the
            // input, PLAY plays the take, ARM records and waits, OVERDUB layers
            // the live input over the take. REC SOURCE names where the take
            // taps the channel. Both write immediately (house rule: no apply).
            Column {
                visible: config.kind === "trackmode"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 18
                    text: "Track " + String(mixer.trackIndex + 1).padStart(2, "0") +
                          " mode"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "PLAY"; textSize: Style.fontSmall; checked: config.trackMode === 0; focusId: "config.trackmode.0"; onClicked: config.trackMode = 0 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "ARM"; textSize: Style.fontSmall; checked: config.trackMode === 1; checkedColor: Style.record; focusId: "config.trackmode.1"; onClicked: config.trackMode = 1 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "OVERDUB"; textSize: 10; checked: config.trackMode === 2; focusId: "config.trackmode.2"; onClicked: config.trackMode = 2 }
                    TouchButton { width: (parent.width - Style.gapTight * 3) / 4; height: parent.height; text: "LIVE"; textSize: Style.fontSmall; checked: config.trackMode === 3; focusId: "config.trackmode.3"; onClicked: config.trackMode = 3 }
                }
                Text {
                    width: parent.width
                    text: "LIVE is input only, PLAY is the take, ARM records and waits for REC, OVERDUB records the live layer over the take. ARM and OVERDUB record; nothing armed means nothing is recorded."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    height: 16
                    text: "REC SOURCE  " + config.trackRecSourceText
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "PRE"; textSize: Style.fontSmall; checked: config.trackRecSource === 0; focusId: "config.recsource.0"; onClicked: config.trackRecSource = 0 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "POST"; textSize: Style.fontSmall; checked: config.trackRecSource === 1; focusId: "config.recsource.1"; onClicked: config.trackRecSource = 1 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "POST+FX"; textSize: Style.fontSmall; checked: config.trackRecSource === 2; focusId: "config.recsource.2"; onClicked: config.trackRecSource = 2 }
                }
                // The one-line monitor legend: the MPC source names
                // stay documented even though the face now speaks mode words.
                Text {
                    width: parent.width
                    text: "MON input / AUTO smart / BOTH layer / PLAY takes"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- REC MODE --------------------------------------------------
            Column {
                visible: config.kind === "recmode"
                width: parent.width
                spacing: Style.gapTight

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "NOW"; textSize: Style.fontSmall; checked: config.recMode === 0; focusId: "config.recmode.0"; onClicked: config.recMode = 0 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "COUNT-IN"; textSize: Style.fontSmall; checked: config.recMode === 1; focusId: "config.recmode.1"; onClicked: config.recMode = 1 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "PUNCH"; textSize: Style.fontSmall; checked: config.recMode === 2; focusId: "config.recmode.2"; onClicked: config.recMode = 2 }
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    visible: config.recMode === 1
                    TouchButton { width: Style.touch; height: parent.height; text: "-"; textSize: 18; onClicked: config.countInBars = Math.max(1, config.countInBars - 1) }
                    KnobChip {
                        width: parent.width - Style.touch * 2 - Style.gapTight * 3
                        height: parent.height
                        label: "COUNT-IN"
                        value: config.countInBars
                        valueText: config.countInBars + " bars"
                        minimumValue: 1
                        maximumValue: 8
                        stepSize: 1
                        dialScale: 8
                        popupLabel: "COUNT-IN"
                        popupKind: "int"
                        focusId: "config.countin"
                        onMoved: (value) => config.countInBars = Math.round(value)
                    }
                    TouchButton { width: Style.touch; height: parent.height; text: "+"; textSize: 18; onClicked: config.countInBars = Math.min(8, config.countInBars + 1) }
                }
                Text {
                    width: parent.width
                    text: config.recModeSummary
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            // ---- TAKE row actions ------------------------------------------
            Column {
                visible: config.kind === "takeactions"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: "TAP A ROW TO RUN IT - THE TAKE STAYS SELECTED"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Grid {
                    width: parent.width
                    columns: 2
                    spacing: Style.gapTight
                    Repeater {
                        model: config.takeActions
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight) / 2
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            variant: modelData === "DELETE" ? "danger" : "secondary"
                            focusId: "config.takeaction." + index
                            onClicked: config.chooseTakeAction(index)
                        }
                    }
                }
            }

            // ---- NEW TRACK FROM TAKE: MOVE / KEEP ON SOURCE -----------------
            Column {
                visible: config.kind === "trackfromtake"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    text: config.trackFromTakeText
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    wrapMode: Text.Wrap
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight) / 2; height: parent.height; text: "MOVE"; textSize: Style.fontSmall; variant: "primary"; focusId: "config.trackfromtake.0"; onClicked: config.chooseTrackFromTake(0) }
                    TouchButton { width: (parent.width - Style.gapTight) / 2; height: parent.height; text: "KEEP ON SOURCE"; textSize: Style.fontSmall; focusId: "config.trackfromtake.1"; onClicked: config.chooseTrackFromTake(1) }
                }
                Text {
                    width: parent.width
                    text: "MOVE is the default: the new track plays the take and the source track stops. KEEP ON SOURCE leaves both tracks pointing at the same file."
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

            // ---- INPUT SOURCE (tap-to-open option list) -------------------
            Column {
                visible: config.kind === "inputsource" || config.kind === "outputdest" ||
                         config.kind === "fxslot"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: config.kind === "inputsource"
                          ? "MONO DEVICE CHANNELS OR A STEREO PAIR; NONE IS SILENT"
                          : (config.kind === "outputdest"
                             ? "MAIN 1&2, OUT3&4, PHONES OR NONE"
                             : "ONE EFFECT PER SLOT; WET AND BYPASS LIVE IN THE TRACK FX POPUP")
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }

                // The source list follows the model's device width, so it is
                // scrollable instead of assuming a fixed number of entries.
                Flickable {
                    id: inputSourceScroll
                    visible: config.kind === "inputsource"
                    width: parent.width
                    // Seven rows fit the 480 px popup at today's device width;
                    // a wider registry scrolls instead of shrinking a target.
                    height: Math.min(inputSourceGrid.height,
                                     Style.touch * 7 + Style.gapTight * 6)
                    contentWidth: width
                    contentHeight: inputSourceGrid.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Grid {
                        id: inputSourceGrid
                        width: parent.width
                        columns: 4
                        spacing: Style.gapTight
                        readonly property int rows: Math.ceil(config.inputSourceLabels.length / 4)
                        height: rows * Style.touch + Math.max(0, rows - 1) * Style.gapTight

                        Repeater {
                            model: config.inputSourceLabels
                            delegate: TouchButton {
                                width: (parent.width - Style.gapTight * 3) / 4
                                height: Style.touch
                                text: modelData
                                textSize: Style.fontSmall
                                checked: index === config.inputSourceIndex
                                focusId: "config.input." + index
                                onClicked: config.inputSourceIndex = index
                            }
                        }
                    }
                }

                Row {
                    visible: config.kind === "outputdest"
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    Repeater {
                        model: config.outputLabels
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.outputIndex
                            focusId: "config.output." + index
                            onClicked: config.outputIndex = index
                        }
                    }
                }

                Column {
                    visible: config.kind === "fxslot"
                    width: parent.width
                    spacing: Style.gapTight

                    // One effect per slot: three built-ins then every discovered
                    // CLAP plugin. A longer plugin list scrolls instead of
                    // shrinking the 44 px targets.
                    Flickable {
                        id: fxSlotScroll
                        width: parent.width
                        height: Math.min(fxSlotGrid.height,
                                         Style.touch * 2 + Style.gapTight)
                        contentWidth: width
                        contentHeight: fxSlotGrid.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Grid {
                            id: fxSlotGrid
                            width: parent.width
                            columns: 4
                            spacing: Style.gapTight
                            readonly property int rows: Math.ceil(config.fxSlotLabels.length / 4)
                            height: rows * Style.touch + Math.max(0, rows - 1) * Style.gapTight

                            Repeater {
                                model: config.fxSlotLabels
                                delegate: TouchButton {
                                    width: (fxSlotGrid.width - Style.gapTight * 3) / 4
                                    height: Style.touch
                                    text: modelData
                                    textSize: Style.fontSmall
                                    checked: index === config.fxSlotChoice
                                    focusId: "config.fxslot." + index
                                    onClicked: config.fxSlotChoice = index
                                }
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        height: 16
                        text: config.fxSlotNote
                        color: config.fxSlotNote.indexOf("LIMIT") >= 0
                               ? Style.record : Style.textDim
                        font.pixelSize: Style.fontSmall
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- ADD TRACK ------------------------------------------------
            Column {
                visible: config.kind === "addtrack"
                width: parent.width
                spacing: Style.gapTight

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    TouchButton {
                        width: 150
                        height: parent.height
                        text: "NAME"
                        textSize: Style.fontSmall
                        focusId: "config.addtrack.name"
                        onClicked: app.requestAddTrackName()
                    }
                    Text {
                        width: parent.width - 158
                        height: parent.height
                        text: config.addTrackSummary
                        color: Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Text { width: parent.width; height: 16; text: "INPUT"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "NONE"; textSize: Style.fontSmall; checked: config.addTrackInputMode === 0; focusId: "config.addtrack.mode.0"; onClicked: config.addTrackInputMode = 0 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "MONO 1-18"; textSize: Style.fontSmall; checked: config.addTrackInputMode === 1; focusId: "config.addtrack.mode.1"; onClicked: config.addTrackInputMode = 1 }
                    TouchButton { width: (parent.width - Style.gapTight * 2) / 3; height: parent.height; text: "STEREO PAIR"; textSize: Style.fontSmall; checked: config.addTrackInputMode === 2; focusId: "config.addtrack.mode.2"; onClicked: config.addTrackInputMode = 2 }
                }
                // Channel count comes from the model's device width; the grid
                // scrolls when the device exposes more inputs than fit here.
                Flickable {
                    id: addTrackChannelScroll
                    width: parent.width
                    height: Math.min(addTrackChannelGrid.height,
                                     Style.touch * 2 + Style.gapTight)
                    contentWidth: width
                    contentHeight: addTrackChannelGrid.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Item {
                        id: addTrackChannelGrid
                        width: parent.width
                        readonly property int count: config.addTrackChannelCount
                        readonly property int rows: Math.ceil(count / 9)
                        height: rows * Style.touch + Math.max(0, rows - 1) * Style.gapTight

                        Repeater {
                            model: addTrackChannelGrid.count
                            delegate: TouchButton {
                                width: (parent.width - Style.gapTight * 8) / 9
                                height: Style.touch
                                x: (index % 9) * (width + Style.gapTight)
                                y: Math.floor(index / 9) * (height + Style.gapTight)
                                text: String(index + 1).padStart(2, "0")
                                textSize: Style.fontSmall
                                checked: config.addTrackInputMode !== 0 &&
                                         index === config.addTrackChannel
                                active: config.addTrackInputMode !== 0
                                focusId: "config.addtrack.ch." + index
                                onClicked: config.addTrackChannel = index
                            }
                        }
                    }
                }
                Text { width: parent.width; height: 16; text: "OUTPUT"; color: Style.textDim; font.pixelSize: Style.fontSmall; font.weight: Font.DemiBold }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    Repeater {
                        model: config.outputLabels
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.addTrackOutput
                            focusId: "config.addtrack.out." + index
                            onClicked: config.addTrackOutput = index
                        }
                    }
                }
                Text {
                    width: parent.width
                    height: 16
                    text: config.addTrackError
                    visible: config.addTrackError !== ""
                    color: Style.record
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }

            // ---- PIPE CHAIN PORT SELECTORS ---------------------------------
            Column {
                visible: config.kind === "pipesource" || config.kind === "pipedest"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: config.kind === "pipesource"
                          ? "CHAIN SOURCE - EVERY MESSAGE FROM THIS PORT RUNS THE CHAIN"
                          : "CHAIN TARGET - THE SELECTED PORT RECEIVES THE CHAIN OUTPUT"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Grid {
                    visible: config.kind === "pipesource"
                    width: parent.width
                    columns: 4
                    spacing: Style.gapTight

                    Repeater {
                        model: config.pipeSourceLabels
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.pipeSourceIndex
                            focusId: "config.pipesource." + index
                            onClicked: config.pipeSourceIndex = index
                        }
                    }
                }
                Grid {
                    visible: config.kind === "pipedest"
                    width: parent.width
                    columns: 4
                    spacing: Style.gapTight

                    Repeater {
                        model: config.pipeDestLabels
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.pipeDestIndex
                            focusId: "config.pipedest." + index
                            onClicked: config.pipeDestIndex = index
                        }
                    }
                }
            }

            // ---- PIPE TYPE: name plus the schema role of every type -------
            // The list scrolls: the schema owns 16 types and each row states
            // what the type does in the chain (schema "role"), so the pick is
            // made from the description, not from a remembered order.
            Column {
                visible: config.kind === "pipetype"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: config.pipeTypeHeadOnly
                          ? "GENERATOR HEAD - ONLY A GENERATOR TYPE CAN START A SOURCE-LESS CHAIN"
                          : "PIPE TYPE - ADD APPENDS THE HIGHLIGHTED TYPE TO THE CHAIN"
                    color: config.pipeTypeHeadOnly ? Style.accent : Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                ListView {
                    id: pipeTypeList
                    width: parent.width
                    height: Math.min(contentHeight, Style.touch * 6 + Style.gapHair * 5)
                    clip: true
                    cacheBuffer: 0
                    model: config.pipeTypeLabels
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: Style.gapHair
                    // The armed type is somewhere in the 16 rows: open the list
                    // on it instead of on the top of the schema.
                    onVisibleChanged: if (visible) positionViewAtIndex(
                                          config.pipeTypeIndex, ListView.Center)

                    delegate: Row {
                        width: pipeTypeList.width
                        height: Style.touch
                        spacing: Style.gapTight

                        TouchButton {
                            width: 240
                            height: parent.height
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.pipeTypeIndex
                            focusId: "config.pipetype." + index
                            onClicked: config.pipeTypeIndex = index
                        }
                        Text {
                            width: parent.width - 248
                            height: parent.height
                            text: config.pipeTypeRoles[index] !== undefined
                                  ? config.pipeTypeRoles[index] : ""
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
                Text {
                    width: parent.width
                    height: 28
                    text: config.pipeTypeDocs
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            // ---- PIPE PARAMETER CHOICE ------------------------------------
            // A bool/enum pipe parameter opens this list; the tap writes the
            // parameter, so OK/CANCEL behave like every other selector popup.
            Column {
                visible: config.kind === "pipeparam"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: config.pipeParamContext
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Grid {
                    id: pipeParamGrid
                    readonly property int choiceColumns: Math.max(1,
                                                                 Math.min(4, config.pipeParamChoices.length))
                    width: parent.width
                    columns: pipeParamGrid.choiceColumns
                    spacing: Style.gapTight

                    Repeater {
                        model: config.pipeParamChoices
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * (pipeParamGrid.choiceColumns - 1))
                                   / pipeParamGrid.choiceColumns
                            height: Style.touch
                            text: modelData
                            textSize: Style.fontSmall
                            checked: index === config.pipeParamChoice
                            focusId: "config.pipeparam." + index
                            onClicked: config.setPipeParamChoice(index)
                        }
                    }
                }
                Text {
                    width: parent.width
                    height: 16
                    text: "TAP A CHOICE - OK KEEPS IT, CANCEL RESTORES THE PIPE PARAMETER"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }

            // ---- CHAIN MENU ------------------------------------------------
            // The selected route's chain preset file actions; they live here
            // so the pipe row keeps one selector, ADD, the scope chip and this
            // button.
            Column {
                visible: config.kind === "chainmenu"
                width: parent.width
                spacing: Style.gapTight

                Text {
                    width: parent.width
                    height: 16
                    text: routing.pipeRouteText + " - " + routing.pipeCount + " PIPE(S) IN THE CHAIN"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    TouchButton {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        text: "EXPORT CHAIN"
                        focusId: "shift.routing.pipes.chain.export"
                        onClicked: {
                            app.configCancel()
                            app.requestExportPipeChain()
                        }
                    }
                    TouchButton {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        text: "IMPORT CHAIN"
                        focusId: "shift.routing.pipes.chain.import"
                        onClicked: {
                            app.configCancel()
                            app.requestImportPipeChain()
                        }
                    }
                }
                Text {
                    width: parent.width
                    height: 16
                    text: "ONE JSON FILE PER ROUTE CHAIN, NAMED WITH THE SHARED KEYPAD"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }

            // ---- DELAY DIVISION / REVERB MODEL selector lists --------------
            Column {
                visible: config.kind === "delaydivision" || config.kind === "reverbmodel"
                width: parent.width
                spacing: Style.gapTight

                Grid {
                    visible: config.kind === "delaydivision"
                    width: parent.width
                    columns: 4
                    spacing: Style.gapTight

                    Repeater {
                        model: 8
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: ["1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1"][index]
                            textSize: Style.fontSmall
                            checked: index === config.delayDivision
                            focusId: "config.delaydivision." + index
                            onClicked: config.delayDivision = index
                        }
                    }
                }
                Row {
                    visible: config.kind === "reverbmodel"
                    width: parent.width
                    height: Style.touch
                    spacing: Style.gapTight

                    Repeater {
                        model: 4
                        delegate: TouchButton {
                            width: (parent.width - Style.gapTight * 3) / 4
                            height: Style.touch
                            text: ["CORE", "ROOM", "HALL", "PLATE"][index]
                            textSize: Style.fontSmall
                            checked: index === config.reverbModel
                            focusId: "config.reverbmodel." + index
                            onClicked: config.reverbModel = index
                        }
                    }
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
                    Text {
                        width: (parent.width - 8) / 3
                        height: parent.height
                        text: "DIV  " + config.divisionText
                        color: Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                // The division is a list, not a prev/next stepper: every note
                // value is one tap away.
                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 2

                    Repeater {
                        model: 8
                        delegate: TouchButton {
                            width: (parent.width - 14) / 8
                            height: Style.touch
                            text: ["1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1"][index]
                            textSize: Style.fontSmall
                            checked: index === config.delayDivision
                            focusId: "config.delaydivision." + index
                            onClicked: config.delayDivision = index
                        }
                    }
                }
                Row {
                    width: parent.width
                    height: 84
                    spacing: Style.gapTight
                    ConfigKnob {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        label: "FEEDBACK"
                        value: config.delayFeedback
                        minimumValue: 0
                        maximumValue: 0.95
                        popupKind: "percent"
                        focusId: "config.delay.feedback"
                        onMoved: (v) => config.delayFeedback = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        label: "MIX"
                        value: config.delayMix
                        minimumValue: 0
                        maximumValue: 1
                        popupKind: "percent"
                        focusId: "config.delay.mix"
                        onMoved: (v) => config.delayMix = v
                    }
                }
                Text {
                    width: parent.width
                    text: "This track's FX slot. The grid owns cutoff (X) and width (Y); these are the delay's other controls: Beat syncs Time to the clock and Division picks the note value."
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
                Row {
                    width: parent.width
                    height: 84
                    spacing: Style.gapTight
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "PREDELAY"
                        value: config.reverbPredelayMs
                        minimumValue: 0
                        maximumValue: 200
                        suffix: " ms"
                        popupKind: "ms"
                        focusId: "config.reverb.predelay"
                        onMoved: (v) => config.reverbPredelayMs = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "DECAY"
                        value: config.reverbDecay
                        minimumValue: 0
                        maximumValue: 0.98
                        popupKind: "generic"
                        focusId: "config.reverb.decay"
                        onMoved: (v) => config.reverbDecay = v
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight * 2) / 3
                        height: parent.height
                        label: "DAMPING"
                        value: config.reverbDamping
                        minimumValue: 0
                        maximumValue: 1
                        popupKind: "generic"
                        focusId: "config.reverb.damping"
                        onMoved: (v) => config.reverbDamping = v
                    }
                }
                Text {
                    width: parent.width
                    text: "This track's FX slot. The grid owns size (X) and level (Y); Freeze is the macro: longest decay, fully damped-free, largest size, fully wet; releasing restores these values."
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
                    height: 16
                    text: "messages on other channels bypass this pipe"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
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

                // the op's range and its validity are read before the run,
                // not reported as a refusal after it.
                Text {
                    width: parent.width
                    height: 16
                    text: config.regionLine
                    color: Style.text
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: visible ? 16 : 0
                    visible: !config.regionApplies
                    text: config.regionReason
                    color: Style.record
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }

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
                    TouchButton { width: (parent.width - 6) / 3; height: parent.height; text: "NORM"; textSize: Style.fontSmall; checked: config.timelineOp === 4; focusId: "config.timeline.op.4"; onClicked: config.timelineOp = 4 }
                    TouchButton { width: (parent.width - 6) / 3; height: parent.height; text: "DELETE"; textSize: Style.fontSmall; checked: config.timelineOp === 5; focusId: "config.timeline.op.5"; onClicked: config.timelineOp = 5 }
                    TouchButton { width: (parent.width - 6) / 3; height: parent.height; text: "CLEAR"; textSize: Style.fontSmall; checked: config.timelineOp === 6; focusId: "config.timeline.op.6"; onClicked: config.timelineOp = 6 }
                }
                // Only the fields the chosen op reads are shown.
                Row {
                    width: parent.width
                    height: config.showGainField ? 84 : 0
                    visible: config.showGainField
                    ConfigKnob {
                        width: parent.width
                        height: parent.height
                        label: "GAIN"
                        value: config.timelineGainDb
                        minimumValue: -96
                        maximumValue: 12
                        suffix: " dB"
                        popupKind: "db"
                        focusId: "config.timeline.gain"
                        onMoved: (value) => config.timelineGainDb = value
                    }
                }
                Row {
                    width: parent.width
                    height: config.showFadeFields ? 84 : 0
                    visible: config.showFadeFields
                    spacing: Style.gapTight
                    ConfigKnob {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
                        label: "FADE IN"
                        value: config.timelineFadeInBeats
                        minimumValue: 0
                        maximumValue: 16
                        suffix: " b"
                        popupKind: "generic"
                        focusId: "config.timeline.fadein"
                        onMoved: (value) => config.timelineFadeInBeats = value
                    }
                    ConfigKnob {
                        width: (parent.width - Style.gapTight) / 2
                        height: parent.height
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
                    height: config.showRegionPreview ? 64 : 0
                    visible: config.showRegionPreview
                    Rectangle {
                        visible: timeline.previewWave.length > 0
                        anchors.fill: parent
                        color: Style.panelDark
                        radius: Style.corner
                        border.width: Style.outlineWidth
                        border.color: Style.outline
                    }
                    TimelineWave {
                        visible: timeline.previewWave.length > 0
                        anchors.fill: parent
                        anchors.margins: 3
                        values: timeline.previewWave
                        showEnvelope: true
                        gainDb: config.timelineGainDb
                        fadeInRatio: timeline.previewLengthBeats > 0
                                      ? config.timelineFadeInBeats / timeline.previewLengthBeats : 0
                        fadeOutRatio: timeline.previewLengthBeats > 0
                                      ? config.timelineFadeOutBeats / timeline.previewLengthBeats : 0
                    }
                    // a missing peaks sidecar is an action, not a dead
                    // preview block.
                    TouchButton {
                        visible: timeline.previewWave.length === 0
                        anchors.fill: parent
                        text: "RENDER PEAKS"
                        textSize: Style.fontSmall
                        onClicked: app.showTextPopup("PEAKS", timeline.renderPeaks())
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
                    text: "TRIM/DELETE use IN..OUT. Preview needs a peaks sidecar."
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
                // bake IN..OUT into a new take and drop it on a new track.
                TouchButton {
                    width: parent.width
                    height: Style.touch
                    text: "RENDER REGION TO TRACK"
                    textSize: Style.fontSmall
                    active: config.regionApplies
                    focusId: "config.renderregion"
                    onClicked: {
                        app.configCancel()
                        app.renderRegionToTrack()
                    }
                }
            }

            Item { width: 1; height: 2 }

            Row {
                width: parent.width
                // Sheet footer: the two exits are a full 64 px row; CANCEL
                // is a common action (ghost), not a destructive red verb.
                height: Style.touchPrimary
                spacing: 4
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: "CANCEL"
                    variant: "ghost"
                    focusId: "config.cancel"
                    onClicked: app.configCancel()
                }
                // the button is its verb; the header repeats it. D5: a
                // region op with an unusable range is dimmed and says why
                // above instead of failing after the tap.
                TouchButton {
                    width: (parent.width - Style.gapTight) / 2
                    height: parent.height
                    text: config.acceptLabel
                    variant: "primary"
                    active: config.kind !== "timelineops" || config.regionApplies
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
        // Decimal places of the card's value text: an integer control (the
        // click BARS) passes 0.
        property int decimals: maximumValue > 20 ? 0 : 1
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
            text: Number(parent.value).toFixed(parent.decimals) + parent.suffix
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
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
