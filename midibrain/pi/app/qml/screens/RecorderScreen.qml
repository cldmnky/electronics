import QtQuick
import Mnkybox 1.0
import "../components"

// RECORDER primary: transport, track selection, arm/start and the take list.
// SHIFT: take management (import/live/active/rename/delete), the take loop and
// punch markers. The primary face keeps the take list and transport; every
// management action lives on SHIFT.
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
                width: parent.width - parent.width * 0.55 - Style.gap
                height: parent.height
                text: "ACTIVE  " + recorder.activeTake
                color: recorder.activeTake === "LIVE INPUT" ? Style.textDim : Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
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
            TouchButton {
                width: 70
                height: parent.height
                text: "ARM"
                textSize: Style.fontSmall
                checked: recorder.trackArm
                checkedColor: Style.record
                focusId: "recorder.arm"
                onClicked: mixer.toggleArm(recorder.selectedTrack)
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
            Text {
                width: parent.width - 120 - 70 - 140 - Style.gap * 3
                height: parent.height
                text: "SHIFT > takes + punch"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
                color: index === recorder.selectedRow ? Style.raised
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === ("recorder.take." + index) ? 2 : 0
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
                    width: parent.width * 0.44
                    height: parent.height
                    text: (model.imported ? "PTR  " : "") + model.name
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 52
                    width: parent.width * 0.30
                    height: parent.height
                    visible: !(model.active && recorder.trackArmMutesTake)
                    text: model.duration + "  -  " + model.bytes
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // A1: the take is ACTIVE but the armed strip mutes it. The chip
                // disarms and says what plays next.
                Rectangle {
                    visible: model.active && recorder.trackArmMutesTake
                    anchors.right: selectTarget.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 150
                    height: Style.touch
                    radius: Style.corner
                    color: Style.panelDark
                    border.width: 1
                    border.color: Style.record

                    Text {
                        anchors.fill: parent
                        text: "ARMED - MUTES TAKE"
                        color: Style.record
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    TapHandler {
                        onTapped: {
                            mixer.toggleArm(recorder.selectedTrack)
                            app.showTextPopup("ARM OFF", "playing " + model.name)
                        }
                    }
                }
                // A3: the row body loads the take; "..." only selects it.
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
                    TapHandler {
                        onTapped: app.selectTakeRow(index)
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
}
