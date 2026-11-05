import QtQuick
import QtQuick.Window
import Mnkybox 1.0
import "components"
import "overlays"

// mnkybox shell: tab row, persistent transport strip, one of ten screens, and
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

    readonly property var tabNames: ["Mixer", "Routing", "FX", "Recorder", "System",
                                     "Projects", "Monitor", "EQ", "XY", "EDIT"]

    Column {
        anchors.fill: parent

        Rectangle {
            id: tabBar
            width: parent.width
            height: Style.tabHeight
            color: Style.panelDark
            // EQ fullscreen hides the whole bridge (tab row + transport strip);
            // AppController::setTab restores it when another tab is picked.
            visible: !app.fullscreen

            readonly property bool shiftMode: app.panelMode === "shift"
            readonly property int tabCount: 10

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
                            // Short tap: navigate to the tab (leaves a shift face).
                            // Long-press: open the tab's SHIFT face — the touch twin of
                            // the panel's SHIFT long-hold. The flag stops the tap that
                            // ends the hold from navigating back out of the face.
                            id: tabTap
                            property bool heldShift: false
                            onPressedChanged: if (pressed) heldShift = false
                            onTapped: {
                                if (heldShift) return
                                app.setTab(index)
                                if (app.panelMode === "shift") app.showShiftFace(false)
                            }
                            onLongPressed: {
                                heldShift = true
                                app.setTab(index)
                                app.showShiftFace(true)
                                app.showTextPopup("SHIFT", window.tabNames[index] + " shift face")
                            }
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
            id: transport
            width: parent.width
            height: Style.transportHeight
            visible: !app.fullscreen
        }

        Loader {
            id: screenLoader
            width: parent.width
            height: parent.height - (tabBar.visible ? tabBar.height : 0)
                    - (transport.visible ? Style.transportHeight : 0)
            source: {
                switch (app.tab) {
                case 0: return "screens/MixerScreen.qml"
                case 1: return "screens/RoutingScreen.qml"
                case 2: return "screens/FxScreen.qml"
                case 3: return "screens/RecorderScreen.qml"
                case 4: return "screens/SystemScreen.qml"
                case 5: return "screens/ProjectsScreen.qml"
                case 6: return "screens/MidiMonitorScreen.qml"
                case 7: return "screens/EqScreen.qml"
                case 8: return "screens/XyScreen.qml"
                case 9: return "screens/TimelineScreen.qml"
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
