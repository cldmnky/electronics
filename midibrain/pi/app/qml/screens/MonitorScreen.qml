import QtQuick
import Mnkybox 1.0
import "../components"

// SYSTEM SHIFT / OUTPUT page: the clock-source choice plus one summary button
// per output config group. The full controls, including the help text, live in
// the ConfigPopup modal.
Item {
    id: page

    property string focusPrefix: "monitor"

    Column {
        width: parent.width
        spacing: Style.gapTight

        Text {
            width: parent.width
            height: 16
            text: "OUTPUT CONFIG"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        // clock source. The three choices persist in the session
        // (clock.source + transportFollow); the note states what this build
        // can send, so the row never claims a follow that is not wired.
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            Text {
                width: 104
                height: parent.height
                text: "CLOCK SOURCE"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Repeater {
                model: ["INTERNAL", "FOLLOW TRS", "FOLLOW USB"]
                delegate: TouchButton {
                    width: (parent.width - 104 - Style.gapTight * 3) / 3
                    height: parent.height
                    text: modelData
                    textSize: Style.fontSmall
                    checked: session.clockSourceChoice === index
                    focusId: "shift.monitor.clocksource." + index
                    onClicked: session.clockSourceChoice = index
                }
            }
        }
        Text {
            width: parent.width
            height: 24
            text: "now: " + session.clockSourceText + "  -  " + session.clockSourceNote
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "CLICK   " + config.metronomeSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.metronome"
            onClicked: app.openConfigFrom("metronome", this)
        }
        TouchButton {
            width: parent.width
            height: Style.touch
            text: "SEND SOURCE   " + config.sendSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.sends"
            onClicked: app.openConfigFrom("sends", this)
        }
        TouchButton {
            width: parent.width
            height: Style.touch
            text: "OUTPUT   " + config.monitorSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.monitor"
            onClicked: app.openConfigFrom("monitor", this)
        }
        TouchButton {
            width: parent.width
            height: Style.touch
            text: "COMP   " + config.compSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.comp"
            onClicked: app.openConfigFrom("comp", this)
        }
    }
}
