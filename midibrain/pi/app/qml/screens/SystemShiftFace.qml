import QtQuick
import Mnkybox 1.0
import "../components"

// SYSTEM SHIFT face: OUTPUT (metronome/cue/comp/clock), MIDI learn and TOOLS.
// The primary face stays status + backend selection.
Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            // this page is output/phones/cue/comp - OUTPUT, not MONITOR.
            // MON stays the per-track input-monitor mark on the mixer.
            TouchButton {
                width: 132
                height: parent.height
                text: "OUTPUT"
                textSize: Style.fontSmall
                checked: app.systemShiftPage === 0
                focusId: "shift.system.page.0"
                onClicked: app.setSystemShiftPage(0)
            }
            TouchButton {
                width: 132
                height: parent.height
                text: "MIDI LEARN"
                textSize: Style.fontSmall
                checked: app.systemShiftPage === 1
                focusId: "shift.system.page.1"
                onClicked: app.setSystemShiftPage(1)
            }
            TouchButton {
                width: 132
                height: parent.height
                text: "TOOLS"
                textSize: Style.fontSmall
                checked: app.systemShiftPage === 2
                focusId: "shift.system.page.2"
                onClicked: app.setSystemShiftPage(2)
            }
            TouchButton {
                width: 132
                height: parent.height
                text: "DIAGNOSTICS"
                textSize: Style.fontSmall
                checked: app.systemShiftPage === 3
                focusId: "shift.system.page.3"
                onClicked: app.setSystemShiftPage(3)
            }
            TouchButton {
                width: 92
                height: parent.height
                text: "PANIC"
                textSize: Style.fontSmall
                    variant: "danger"
                focusId: "shift.system.panic"
                onClicked: app.shiftAction(1)
            }
        }

        Loader {
            width: parent.width
            height: parent.height - Style.touch - Style.gap
            source: {
                switch (app.systemShiftPage) {
                case 0: return "MonitorScreen.qml"
                case 1: return "../components/LearnPanel.qml"
                case 2: return "ToolsStatusScreen.qml"
                case 3: return "ToolsScreen.qml"
                default: return ""
                }
            }
            onLoaded: {
                if (app.systemShiftPage === 0) item.focusPrefix = "shift.monitor"
                else if (app.systemShiftPage === 1) item.focusPrefix = "shift.learn"
                else if (app.systemShiftPage === 2) item.focusPrefix = "shift.tools"
            }
        }
    }
}
