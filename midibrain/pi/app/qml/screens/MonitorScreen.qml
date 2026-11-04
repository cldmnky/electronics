import QtQuick
import Mnkybox 1.0
import "../components"

// SYSTEM SHIFT / MONITOR page: one summary button per config group. The full
// controls, including the help text, live in the ConfigPopup modal.
Item {
    id: page

    property string focusPrefix: "monitor"

    Column {
        width: parent.width
        spacing: 6

        Text {
            width: parent.width
            height: 18
            text: "MONITOR CONFIG  -  tap a summary to open it; OK applies, EXIT/CANCEL discards"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.bold: true
            elide: Text.ElideRight
        }

        TouchButton {
            width: parent.width
            height: 64
            text: "METRONOME   " + config.metronomeSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.metronome"
            onClicked: app.openConfig("metronome")
        }
        TouchButton {
            width: parent.width
            height: 64
            text: "SEND SOURCE   " + config.sendSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.sends"
            onClicked: app.openConfig("sends")
        }
        TouchButton {
            width: parent.width
            height: 64
            text: "MONITOR   " + config.monitorSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.monitor"
            onClicked: app.openConfig("monitor")
        }
        TouchButton {
            width: parent.width
            height: 64
            text: "MAIN COMP   " + config.compSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.comp"
            onClicked: app.openConfig("comp")
        }

        Text {
            width: parent.width
            text: "Configuration edits are drafts until OK. A touch outside the popup does not dismiss it."
            color: Style.textDim
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }
}
