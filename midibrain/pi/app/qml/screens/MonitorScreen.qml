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
        spacing: Style.gap

        Text {
            width: parent.width
            height: 18
            text: "MONITOR CONFIG  -  tap a summary to open it; OK applies, EXIT/CANCEL discards"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        TouchButton {
            width: parent.width
            height: 52
            text: "CLICK   " + config.metronomeSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.metronome"
            onClicked: app.openConfigFrom("metronome", this)
        }
        TouchButton {
            width: parent.width
            height: 52
            text: "SEND SOURCE   " + config.sendSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.sends"
            onClicked: app.openConfigFrom("sends", this)
        }
        TouchButton {
            width: parent.width
            height: 52
            text: "MONITOR   " + config.monitorSummary
            textSize: Style.fontBody
            focusId: "shift.monitor.summary.monitor"
            onClicked: app.openConfigFrom("monitor", this)
        }
        TouchButton {
            width: parent.width
            height: 52
            text: "COMP   " + config.compSummary
            textSize: Style.fontSmall
            focusId: "shift.monitor.summary.comp"
            onClicked: app.openConfigFrom("comp", this)
        }

        Text {
            width: parent.width
            text: "Configuration edits are drafts until OK. A touch outside the popup does not dismiss it."
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
