import QtQuick
import Mnkybox 1.0
import "../components"

// SYSTEM SHIFT face: metronome/monitor configuration, MIDI learn and TOOLS.
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

            TouchButton {
                width: 132
                height: parent.height
                text: "MONITOR"
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
                width: 92
                height: parent.height
                text: "PANIC"
                textSize: Style.fontSmall
                    variant: "danger"
                focusId: "shift.system.panic"
                onClicked: app.shiftAction(1)
            }
            Text {
                width: parent.width - 520
                height: parent.height
                text: "SHIFT  configure"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
