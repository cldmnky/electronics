import QtQuick
import QtQuick.Window
import Mnkybox 1.0
import "components"
import "overlays"

// mnkybox shell: tab row, persistent transport strip, one of nine screens, the
// take edit popup, the ARRANGE clip sheet and the keypad/confirmation overlays.
// 800 x 480 landscape-first, resizable. The global FX tab retired into the
// per-track FX rack popup (ChannelPopout > TRACK FX) and EDIT retired into the
// take edit popup (overlays/EditPopup.qml), so the tab row is Mixer..ARRANGE.
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

    // Tab identifiers are fixed (AppController::setTab indices): Mixer 0,
    // Routing 1, Recorder 2, System 3, Projects 4, MIDI 5, EQ 6, XY 7,
    // Arrange 8. Stage 1 shows four persistent destinations; the six remaining
    // pages live behind More in the existing tab-grid mechanism.
    readonly property var tabNames: ["Mixer", "Routing", "Recorder", "System",
                                     "Projects", "MIDI", "EQ", "XY", "Arrange"]
    // One source with AppController::morePageTabs(): the ring and the sheet
    // must never disagree about which pages More contains.
    readonly property var morePageTabs: app.morePageTabs
    readonly property bool morePageActive: morePageTabs.indexOf(app.tab) >= 0
    // Every modal surface above the shell disables the shell's own touch
    // handlers. Without this a tap in the top 52 px band is delivered to the
    // tab row's TapHandler as well as the popup (the stage-3 Edit-popup
    // collapse and the sheet-backdrop leak).
    readonly property bool modalOpen: config.open || fx.open || mixer.channelOpen ||
                                      timeline.editOpen || arrange.clipMenuOpen ||
                                      arrange.lengthPopupOpen || app.keypadOpen ||
                                      app.confirmOpen || app.takeSheetOpen ||
                                      app.tempoPopupOpen || app.bankGridOpen || transport.optionsOpen

    function moreLabel(tab) {
        switch (tab) {
        case 1: return "Routing"
        case 4: return "Projects"
        case 5: return "MIDI"
        case 6: return "EQ"
        case 7: return "XY"
        case 3: return "System"
        default: return "More"
        }
    }

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
        case 8: return "screens/ArrangeScreen.qml"
        default: return ""
        }
    }

    Column {
        anchors.fill: parent

        // Four destinations at 56 px (Style.touchSecondary, ~5.3 mm): Mixer,
        // Recorder, Arrange and More. More opens the six remaining pages in
        // the grid below; the ring walks them as tabgrid.<tab index>, so the
        // controller's setTab indices never change.
        Rectangle {
            id: tabBar
            width: parent.width
            height: Style.tabHeight
            color: Style.panelDark
            visible: !app.fullscreen
            enabled: !window.modalOpen

            readonly property bool shiftMode: app.panelMode === "shift"

            Row {
                id: navRow
                anchors.fill: parent
                anchors.leftMargin: Style.marginCompact
                anchors.rightMargin: Style.marginCompact
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                spacing: Style.gap

                Repeater {
                    model: [
                        { label: "Mixer", tab: 0, id: "nav.mixer" },
                        { label: "Recorder", tab: 2, id: "nav.recorder" },
                        { label: "Arrange", tab: 8, id: "nav.arrange" },
                        { label: "More", tab: -1, id: "nav.more" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        property string focusId: modelData.id
                        readonly property bool isMore: modelData.tab < 0
                        readonly property bool checked: isMore
                                                        ? (app.tabGridOpen ||
                                                           window.morePageActive)
                                                        : app.tab === modelData.tab
                        width: (navRow.width - navRow.spacing * 3) / 4
                        height: navRow.height
                        radius: Style.corner
                        color: checked ? Style.accent : Style.panel
                        border.width: app.focusId === focusId ? 2
                                     : (tabBar.shiftMode ? 1 : 0)
                        border.color: app.focusId === focusId ? Style.accent
                                     : (tabBar.shiftMode ? Style.accent : Style.outline)

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            text: parent.isMore && !app.tabGridOpen &&
                                  window.morePageActive
                                  ? window.moreLabel(app.tab)
                                  : modelData.label
                            color: parent.checked ? Style.panelDark : Style.text
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
                            id: navTap
                            property bool heldShift: false
                            onPressedChanged: if (pressed) heldShift = false
                            onTapped: {
                                if (heldShift) return
                                if (parent.isMore) {
                                    app.openTabGrid()
                                } else {
                                    app.setTab(parent.modelData.tab)
                                    if (app.panelMode === "shift")
                                        app.showShiftFace(false)
                                }
                            }
                            onLongPressed: {
                                heldShift = true
                                if (parent.isMore) {
                                    app.openTabGrid()
                                } else {
                                    app.setTab(parent.modelData.tab)
                                    app.showShiftFace(true)
                                }
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
            enabled: !window.modalOpen
        }

        Loader {
            id: screenLoader
            width: parent.width
            height: parent.height - (tabBar.visible ? tabBar.height : 0)
                    - (transport.visible ? Style.transportHeight : 0)
            source: window.screenSource()
            opacity: 1.0
            // The take sheet and the bank grid live inside the screen, so the
            // screen itself must stay enabled while a modal is open; only the
            // shell chrome is disabled (the top-band leak is a chrome leak).
            enabled: !app.tabGridOpen

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


    // More page grid: the existing tab-grid mechanism, limited to the six pages
    // the four-destination nav does not show. It overlays the screen instead of
    // growing the nav row, so the screens keep their fixed height arithmetic.
    Rectangle {
        id: moreBackdrop
        anchors.fill: parent
        anchors.topMargin: tabBar.height
        visible: app.tabGridOpen && !app.fullscreen
        color: "#88000000"
        z: 60

        MouseArea {
            anchors.fill: parent
            onClicked: app.closeTabGrid()
        }
    }

    Rectangle {
        id: moreSheet
        y: tabBar.height
        width: parent.width
        height: 2 * Style.touchPrimary + Style.gap + Style.marginCompact * 2
        visible: app.tabGridOpen && !app.fullscreen
        color: Style.panel
        border.width: Style.outlineWidth
        border.color: Style.outline
        z: 61

        Item {
            id: gridFrame
            x: Style.marginCompact
            y: Style.marginCompact
            width: parent.width - Style.marginCompact * 2
            height: 2 * Style.touchPrimary + Style.gap

            Grid {
                id: moreGrid
                anchors.fill: parent
                columns: 3
                spacing: Style.gap

                Repeater {
                    model: window.morePageTabs
                    delegate: Rectangle {
                        required property int modelData
                        property string focusId: "tabgrid." + modelData
                        width: (moreGrid.width - moreGrid.spacing * 2) / 3
                        height: Style.touchPrimary
                        radius: Style.corner
                        color: app.tab === modelData ? Style.accent : Style.raised
                        border.width: app.focusId === focusId ? 2
                                     : Style.outlineWidth
                        border.color: app.focusId === focusId
                                      ? Style.accent : Style.outline

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            text: window.moreLabel(modelData)
                            color: app.tab === modelData ? Style.panelDark : Style.text
                            font.pixelSize: Style.fontLabel
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        TapHandler {
                            onTapped: {
                                app.closeTabGrid()
                                app.setTab(modelData)
                            }
                        }
                    }
                }
            }
        }
    }


    // Transport options sheet (stage 1 item 3): recording mode, metronome and
    // the rare choices moved off the strip but stay one tap away. Touch-only;
    // the panel reaches the same functions through its ring stops.
    Rectangle {
        id: transportOptions
        anchors.fill: parent
        visible: transport.optionsOpen
        color: "#88000000"
        z: 62

        MouseArea {
            anchors.fill: parent
            onClicked: transport.optionsOpen = false
        }
    }

    Rectangle {
        id: transportOptionsPanel
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Style.marginCompact
        width: 392
        height: optionsColumn.implicitHeight + Style.margin
        visible: transport.optionsOpen
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent
        z: 63

        Column {
            id: optionsColumn
            x: Style.marginCompact
            y: Style.marginCompact
            width: parent.width - Style.margin
            spacing: Style.gap

            Text {
                text: "TRANSPORT OPTIONS"
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
            }
            Row {
                spacing: Style.gap
                TouchButton {
                    width: 184
                    height: Style.touchPrimary
                    text: "RECMODE  " + app.recModeText
                    textSize: Style.fontSecondary
                    variant: "secondary"
                    chevron: "down"
                    onClicked: app.openRecMode()
                }
                TouchButton {
                    width: 184
                    height: Style.touchPrimary
                    text: app.clickLabel
                    textSize: Style.fontSecondary
                    variant: "ghost"
                    checked: app.clickEnabled
                    onClicked: app.setClickEnabled(!app.clickEnabled)
                }
            }
            Row {
                spacing: Style.gap
                TouchButton {
                    width: 184
                    height: Style.touchSecondary
                    text: "CLICK SETUP"
                    textSize: Style.fontSecondary
                    variant: "ghost"
                    onClicked: {
                        transport.optionsOpen = false
                        app.openConfig("metronome")
                    }
                }
                TouchButton {
                    width: 184
                    height: Style.touchSecondary
                    text: "CLOSE"
                    textSize: Style.fontSecondary
                    variant: "ghost"
                    onClicked: transport.optionsOpen = false
                }
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

    // W5: the take edit popup (EDIT tab retired). It is above the screens and
    // below the keypad/confirm modals.
    EditPopup {
        anchors.fill: parent
    }

    // W6: the ARRANGE clip "..." sheet.
    ArrangeClipPopup {
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
