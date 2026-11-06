import QtQuick
import QtQuick.Window
import Mnkybox 1.0
import "components"
import "overlays"

// mnkybox shell: tab row, persistent transport strip, one of nine screens, and
// the keypad/confirmation overlays. 800 x 480 landscape-first, resizable. The
// global FX tab retired into the per-track FX rack popup (ChannelPopout >
// TRACK FX), so the tab row is Mixer..EDIT with no FX entry.
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

    // B3/C8: tab 5 is PROJ and tab 6 is MIDI; "Monitor" named two other
    // surfaces (the per-track MON mark and the SYSTEM SHIFT OUTPUT page).
    readonly property var tabNames: ["Mixer", "Routing", "Recorder", "System",
                                     "PROJ", "MIDI", "EQ", "XY", "EDIT"]

    function screenSource() {
        switch (app.tab) {
        case 0: return "screens/MixerScreen.qml"
        case 1: return "screens/RoutingScreen.qml"
        case 2: return "screens/RecorderScreen.qml"
        case 3: return "screens/SystemScreen.qml"
        case 4: return "screens/ProjectsScreen.qml"
        case 5: return "screens/MidiMonitorScreen.qml"
        case 6: return "screens/EqScreen.qml"
        case 7: return "screens/XyScreen.qml"
        case 8: return "screens/TimelineScreen.qml"
        default: return ""
        }
    }

    Column {
        anchors.fill: parent

        Rectangle {
            id: tabBar
            width: parent.width
            // C3: the panel ring has one TAB stop, and pressing it expands the
            // tab row into a 2x5 grid. The row grows by one 44 px cell row.
            height: app.tabGridOpen ? Style.touch * 2 + 12 : Style.tabHeight
            color: Style.panelDark
            // EQ fullscreen hides the whole bridge (tab row + transport strip);
            // AppController::setTab restores it when another tab is picked.
            visible: !app.fullscreen

            readonly property bool shiftMode: app.panelMode === "shift"
            readonly property int tabCount: 9
            // The single TAB ring stop is the row itself; its focus ring is the
            // accent border so a panel user sees where the ring is.
            readonly property bool tabStopFocused: app.focusId === "tab.menu"
            border.width: tabStopFocused ? 2 : 0
            border.color: Style.accent

            Item {
                id: tabHost
                anchors.fill: parent
                anchors.leftMargin: Style.marginCompact
                anchors.rightMargin: Style.marginCompact
                anchors.topMargin: 4
                anchors.bottomMargin: 4

                Repeater {
                    model: tabBar.tabCount
                    delegate: Rectangle {
                        readonly property int columns: app.tabGridOpen ? 5 : tabBar.tabCount
                        width: (parent.width - Style.gapTight * (columns - 1)) / columns
                        height: app.tabGridOpen ? Style.touch
                                                : parent.height
                        x: (index % columns) * (width + Style.gapTight)
                        y: Math.floor(index / columns) * (height + Style.gapTight)
                        radius: Style.corner
                        color: app.tab === index ? Style.accent : Style.panel
                        // "tab.N" stays the direct touch target and the
                        // --ui-taps handle. The panel ring reaches the row
                        // through the single "tab.menu" stop and, while the
                        // grid is open, through "tabgrid.N".
                        property string focusId: "tab." + index
                        border.width: (app.focusId === focusId ||
                                       app.focusId === ("tabgrid." + index)) ? 2
                                     : (tabBar.shiftMode ? 1 : 0)
                        border.color: (app.focusId === focusId ||
                                       app.focusId === ("tabgrid." + index)) ? Style.accent
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
                                // D3: a tab without a second face never enters
                                // the dead SHIFT state; the controller says so.
                                app.showShiftFace(true)
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
        // B6: the transport strip owns ordinary value readouts; the floating
        // band exists only on the fullscreen EQ face, which hides the strip.
        popupVisible: app.valuePopupVisible && app.fullscreen
        popupLabel: app.valuePopupLabel
        popupText: app.valuePopupText
    }

    TempoPopup {
        anchors.fill: parent
    }

    ChannelPopout {
        anchors.fill: parent
    }

    TrackFxPopup {
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
