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
        spacing: Style.gapTight

        // (a) The recording state and the running take clock are the first
        // thing read, at value size. The ACTIVE take is the one large card that
        // opens the take sheet.
        Row {
            width: parent.width
            height: Style.touchSecondary
            spacing: Style.gap

            Column {
                width: parent.width * 0.62 - Style.gap
                height: parent.height
                spacing: 0

                Text {
                    width: parent.width
                    height: Style.fontValue + 8
                    text: recorder.transportText
                    color: recorder.recording ? Style.record
                           : (app.rolling ? Style.accent : Style.textDim)
                    font.pixelSize: Style.fontValue
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    height: parent.height - Style.fontValue - 8
                    text: recorder.recording ? recorder.statusText : recorder.transportHint
                    color: recorder.recording ? Style.record : Style.textDim
                    font.pixelSize: Style.fontSecondary
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                width: parent.width - parent.width * 0.62 - Style.gap
                height: parent.height
                radius: Style.corner
                color: Style.panel
                border.width: app.focusId === "recorder.takesheet" ? 2 : Style.outlineWidth
                border.color: app.focusId === "recorder.takesheet" ? Style.accent : Style.outline
                property string focusId: "recorder.takesheet"

                Text {
                    x: 10
                    width: parent.width - 20
                    height: 16
                    text: "ACTIVE TAKE  -  TAP FOR TAKES"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    x: 10
                    y: 16
                    width: parent.width - 20
                    height: parent.height - 16
                    text: recorder.activeTake
                    color: recorder.activeTake === "LIVE INPUT" ? Style.textDim : Style.accent
                    font.pixelSize: Style.fontLabel
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                }
                TapHandler {
                    onTapped: app.openTakeSheet(recorder.selectedTrack)
                }
            }
        }

        // (b) ONE selected-track button (opens the picker) plus prev/next,
        // instead of the permanent 18-selector grid. (d) The recording source
        // is its own readable summary card.
        Row {
            width: parent.width
            height: Style.touchPrimary
            spacing: Style.gap

            TouchButton {
                width: parent.width - 64 * 2 - 168 - Style.gap * 3
                height: parent.height
                text: "TRACK " + String(recorder.selectedTrack + 1).padStart(2, "0") +
                      "   " + recorder.trackName
                textSize: Style.fontLabel
                chevron: "down"
                focusId: "recorder.track.pick"
                onClicked: app.openTrackPicker()
            }
            TouchButton {
                width: 64
                height: parent.height
                text: "<"
                textSize: Style.fontLabel
                focusId: "recorder.track.prev"
                onClicked: recorder.setSelectedTrack((recorder.selectedTrack + 17) % 18)
            }
            TouchButton {
                width: 64
                height: parent.height
                text: ">"
                textSize: Style.fontLabel
                focusId: "recorder.track.next"
                onClicked: recorder.setSelectedTrack((recorder.selectedTrack + 1) % 18)
            }
            TouchButton {
                width: 168
                height: parent.height
                text: "SRC  " + recorder.trackRecSourceText
                textSize: Style.fontLabel
                chevron: "down"
                focusId: "recorder.recsource"
                onClicked: app.openTrackMode(recorder.selectedTrack)
            }
        }

        // (c) Large ARM and START/STOP TAKE actions, with the armed/recording
        // state spelled out in words and an icon as well as colour (g).
        Row {
            width: parent.width
            height: Style.touchPrimary
            spacing: Style.gap

            TouchButton {
                width: 200
                height: parent.height
                text: recorder.trackArm ? "ARM  ON" : "ARM  OFF"
                textSize: Style.fontLabel
                checked: recorder.trackArm
                checkedColor: Style.record
                focusId: "recorder.mode"
                onClicked: app.cycleTrackMode(recorder.selectedTrack)
                onLongClicked: app.openTrackMode(recorder.selectedTrack)
            }
            TouchButton {
                width: 300
                height: parent.height
                text: recorder.buttonText
                textSize: Style.fontLabel
                variant: recorder.recording ? "danger" : "primary"
                focusId: "recorder.start"
                onClicked: recorder.startStop()
            }
            Row {
                width: parent.width - 200 - 300 - Style.gap * 2
                height: parent.height
                spacing: Style.gap

                Rectangle {
                    width: 14
                    height: 14
                    radius: 7
                    anchors.verticalCenter: parent.verticalCenter
                    color: recorder.recording ? Style.record : "transparent"
                    border.width: 2
                    border.color: recorder.recording || recorder.trackArm
                                  ? Style.record : Style.textDim
                }
                Text {
                    width: parent.width - 14 - Style.gap
                    height: parent.height
                    text: recorder.recording ? "RECORDING"
                          : (recorder.trackArm ? "ARMED - START TAKE READY" : "NOT ARMED")
                    color: recorder.recording || recorder.trackArm ? Style.record : Style.textDim
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Disk/take-count line, or the state warning when one is active. The
        // take list below owns the rest of the page and scrolls the focused
        // row into view (audit G14).
        Text {
            width: parent.width
            height: 18
            text: recorder.statusWarning ? recorder.statusText : recorder.diskText
            color: recorder.statusWarning ? Style.record : Style.textDim
            font.pixelSize: recorder.statusWarning ? Style.fontSecondary : Style.fontSmall
            font.weight: recorder.statusWarning ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        ListView {
            id: takeList
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 -
                                Style.touchSecondary - Style.touchPrimary * 2 - 18 -
                                Style.gapTight * 4)
            clip: true
            model: recorder.takes
            boundsBehavior: Flickable.StopAtBounds

            function syncFocus() {
                const id = app.focusId
                if (!id.startsWith("recorder.take.")) return
                const parts = id.split(".")
                if (parts.length < 3) return
                const row = parseInt(parts[2])
                if (!isNaN(row)) positionViewAtIndex(row, ListView.Contain)
            }
            Connections {
                target: app
                function onFocusChanged() { takeList.syncFocus() }
            }
            Component.onCompleted: syncFocus()

            delegate: Rectangle {
                id: takeRow
                width: ListView.view.width
                height: 64 + (takeRow.detailsOpen ? 40 : 0)
                radius: Style.corner
                color: index === recorder.selectedRow ? Style.raised
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === ("recorder.take." + index) ? 2 : 0
                border.color: Style.accent
                property bool detailsOpen: false

                // The row body is the ACTIVE tap target and the panel ring stop.
                Item {
                    id: bodyLeft
                    x: 0
                    y: 0
                    width: takeRow.width - 70 - 64 - 64 - 64
                    height: 64
                    property string focusId: "recorder.take." + index

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
                        width: 60
                        height: 16
                        text: model.active ? "✓ ACTIVE" : ""
                        color: Style.accent
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        x: 10
                        y: 16
                        width: parent.width - 14
                        height: 40
                        text: (model.imported ? "PTR  " : (model.master ? "MASTER  " : "")) + model.name
                        color: Style.text
                        font.pixelSize: Style.fontLabel
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }
                    TapHandler {
                        onTapped: app.activateTakeRow(index)
                    }
                }
                // EDIT is the first action of the row (f).
                Rectangle {
                    id: editButton
                    x: bodyLeft.x + bodyLeft.width + 4
                    anchors.verticalCenter: parent.top
                    anchors.verticalCenterOffset: 32
                    width: 64
                    height: 64
                    radius: Style.corner
                    color: Style.panelDark
                    border.width: app.focusId === ("recorder.take.edit." + index) ? 2 : 1
                    border.color: app.focusId === ("recorder.take.edit." + index)
                                  ? Style.accent : Style.outline
                    property string focusId: "recorder.take.edit." + index

                    Text {
                        anchors.centerIn: parent
                        text: "EDIT"
                        color: Style.accent
                        font.pixelSize: Style.fontSecondary
                        font.weight: Font.DemiBold
                    }
                    TapHandler {
                        onTapped: app.requestEditTake(index)
                    }
                }
                Text {
                    x: editButton.x + editButton.width + 8
                    width: 48
                    height: 64
                    text: model.duration
                    color: Style.textDim
                    font.pixelSize: Style.fontSecondary
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // Details disclosure: full path, file size and file count only
                // on request; the row itself stays an identity/duration/EDIT row.
                Rectangle {
                    id: detailsButton
                    x: takeRow.width - 64 - 64 - 8
                    y: 0
                    width: 64
                    height: 64
                    radius: Style.corner
                    color: takeRow.detailsOpen ? Style.raised : Style.panelDark
                    border.width: 1
                    border.color: Style.outline

                    Text {
                        anchors.centerIn: parent
                        text: takeRow.detailsOpen ? "LESS" : "INFO"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                    }
                    TapHandler {
                        onTapped: takeRow.detailsOpen = !takeRow.detailsOpen
                    }
                }
                Rectangle {
                    id: selectTarget
                    x: takeRow.width - 64 - 2
                    y: 0
                    width: 64
                    height: 64
                    radius: Style.corner
                    color: index === recorder.selectedRow ? Style.raised : Style.panelDark
                    border.width: app.focusId === ("recorder.take.menu." + index) ? 2 : 1
                    border.color: app.focusId === ("recorder.take.menu." + index)
                                  ? Style.accent : Style.outline

                    Text {
                        anchors.centerIn: parent
                        text: "..."
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        property string focusId: "recorder.take.menu." + index
                    }
                    TapHandler {
                        onTapped: app.openTakeActions(index)
                    }
                }
                // Mini-waveform inset at the bottom of the identity block:
                // tapping it opens the take edit popup (kept for parity with
                // the stage-2 row and the ui-tap gate).
                Item {
                    id: miniWave
                    x: bodyLeft.x + 10
                    y: 40
                    width: Math.max(0, bodyLeft.width - 20)
                    height: 20
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
                // Disclosed metadata: full storage path, size and file count.
                Rectangle {
                    visible: takeRow.detailsOpen
                    x: 8
                    y: 64
                    width: takeRow.width - 16
                    height: 36
                    color: Style.panelDark
                    radius: Style.corner

                    Text {
                        x: 8
                        width: parent.width - 16
                        height: 18
                        text: "FILE  " + model.path
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }
                    Text {
                        x: 8
                        y: 18
                        width: parent.width - 16
                        height: 16
                        text: model.bytes + "  -  " + model.files + " file(s)"
                        color: Style.textDim
                        font.pixelSize: Style.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                visible: takeList.count === 0
                anchors.centerIn: parent
                width: parent.width - 20
                text: "no takes yet - ARM a track and tap START TAKE"
                color: Style.textDim
                font.pixelSize: Style.fontLabel
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


    // TAKE FOR TRACK nn (A10): the stage-4 sheet. Title and padding match the
    // shared overlay header, rows are 64 px with identity/duration/EDIT first,
    // the details disclosure holds the full path/size, and CLOSE is a fixed
    // 64 px footer while the list scrolls (item #8 vocabulary).
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
            width: Math.min(680, parent.width - 24)
            height: Math.min(parent.height - 8, 352)
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
                        width: parent.width
                        height: parent.height
                        text: "TAKE FOR TRACK " +
                              String(app.takeSheetTrack + 1).padStart(2, "0")
                        color: Style.text
                        font.pixelSize: Style.fontTitle
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Style.touchSecondary
                    radius: Style.corner
                    property string focusId: "take.sheet.live"
                    color: Style.panelDark
                    border.width: app.focusId === focusId ? 2 : Style.outlineWidth
                    border.color: app.focusId === focusId ? Style.accent : Style.outline

                    Text {
                        x: 10
                        width: parent.width - 20
                        height: parent.height
                        text: "LIVE INPUT"
                        color: Style.textDim
                        font.pixelSize: Style.fontLabel
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    TapHandler {
                        onTapped: app.chooseLiveInput()
                    }
                }

                ListView {
                    width: parent.width
                    height: parent.height - 24 - Style.touchSecondary - 64 - Style.gap * 3
                    clip: true
                    model: recorder.takes
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: takeSheetRow
                        width: ListView.view.width
                        height: 64 + (takeSheetRow.detailsOpen ? 26 : 0)
                        radius: Style.corner
                        color: model.active ? Style.raised : Style.panelDark
                        border.width: model.active ? 2 : Style.outlineWidth
                        border.color: model.active ? Style.accent : Style.outline
                        property bool detailsOpen: false

                        // Identity + active marker + duration; the pick target
                        // stops before EDIT so a tap is never both.
                        Item {
                            x: 0
                            y: 0
                            width: parent.width - 64 - 64
                            height: parent.height
                            property string focusId: "take.sheet.row." + index

                            Text {
                                x: 8
                                width: 64
                                height: parent.height
                                text: model.active ? "\u2713 ACTIVE" : ""
                                color: Style.accent
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                x: 76
                                width: parent.width - 76 - 74
                                height: parent.height
                                text: model.name
                                color: Style.text
                                font.pixelSize: Style.fontLabel
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                width: 70
                                height: parent.height
                                text: model.duration
                                color: Style.textDim
                                font.pixelSize: Style.fontSecondary
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            TapHandler {
                                onTapped: app.chooseTakeRow(index)
                            }
                        }
                        Rectangle {
                            x: parent.width - 128
                            y: 2
                            width: 64
                            height: 60
                            radius: Style.corner
                            color: Style.panelDark
                            border.width: app.focusId === ("take.sheet.edit." + index) ? 2 : 1
                            border.color: app.focusId === ("take.sheet.edit." + index)
                                          ? Style.accent : Style.outline
                            property string focusId: "take.sheet.edit." + index

                            Text {
                                anchors.centerIn: parent
                                text: "EDIT"
                                color: Style.accent
                                font.pixelSize: Style.fontSecondary
                                font.weight: Font.DemiBold
                            }
                            TapHandler {
                                onTapped: app.requestEditTake(index)
                            }
                        }
                        // Details disclosure: full file path and size, not on
                        // the row itself.
                        Rectangle {
                            x: parent.width - 64
                            y: 2
                            width: 60
                            height: 60
                            radius: Style.corner
                            color: takeSheetRow.detailsOpen ? Style.raised : Style.panelDark
                            border.width: 1
                            border.color: Style.outline

                            Text {
                                anchors.centerIn: parent
                                text: takeSheetRow.detailsOpen ? "LESS" : "INFO"
                                color: Style.textDim
                                font.pixelSize: Style.fontSmall
                                font.weight: Font.DemiBold
                            }
                            TapHandler {
                                onTapped: takeSheetRow.detailsOpen = !takeSheetRow.detailsOpen
                            }
                        }
                        Text {
                            visible: takeSheetRow.detailsOpen
                            x: 8
                            y: 64
                            width: takeSheetRow.width - 16
                            height: 24
                            text: model.path + "  -  " + model.bytes
                            color: Style.textDim
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // Fixed footer: the one large predictable close control.
                Row {
                    width: parent.width
                    height: 64
                    spacing: Style.gap

                    Text {
                        width: parent.width - 160 - Style.gap
                        height: parent.height
                        text: recorder.activeTake === "LIVE INPUT"
                              ? "no take is ACTIVE - this track plays the live input"
                              : "ACTIVE  " + recorder.activeTake
                        color: Style.textDim
                        font.pixelSize: Style.fontSecondary
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }
                    TouchButton {
                        width: 160
                        height: parent.height
                        text: "CLOSE"
                        textSize: Style.fontLabel
                        variant: "primary"
                        focusId: "take.sheet.close"
                        onClicked: app.closeTakeSheet()
                    }
                }
            }
        }
    }

    // TRACK PICKER (stage 4): the selected-track button's sheet. 18 tracks in
    // a 6x3 grid of 64 px rows, the selected one checked; CLOSE is a 64 px
    // footer and outside touches never dismiss (the shared overlay contract).
    Rectangle {
        id: trackPicker

        visible: app.trackPickerOpen
        anchors.fill: parent
        color: "#cc000000"
        z: 55

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Rectangle {
            width: Math.min(680, parent.width - 24)
            height: Math.min(parent.height - 8, 352)
            anchors.centerIn: parent
            color: Style.raised
            radius: Style.cornerPanel
            border.width: 2
            border.color: Style.accent

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: Style.gap

                Text {
                    width: parent.width
                    height: 24
                    text: "SELECT TRACK"
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }

                Grid {
                    width: parent.width
                    height: parent.height - 24 - 64 - Style.gap * 2
                    columns: 6
                    rowSpacing: Style.gapTight
                    columnSpacing: Style.gapTight

                    Repeater {
                        model: recorder.arms
                        delegate: TouchButton {
                            width: (trackPicker.width - 24 - 5 * Style.gapTight) / 6
                            height: 64
                            text: (index === recorder.selectedTrack ? "\u2713 " : "") +
                                  String(index + 1).padStart(2, "0")
                            textSize: Style.fontLabel
                            checked: index === recorder.selectedTrack
                            focusId: "recorder.track.pick." + index
                            onClicked: app.chooseTrack(index)

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 4
                                height: 12
                                text: model.name
                                color: index === recorder.selectedTrack ? Style.panelDark
                                                                        : Style.textDim
                                font.pixelSize: Style.fontSmall
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                TouchButton {
                    width: parent.width
                    height: 64
                    text: "CLOSE"
                    textSize: Style.fontLabel
                    variant: "primary"
                    focusId: "recorder.track.picker.close"
                    onClicked: app.closeTrackPicker()
                }
            }
        }
    }
}
