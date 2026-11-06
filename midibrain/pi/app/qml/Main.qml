import QtQuick
import QtQuick.Window
import Mnkybox 1.0
import "components"
import "overlays"

// mnkybox shell: tab row, persistent transport strip, one of ten screens, and
// the keypad/confirmation overlays. 800 x 480 landscape-first, resizable.
// Face switches (a tab tap or a SHIFT toggle) fade the screen host over 80 ms;
// the popups own their 120 ms scale-fade from the summary button that opened
// them.
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

    function screenSource() {
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
                anchors.leftMargin: Style.marginCompact
                anchors.rightMargin: Style.marginCompact
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                spacing: Style.gapTight

                Repeater {
                    model: tabBar.tabCount
                    delegate: Rectangle {
                        width: (parent.width - Style.gapTight * (tabBar.tabCount - 1)) /
                               tabBar.tabCount
                        height: parent.height
                        radius: Style.corner
                        color: app.tab === index ? Style.accent : Style.panel
                        // "tab.N" is a panel focus stop (AppController's ring);
                        // carrying it here makes the ring visible and lets the
                        // --ui-taps runner exercise setTab through QML.
                        property string focusId: "tab." + index
                        border.width: app.focusId === focusId ? 2
                                     : (tabBar.shiftMode ? 1 : 0)
                        border.color: app.focusId === focusId ? Style.accent
                                     : (tabBar.shiftMode ? Style.accent : Style.outline)

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            text: window.tabNames[index]
                            color: app.tab === index ? Style.panelDark : Style.text
                            font.pixelSize: Style.fontTab
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            visible: tabBar.shiftMode
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 6
                            height: 2
                            radius: 1
                            color: Style.accent
                        }

                        Rectangle {
                            visible: index === 5 && app.dirty
                            x: parent.width - 12
                            y: parent.height - 10
                            width: 6
                            height: 6
                            radius: 3
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
            source: window.screenSource()
            opacity: 1.0

            NumberAnimation {
                id: faceFade
                target: screenLoader
                property: "opacity"
                to: 1.0
                duration: Style.motionFace
                easing.type: Easing.OutCubic
            }

            // A tab tap or a SHIFT toggle is a face switch: dip the new face in
            // over 80 ms so the change reads as a transition, not a jump.
            function crossfade() {
                opacity = 0.0
                faceFade.restart()
            }
            onSourceChanged: crossfade()

            Connections {
                target: app
                function onPanelModeChanged() { screenLoader.crossfade() }
            }
        }
    }

    ValuePopup {
        // Top band; the component moves below the finger when the finger is in
        // that band (fullscreen EQ grid), so the readout is never under the
        // touch point. The panel EDIT path has no anchor and keeps the band.
        anchorY: app.touchAnchorY
        popupVisible: app.valuePopupVisible
        popupLabel: app.valuePopupLabel
        popupText: app.valuePopupText
    }

    ChannelPopout {
        anchors.fill: parent
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
