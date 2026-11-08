import QtQuick
import Mnkybox 1.0
import "../components"

// Mixer screen with two views of the same strips: MIXER (one setting across
// many tracks) and TRACK (one track with many settings). The mixer is the
// session's dynamic track list, three strips per page: ADD TRACK appends a
// strip, each strip's CH control opens the full channel popout (where pan,
// input, sends, the mode selector and REMOVE TRACK live, confirmed), and every
// selector opens its option list. SHIFT switches to the configure face:
// per-strip Gain/FX/CUE/OUT34/mode for MIXER, and the TRACK I/O, sends and
// monitor summaries for TRACK.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touchSecondary
            spacing: Style.gap

            TouchButton {
                width: 96
                height: parent.height
                text: "MIXER"
                textSize: Style.fontSecondary
                checked: !mixer.trackMode
                focusId: "mixer.mode.0"
                onClicked: mixer.trackMode = false
            }
            TouchButton {
                width: 96
                height: parent.height
                text: "TRACK"
                textSize: Style.fontSecondary
                checked: mixer.trackMode
                focusId: "mixer.mode.1"
                onClicked: mixer.trackMode = true
            }
            Row {
                width: 264
                height: parent.height
                spacing: Style.gapTight

                TouchButton {
                    width: Style.touchSecondary
                    height: parent.height
                    text: "<"
                    textSize: 18
                    focusId: "mixer.bank.0"
                    onClicked: mixer.stepBank(-1)
                }
                // the label is the pager. It opens the page grid so page
                // 16 is one tap away; the arrows keep stepping one page.
                TouchButton {
                    width: parent.width - Style.touchSecondary * 2 - Style.gapTight * 2
                    height: parent.height
                    text: mixer.bankText
                    textSize: Style.fontSecondary
                    variant: "ghost"
                    chevron: "down"
                    focusId: "mixer.bank.pages"
                    onClicked: app.openBankGrid()
                }
                TouchButton {
                    width: Style.touchSecondary
                    height: parent.height
                    text: ">"
                    textSize: 18
                    focusId: "mixer.bank.1"
                    onClicked: mixer.stepBank(1)
                }
            }
            TouchButton {
                width: 120
                height: parent.height
                text: "ADD TRACK"
                textSize: Style.fontSecondary
                variant: "primary"
                focusId: "mixer.add"
                onClicked: app.requestAddTrack()
            }
            Text {
                width: parent.width - 96 - 96 - 264 - 120 - Style.gap * 4
                height: parent.height
                text: screen.shiftFace ? "SHIFT  configure"
                      : (mixer.trackMode ? mixer.trackName + " - ^ = channel"
                                         : (mixer.empty ? "no tracks yet"
                                                        : "tap name = select"))
                color: screen.shiftFace ? Style.accent : Style.textDim
                font.pixelSize: Style.fontSecondary
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // ---- MIXER primary: perform surface -------------------------------
        Item {
            visible: !mixer.trackMode && !screen.shiftFace
            width: parent.width
            height: parent.height - Style.touchSecondary - Style.gap

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
                        text: "EMPTY MIXER - tap ADD TRACK to create your first channel"
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: mixer
                        delegate: MixerStrip {
                            readonly property real cellWidth: (stripsArea.width -
                                Style.gap * (mixer.stripsPerBank - 1)) / mixer.stripsPerBank
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? cellWidth : 0
                            height: stripsArea.height
                            x: visible ? (index % mixer.stripsPerBank) *
                                         (cellWidth + Style.gap) : 0
                            stripIndex: model.slot
                            stripName: model.name
                            gainDb: model.gainDb
                            level: model.level
                            clip: model.clip
                            mode: model.mode
                            modeWord: model.modeWord
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
                            height: 20
                            text: "MASTER"
                            color: Style.text
                            font.pixelSize: Style.fontLabel
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        Row {
                            width: implicitWidth
                            height: 30
                            x: (parent.width - width) / 2
                            spacing: 3

                            Text {
                                height: parent.height
                                text: Number(mixer.masterDb).toFixed(1)
                                color: Style.text
                                font.pixelSize: Style.fontValue
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                height: parent.height
                                text: "dB"
                                color: Style.textDim
                                font.pixelSize: Style.fontSecondary
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        TouchSlider {
                            id: masterGain
                            width: Style.touchPrimary
                            height: parent.height - 20 - 30 - 72 - Style.gapTight * 3
                            x: (parent.width - width) / 2
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
                            height: 72
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
            height: parent.height - Style.touchSecondary - Style.gap

            Item {
                id: shiftStripsArea
                width: parent.width
                height: parent.height

                Repeater {
                    model: mixer
                    delegate: MixerShiftStrip {
                        readonly property real cellWidth: (shiftStripsArea.width -
                            Style.gap * (mixer.stripsPerBank - 1)) / mixer.stripsPerBank
                        visible: index >= mixer.bank * mixer.stripsPerBank &&
                                 index < (mixer.bank + 1) * mixer.stripsPerBank
                        width: visible ? cellWidth : 0
                        height: shiftStripsArea.height - 30
                        x: visible ? (index % mixer.stripsPerBank) *
                                     (cellWidth + Style.gap) : 0
                        stripIndex: model.slot
                        stripName: model.name
                        fx1Wet: model.fx1Wet
                        fx2Wet: model.fx2Wet
                        fx1Name: model.fx1Name
                        fx2Name: model.fx2Name
                        mode: model.mode
                        modeWord: model.modeWord
                    }
                }

                // one colour legend for the three strip switches.
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 30
                    text: "solo green - mute gray - arm red - MON input - AUTO smart - BOTH layer - PLAY take" +
                          "  -  tap a meter to clear its clip hold"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: 2
                }
            }
        }

        // ---- TRACK primary: essentials -----------------------------------
        Item {
            visible: mixer.trackMode && !screen.shiftFace
            width: parent.width
            height: parent.height - Style.touchSecondary - Style.gap

            Column {
                width: parent.width
                height: parent.height
                spacing: Style.gap

                Item {
                    width: parent.width
                    height: Style.touch

                    // The selector is the current page of three: the header's
                    // page buttons walk the same list the MIXER face banks.
                    readonly property int selectWidth: mixer.empty ? width
                                                                    : width - Style.touch -
                                                                      Style.gapTight
                    Repeater {
                        model: mixer
                        delegate: TouchButton {
                            visible: index >= mixer.bank * mixer.stripsPerBank &&
                                     index < (mixer.bank + 1) * mixer.stripsPerBank
                            width: visible ? parent.selectWidth / mixer.stripsPerBank : 0
                            height: Style.touch
                            x: visible ? (index % mixer.stripsPerBank) * width : 0
                            text: model.name
                            textSize: Style.fontSecondary
                            checked: model.slot === mixer.trackIndex
                            focusId: "track.select." + model.slot
                            onClicked: mixer.setTrackIndex(model.slot)
                        }
                    }

                    // the same channel entry point as a strip header, with
                    // its payoff named by the header hint.
                    TouchButton {
                        visible: !mixer.empty
                        width: Style.touch
                        height: parent.height
                        anchors.right: parent.right
                        text: ""
                        chevron: "up"
                        variant: "ghost"
                        focusId: "track.expand"
                        onClicked: app.openChannel(mixer.trackIndex)
                        onLongClicked: app.showTextPopup(
                            "CHANNEL",
                            "fader, pan, sends, monitor, I/O, insert, EQ")
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
                    valueText: Number(mixer.trackGainDb).toFixed(1)
                    suffix: " dB"
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
                    valueText: mixer.trackPanText
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
                        text: "ARM"
                        textSize: Style.fontSmall
                        checked: mixer.trackArm
                        checkedColor: Style.record
                        focusId: "track.arm"
                        onClicked: mixer.setTrackArm(!mixer.trackArm)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "MUTE"
                        textSize: Style.fontSmall
                        checked: mixer.trackMute
                        checkedColor: Style.textDim
                        checkedTextColor: Style.text
                        focusId: "track.mute"
                        onClicked: mixer.setTrackMute(!mixer.trackMute)
                    }
                    TouchButton {
                        width: (parent.width - Style.gap * 2) / 3
                        height: parent.height
                        text: "SOLO"
                        textSize: Style.fontSmall
                        checked: mixer.trackSolo
                        checkedColor: Style.ok
                        checkedTextColor: Style.panelDark
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
            height: parent.height - Style.touchSecondary - Style.gap

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
                            valueText: Number(mixer.trackTrimDb).toFixed(1)
                            suffix: " dB"
                            minimumValue: -24
                            maximumValue: 24
                            popupKind: "db"
                            focusId: "shift.track.gain"
                            onMoved: (value) => mixer.setTrackTrimDb(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "FX1 WET"
                            value: mixer.trackFxWet1
                            valueText: Math.round(mixer.trackFxWet1 * 100)
                            suffix: "%"
                            popupLabel: mixer.trackName + " FX1 WET"
                            popupKind: "percent"
                            focusId: "shift.track.fx1"
                            onMoved: (value) => mixer.setTrackFxWet1(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "FX2 WET"
                            value: mixer.trackFxWet2
                            valueText: Math.round(mixer.trackFxWet2 * 100)
                            suffix: "%"
                            popupLabel: mixer.trackName + " FX2 WET"
                            popupKind: "percent"
                            focusId: "shift.track.fx2"
                            onMoved: (value) => mixer.setTrackFxWet2(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "CUE"
                            value: mixer.trackCue
                            valueText: Math.round(mixer.trackCue * 100)
                            suffix: "%"
                            popupKind: "percent"
                            focusId: "shift.track.cue"
                            onMoved: (value) => mixer.setTrackCue(value)
                        }
                        TrackSliderRow {
                            width: parent.width
                            label: "OUT34"
                            value: mixer.trackOut34
                            valueText: Math.round(mixer.trackOut34 * 100)
                            suffix: "%"
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
                            text: "MODE   " + mixer.selectedModeWord
                            textSize: Style.fontSmall
                            chevron: "down"
                            focusId: "shift.track.mode"
                            onClicked: app.openTrackMode(mixer.trackIndex)
                        }
                        TouchButton {
                            width: parent.width
                            height: Style.touch
                            text: "TRACK FX   " + mixer.channelFxSummary
                            textSize: Style.fontSmall
                            chevron: "up"
                            focusId: "shift.track.fx"
                            onClicked: app.openTrackFx(0)
                        }
                        Text {
                            width: parent.width
                            text: "FX1/FX2 are the slot wet; TRACK FX opens the rack (series FX1 -> FX2). " +
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

    // the page grid. One 44 px cell per page, current page outlined, a
    // page past the track list dimmed and unselectable. Arrows keep stepping.
    Rectangle {
        id: bankGrid

        visible: app.bankGridOpen
        anchors.fill: parent
        color: "#9e000000"
        z: 70

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        readonly property int pageColumns: 4
        readonly property int pageRows: Math.ceil(mixer.bankPages.length / pageColumns)

        Rectangle {
            width: 236
            // The title row, the page rows and the Column's 10 px top/bottom
            // margins: a 64-track capacity is 22 pages, six rows, 356 px.
            height: Math.min(parent.height - 4,
                             44 + Style.gap + bankGrid.pageRows * Style.touch +
                             (bankGrid.pageRows - 1) * Style.gapTight + 20)
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
                    height: Style.touch

                    Text {
                        width: parent.width - 92
                        height: parent.height
                        text: "MIXER PAGE"
                        color: Style.text
                        font.pixelSize: Style.fontTitle
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: "CLOSE"
                        textSize: Style.fontSmall
                        variant: "ghost"
                        focusId: "bank.page.close"
                        onClicked: app.closeBankGrid()
                    }
                }

                Grid {
                    width: parent.width
                    columns: bankGrid.pageColumns
                    spacing: Style.gapTight

                    Repeater {
                        model: mixer.bankPages
                        delegate: Rectangle {
                            readonly property bool current: modelData.current
                            width: (parent.width - Style.gapTight *
                                    (bankGrid.pageColumns - 1)) / bankGrid.pageColumns
                            height: Style.touch
                            radius: Style.corner
                            color: current ? Style.accent
                                           : (modelData.empty ? Style.panelDark : Style.panel)
                            border.width: current ? 2 : Style.outlineWidth
                            border.color: current ? Style.accent : Style.outline
                            opacity: modelData.empty ? 0.4 : 1.0
                            property string focusId: modelData.empty
                                                    ? "" : "bank.page." + modelData.index

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: parent.current ? Style.panelDark : Style.text
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                            }
                            TapHandler {
                                enabled: !modelData.empty
                                onTapped: app.chooseBankPage(modelData.index)
                            }
                        }
                    }
                }
            }
        }
    }

    // Label + value row used by the TRACK views: an arc-dial chip, because the
    // refresh law keeps sliders for the dedicated wide faders (the strip and
    // master faders). The call site supplies the readout and moved().
    component TrackSliderRow: KnobChip {
        height: Style.touch
    }
}
