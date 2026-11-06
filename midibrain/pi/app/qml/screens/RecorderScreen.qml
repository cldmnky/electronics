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

            Text {
                width: parent.width * 0.55
                height: parent.height
                text: recorder.transportText
                color: recorder.recording ? Style.record
                       : (app.rolling ? Style.accent : Style.text)
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

        Item {
            width: parent.width
            // One row of nine per line; the row count and the arm state come
            // from the recorder's own arm model, not a literal.
            height: Math.ceil(recorder.arms.rowCount / 9) * Style.touch

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
                checked: mixer.trackArm
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
                text: "SHIFT: takes + punch"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            width: parent.width
            height: 16
            text: recorder.statusText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
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
                                16 - 14 - Style.gap * 5)
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
                    width: parent.width * 0.52
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
                    anchors.rightMargin: 8
                    width: parent.width * 0.38
                    height: parent.height
                    text: model.duration + "  -  " + model.bytes
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TapHandler {
                    onTapped: recorder.selectRow(index)
                }
            }

            Text {
                visible: takeList.count === 0
                anchors.centerIn: parent
                width: parent.width - 20
                text: "no takes yet - press START TAKE"
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
                active: recorder.selectedRow >= 0
                focusId: "shift.recorder.activate"
                onClicked: recorder.activateSelected()
            }
            TouchButton {
                width: 74
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                active: !recorder.recording && recorder.selectedRow >= 0
                focusId: "shift.recorder.rename"
                onClicked: app.requestRenameTake()
            }
            TouchButton {
                width: 68
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                active: !recorder.recording && recorder.selectedRow >= 0
                variant: "danger"
                focusId: "shift.recorder.delete"
                onClicked: app.requestDeleteTake()
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
