import QtQuick
import Mnkybox 1.0
import "../components"

// Tools: PUNCH markers, MONITOR / metronome / bus-comp configuration and the
// TOOLS status page. One tab, three pages, all settings written to the session
// v2 app-owned sections.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: 150
                height: parent.height
                text: "PUNCH"
                textSize: Style.fontSmall
                checked: app.toolsPage === 0
                focusId: "tools.page.0"
                onClicked: app.setToolsPage(0)
            }
            TouchButton {
                width: 150
                height: parent.height
                text: "MONITOR"
                textSize: Style.fontSmall
                checked: app.toolsPage === 1
                focusId: "tools.page.1"
                onClicked: app.setToolsPage(1)
            }
            TouchButton {
                width: 150
                height: parent.height
                text: "TOOLS STATUS"
                textSize: Style.fontSmall
                checked: app.toolsPage === 2
                focusId: "tools.page.2"
                onClicked: app.setToolsPage(2)
            }
            Text {
                width: parent.width - 478
                height: parent.height
                text: app.toolsPage === 0 ? "transport markers"
                       : (app.toolsPage === 1 ? "session v2 monitor config" : "disk / screen / versions")
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }
        }

        Loader {
            width: parent.width
            height: parent.height - Style.touch - Style.gap
            source: {
                switch (app.toolsPage) {
                case 0: return "PunchScreen.qml"
                case 1: return "MonitorScreen.qml"
                case 2: return "ToolsStatusScreen.qml"
                default: return ""
                }
            }
        }
    }
}
