import QtQuick
import Mnkybox 1.0
import "../components"

// RECORDER primary: transport, track selection, arm/start and the take list
// with a 44 px mini-waveform per row and an EDIT affordance that opens the take
// edit popup (plan W5). The ACTIVE readout opens TAKE FOR TRACK nn - the sheet
// that used to live on the retired EDIT tab. SHIFT keeps take management
// (import/live/active/rename/delete), the take loop and punch markers.
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
            height: Style.touch
            spacing: Style.gap

            // A6: REC / PLAY / STOP only, in the state's colour. The dim line
            // below says what the state does not (playing a take / inputs live).
            Text {
                width: parent.width * 0.55
                height: parent.height
                text: recorder.transportText
                color: recorder.recording ? Style.record
                       : (app.rolling ? Style.accent : Style.textDim)
                font.pixelSize: Style.fontDisplay
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                id: activeReadout
                width: parent.width - parent.width * 0.55 - Style.gap
                height: parent.height
                text: "ACTIVE  " + recorder.activeTake
                color: recorder.activeTake === "LIVE INPUT" ? Style.textDim : Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
                // The take-sheet opener: the sheet left the retired EDIT tab and
                // now lives here, on the surface that owns the takes.
                property string focusId: "recorder.takesheet"
                TapHandler {
                    onTapped: app.openTakeSheet(recorder.selectedTrack)
                }
            }
        }

        Text {
            width: parent.width
            height: recorder.transportHint !== "" ? 14 : 0
            visible: recorder.transportHint !== ""
            text: recorder.transportHint
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }

        Item {
            width: parent.width
            // One row of nine per line; the row count and the arm state come
            // from the recorder's own arm model, not a literal.
            height: Math.ceil(recorder.arms.count / 9) * Style.touch

            Repeater {
                model: recorder.arms
                delegate: TouchButton {
                    width: parent.width / 9
                    height: Style.touch
                    x: (index % 9) * width
                    y: Math.floor(index / 9) * Style.touch
                    text: String(index + 1).padStart(2, "0")
                    textSize: Style.fontSmall
                    checked: index === recorder.selectedTrack
                    focusId: "recorder.track." + index
                    onClicked: recorder.setSelectedTrack(index)

                    // C2: one track identity - the mixer's name on a second
                    // 9 px line, so the selector and the strip agree.
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 12
                        text: model.name
                        color: index === recorder.selectedTrack ? Style.panelDark
                                                                : Style.textDim
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: 120
                height: parent.height
                text: recorder.trackName
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            // W1: the mode word replaces the ARM toggle (ARM is a mode, not a
            // hidden switch). Tap cycles PLAY -> ARM -> OVERDUB -> LIVE; a long
            // press picks from the list.
            TouchButton {
                width: 78
                height: parent.height
                text: recorder.trackModeWord
                textSize: Style.fontSmall
                checked: recorder.trackArm
                checkedColor: Style.record
                focusId: "recorder.mode"
                onClicked: app.cycleTrackMode(recorder.selectedTrack)
                onLongClicked: app.openTrackMode(recorder.selectedTrack)
            }
            TouchButton {
                width: 140
                height: parent.height
                text: recorder.buttonText
                textSize: Style.fontSmall
                variant: "danger"
                focusId: "recorder.start"
                onClicked: recorder.startStop()
            }
            // W2b: the tap point the take captures, beside the mode word.
            Text {
                width: 76
                height: parent.height
                text: "SRC " + recorder.trackRecSourceText
                color: recorder.trackRecSourceText === "PRE" ? Style.textDim
                                                             : Style.accent
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                TapHandler {
                    onTapped: app.openTrackMode(recorder.selectedTrack)
                }
            }
            Text {
                width: parent.width - 120 - 78 - 76 - 140 - Style.gap * 4
                height: parent.height
                text: "SHIFT > takes + punch"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Text {
            width: parent.width
            height: recorder.statusWarning ? 34 : 16
            text: recorder.statusText
            color: recorder.statusWarning ? Style.record : Style.textDim
            font.pixelSize: recorder.statusWarning ? 16 : Style.fontSmall
            font.weight: recorder.statusWarning ? Font.DemiBold : Font.Normal
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            height: 14
            text: recorder.diskText + "  -  " + recorder.takeDirectory
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideMiddle
        }

        ListView {
            id: takeList
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 - Style.touch * 4 -
                                16 - 14 - 18 - Style.gap * 6)
            clip: true
            model: recorder.takes
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: ListView.view.width
                height: Style.touch
                radius: Style.corner
                // The row body is the ACTIVE tap target and the panel ring stop;
                // it carries the id as a property so the headless tap gate
                // clicks exactly this surface (the owner's crash path).
                property string focusId: "recorder.take." + index
                color: index === recorder.selectedRow ? Style.raised
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === focusId ? 2 : 0
                border.color: Style.accent

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 4
                    radius: 2
                    color: model.active ? Style.accent : "transparent"
                }
                Text {
                    x: 10
                    width: 56
                    height: parent.height
                    text: model.active ? "ACTIVE" : ""
                    color: Style.accent
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    x: 72
                    width: parent.width * 0.30
                    height: parent.height
                    text: (model.imported ? "PTR  " : (model.master ? "MASTER  " : "")) + model.name
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                }
                // W5: the row's mini-waveform (44 px strip). Tapping it opens
                // the take edit popup; the model carries the .peaks reduction,
                // so the row paints without touching the disk.
                Item {
                    id: miniWave
                    x: 72 + parent.width * 0.30 + 6
                    width: 132
                    height: Style.touch - 8
                    anchors.verticalCenter: parent.verticalCenter
                    property string focusId: "recorder.take.wave." + index

                    TimelineWave {
                        anchors.fill: parent
                        values: model.wave
                        placeholderText: ""
                        waveColor: model.active ? Style.accent : Style.textDim
                    }
                    TapHandler {
                        onTapped: app.openTakeEdit(index)
                    }
                }
                Text {
                    anchors.right: editButton.left
                    anchors.rightMargin: 6
                    width: Math.max(90, parent.width * 0.24)
                    height: parent.height
                    text: model.duration + "  -  " + model.bytes
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // W5: EDIT opens the take edit popup for this row.
                Rectangle {
                    id: editButton
                    anchors.right: selectTarget.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 64
                    height: Style.touch - 4
                    radius: Style.corner
                    color: Style.panelDark
                    border.width: 1
                    border.color: Style.outline
                    property string focusId: "recorder.take.edit." + index

                    Text {
                        anchors.centerIn: parent
                        text: "EDIT"
                        color: Style.accent
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                    }
                    TapHandler {
                        onTapped: app.requestEditTake(index)
                    }
                }
                // A3: the row body loads the take; "..." opens the row actions
                // (docs/daw-plan.md W4).
                TapHandler {
                    onTapped: app.activateTakeRow(index)
                }
                Rectangle {
                    id: selectTarget
                    width: Style.touch
                    height: Style.touch
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Style.corner
                    color: index === recorder.selectedRow ? Style.raised : Style.panelDark
                    border.width: 2
                    border.color: index === recorder.selectedRow ? Style.accent : Style.outline

                    Text {
                        anchors.centerIn: parent
                        text: "..."
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        // W4: the row's action menu target ("..." = 44 px).
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        property string focusId: "recorder.take.menu." + index
                    }
                    TapHandler {
                        onTapped: app.openTakeActions(index)
                    }
                }
            }

            Text {
                visible: takeList.count === 0
                anchors.centerIn: parent
                width: parent.width - 20
                text: "no takes yet - tap START TAKE"
                color: Style.textDim
                font.pixelSize: Style.fontBody
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Beat field for the take loop: an arc-dial chip (drag nudges it in
    // 0.25-beat steps, the panel encoder drives the same value) with the two
    // fine-step buttons beside it. No slider: the law keeps those for faders.
    component LoopBeatField: Row {
        property string label: ""
        property real beats: 0.0
        property string valueText: "0.00"
        property string focusId: ""
        signal step(real delta)

        height: Style.touch
        spacing: Style.gapTight

        KnobChip {
            width: parent.width - Style.touch * 2 - Style.gapTight * 2
            height: parent.height
            label: parent.label
            value: parent.beats
            valueText: parent.valueText
            suffix: " b"
            stepSize: 0.25
            dialScale: Math.max(8, Math.ceil(parent.beats))
            popupLabel: "LOOP " + parent.label
            popupKind: "generic"
            focusId: parent.focusId
            onMoved: (value) => parent.step(value - parent.beats)
        }
        TouchButton {
            width: Style.touch
            height: parent.height
            text: "-"
            textSize: 18
            focusId: parent.focusId + ".dec"
            onClicked: parent.step(-0.25)
        }
        TouchButton {
            width: Style.touch
            height: parent.height
            text: "+"
            textSize: 18
            focusId: parent.focusId + ".inc"
            onClicked: parent.step(0.25)
        }
    }

    // SHIFT: management + loop + punch
    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: 108
                height: parent.height
                text: "TAKES + PUNCH"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: 76
                height: parent.height
                text: "IMPORT"
                textSize: Style.fontSmall
                focusId: "shift.recorder.import"
                onClicked: app.requestImportTake()
            }
            TouchButton {
                width: 64
                height: parent.height
                text: "LIVE"
                textSize: Style.fontSmall
                checked: recorder.activeTake === "LIVE INPUT"
                focusId: "shift.recorder.live"
                onClicked: recorder.clearActive()
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "ACTIVE"
                textSize: Style.fontSmall
                opacity: recorder.selectedRow >= 0 ? 1.0 : 0.4
                focusId: "shift.recorder.activate"
                onClicked: {
                    if (recorder.selectedRow < 0) {
                        app.showTextPopup("ACTIVE TAKE", "select a row first (...)")
                        return
                    }
                    recorder.activateSelected()
                }
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                opacity: (!recorder.recording && recorder.selectedRow >= 0) ? 1.0 : 0.4
                focusId: "shift.recorder.rename"
                onClicked: {
                    if (recorder.selectedRow < 0) {
                        app.showTextPopup("RENAME TAKE", "select a row first (...)")
                        return
                    }
                    app.requestRenameTake()
                }
            }
            TouchButton {
                width: 96
                height: parent.height
                text: "MODE " + recorder.trackModeWord
                textSize: 10
                chevron: "down"
                focusId: "shift.recorder.mode"
                onClicked: app.openTrackMode(recorder.selectedTrack)
            }
            TouchButton {
                width: 68
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                variant: "danger"
                opacity: (!recorder.recording && recorder.selectedRow >= 0) ? 1.0 : 0.4
                focusId: "shift.recorder.delete"
                onClicked: {
                    if (recorder.selectedRow < 0) {
                        app.showTextPopup("DELETE TAKE", "select a row first (...)")
                        return
                    }
                    app.requestDeleteTake()
                }
            }
            // A3: the destructive buttons act on this row; name it here.
            Text {
                width: parent.width - 108 - 76 - 64 - 74 - 74 - 68 - Style.gap * 6
                height: parent.height
                text: recorder.selectedRow >= 0
                      ? "SELECTED  " + recorder.takeNameAt(recorder.selectedRow)
                      : "no take selected"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                opacity: 0.8
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }
        }

        // Take loop for the selected track (session v2 takes.loop). The engine
        // loops the beat window; with an end at or below the start it loops the
        // whole active take, which is what the hint line says.
        Column {
            width: parent.width
            height: Style.touch * 2 + Style.gap
            spacing: Style.gap

            Row {
                width: parent.width
                height: Style.touch
                spacing: Style.gap

                Text {
                    width: 96
                    height: parent.height
                    text: "LOOP  " + String(recorder.selectedTrack + 1).padStart(2, "0")
                    color: recorder.loopEnabled ? Style.accent : Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                TouchButton {
                    width: 76
                    height: parent.height
                    text: recorder.loopEnabled ? "ON" : "OFF"
                    textSize: Style.fontSmall
                    checked: recorder.loopEnabled
                    focusId: "shift.loop.enable"
                    onClicked: {
                        recorder.setLoopEnabled(!recorder.loopEnabled)
                        app.showTextPopup("LOOP", recorder.loopText)
                    }
                }
                Text {
                    width: parent.width - 96 - 76 - Style.gap * 2
                    height: parent.height
                    text: recorder.loopText + "   -   beats from the take start; " +
                          "end <= start loops the whole take"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Row {
                width: parent.width
                height: Style.touch
                spacing: Style.gap

                LoopBeatField {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    label: "START"
                    beats: recorder.loopStartBeats
                    valueText: recorder.loopStartText
                    focusId: "shift.loop.start"
                    onStep: (delta) => {
                        recorder.setLoopStartBeats(recorder.loopStartBeats + delta)
                        app.showValuePopup("LOOP START", "generic", recorder.loopStartBeats)
                    }
                }
                LoopBeatField {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    label: "END"
                    beats: recorder.loopEndBeats
                    valueText: recorder.loopEndText
                    focusId: "shift.loop.end"
                    onStep: (delta) => {
                        recorder.setLoopEndBeats(recorder.loopEndBeats + delta)
                        app.showValuePopup("LOOP END", "generic", recorder.loopEndBeats)
                    }
                }
            }
        }

        PunchScreen {
            width: parent.width
            height: parent.height - Style.touch * 3 - Style.gap * 3
            focusPrefix: "shift.punch"
            compact: true
        }
    }


    // TAKE FOR TRACK nn (A10): the sheet that used to live on the EDIT tab. The
    // ACTIVE readout opens it; a row makes that take ACTIVE, EDIT opens the take
    // edit popup for it, and LIVE INPUT clears the pointer. Every row carries its
    // own 44 px mini-waveform.
    Rectangle {
        id: takeSheet

        visible: app.takeSheetOpen
        anchors.fill: parent
        color: "#cc000000"
        z: 60

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Rectangle {
            width: Math.min(620, parent.width - 24)
            height: Math.min(parent.height - 8, 24 + 16 + Style.touch + 8 +
                            Math.min(5, recorder.takes.count) * Style.touch +
                            Style.gap * 4)
            anchors.centerIn: parent
            color: Style.raised
            radius: Style.cornerPanel
            border.width: 2
            border.color: Style.accent

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: Style.gap

                Row {
                    width: parent.width
                    height: 24

                    Text {
                        width: parent.width - 92
                        height: parent.height
                        text: "TAKE FOR TRACK " +
                              String(app.takeSheetTrack + 1).padStart(2, "0")
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
                        focusId: "take.sheet.close"
                        onClicked: app.closeTakeSheet()
                    }
                }

                Text {
                    width: parent.width
                    height: 16
                    text: "tap a take to make it ACTIVE - EDIT opens the waveform"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: parent.width
                    height: Style.touch
                    radius: Style.corner
                    property string focusId: "take.sheet.live"
                    color: Style.panelDark
                    border.width: app.focusId === focusId ? 2 : Style.outlineWidth
                    border.color: app.focusId === focusId ? Style.accent : Style.outline

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        width: parent.width - 16
                        height: parent.height
                        text: "LIVE INPUT"
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TapHandler {
                        onTapped: app.chooseLiveInput()
                    }
                }

                ListView {
                    width: parent.width
                    height: Math.min(5, recorder.takes.count) * Style.touch
                    clip: true
                    model: recorder.takes
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: Style.touch
                        radius: Style.corner
                        color: model.active ? Style.raised : Style.panelDark
                        border.width: model.active ? 2 : Style.outlineWidth
                        border.color: model.active ? Style.accent : Style.outline

                        // The pick target stops before EDIT, so a tap is never
                        // both a pick and an edit.
                        Item {
                            x: 0
                            y: 0
                            width: parent.width - 68
                            height: parent.height
                            property string focusId: "take.sheet.row." + index

                            Text {
                                x: 10
                                width: 64
                                height: parent.height
                                text: model.active ? "ACTIVE" : ""
                                color: Style.accent
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                x: 78
                                width: parent.width - 78 - 104
                                height: parent.height
                                text: model.name
                                color: Style.text
                                font.pixelSize: Style.fontBody
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }
                            Item {
                                x: parent.width - 156
                                width: 100
                                height: Style.touch - 8
                                anchors.verticalCenter: parent.verticalCenter

                                TimelineWave {
                                    anchors.fill: parent
                                    values: recorder.takeWave(index)
                                    placeholderText: ""
                                    waveColor: model.active ? Style.accent : Style.textDim
                                }
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                width: 48
                                height: parent.height
                                text: model.duration
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TapHandler {
                                onTapped: app.chooseTakeRow(index)
                            }
                        }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 60
                            height: Style.touch - 4
                            radius: Style.corner
                            color: Style.panelDark
                            border.width: 1
                            border.color: Style.outline
                            property string focusId: "take.sheet.edit." + index

                            Text {
                                anchors.centerIn: parent
                                text: "EDIT"
                                color: Style.accent
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                            }
                            TapHandler {
                                onTapped: app.requestEditTake(index)
                            }
                        }
                    }
                }
            }
        }
    }
}
