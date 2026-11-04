import QtQuick
import QtQuick.Window
import Mnkybox 1.0
import "components"
import "overlays"

// mnkybox shell: tab row, persistent transport strip, one of six screens, and
// the keypad/confirmation overlays. 800 x 480 landscape-first, resizable.
Window {
    id: window

    width: 800
    height: 480
    minimumWidth: 800
    minimumHeight: 480
    visible: true
    title: "mnkybox"
    color: Style.background

    readonly property var tabNames: ["Mixer", "Routing", "FX", "Recorder", "System", "Projects"]

    Column {
        anchors.fill: parent

        Rectangle {
            id: tabBar
            width: parent.width
            height: Style.tabHeight
            color: Style.panelDark

            readonly property bool shiftMode: app.panelMode === "shift"
            readonly property int tabCount: 6

            Row {
                anchors.fill: parent

                Repeater {
                    model: tabBar.tabCount
                    delegate: Rectangle {
                        width: tabBar.width / tabBar.tabCount
                        height: tabBar.height
                        color: app.tab === index ? Style.accent : Style.panelDark
                        border.width: tabBar.shiftMode
                                      ? (app.tab === index ? 2 : 1) : 0
                        border.color: tabBar.shiftMode ? Style.accent : Style.text

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            text: window.tabNames[index]
                            color: app.tab === index ? Style.panelDark : Style.text
                            font.pixelSize: 14
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            visible: tabBar.shiftMode
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 3
                            color: Style.accent
                        }

                        Rectangle {
                            visible: index === 5 && app.dirty
                            x: parent.width - 14
                            y: 8
                            width: 7
                            height: 7
                            radius: 4
                            color: Style.accent
                        }

                        TapHandler {
                            onTapped: app.setTab(index)
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Style.outline
            }
        }

        TransportStrip {
            width: parent.width
            height: Style.transportHeight
        }

        Loader {
            id: screenLoader
            width: parent.width
            height: parent.height - tabBar.height - Style.transportHeight
            source: {
                switch (app.tab) {
                case 0: return "screens/MixerScreen.qml"
                case 1: return "screens/RoutingScreen.qml"
                case 2: return "screens/FxScreen.qml"
                case 3: return "screens/RecorderScreen.qml"
                case 4: return "screens/SystemScreen.qml"
                case 5: return "screens/ProjectsScreen.qml"
                default: return ""
                }
            }
        }
    }

    ValuePopup {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.transportHeight + 10
        popupVisible: app.valuePopupVisible
        popupLabel: app.valuePopupLabel
        popupText: app.valuePopupText
    }

    ConfigPopup {
        anchors.fill: parent
    }

    Keypad {
        anchors.fill: parent
    }

    ConfirmDialog {
        anchors.fill: parent
    }
}
