import QtQuick
import Mnkybox 1.0
import "components"

// Persistent transport strip under the nav row. Sizing policy: the
// three transport verbs are 64 px (Style.touchPrimary, ~6.1 mm), the musical
// position and tempo are read at Style.fontValue (24 px), and the feedback
// readout has a defined width instead of competing for the row's leftover.
// Recording mode, metronome and the rare choices moved into the transport
// options sheet; RECMODE stays visible next to REC whenever it changes what
// pressing REC will do. Configure is the touch twin of the panel SHIFT key and
// toggles the current tab's second face.
Rectangle {
    id: root

    color: Style.panel
    border.width: 0

    // The options sheet is local touch-only state: it is not a panel context.
    property bool optionsOpen: false

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Style.outline
    }

    Row {
        id: stripRow

        anchors.fill: parent
        anchors.leftMargin: Style.marginCompact
        anchors.rightMargin: Style.marginCompact
        anchors.topMargin: 0
        anchors.bottomMargin: 0
        spacing: Style.gapTight

        readonly property int positionWidth: 108
        readonly property int readoutWidth: 100
        // Fixed widths only; the readout below claims readoutWidth and the
        // row never needs to compute a shrinking remainder.
        readonly property int fixedWidth: positionWidth + 84 + 3 * Style.touchPrimary +
                                          44 + readoutWidth + 88 + 84 +
                                          (app.noPicoMode ? 52 : 0)

        // Musical position is the primary readout of the strip: bar:beat at
        // 24 px, elapsed time as 14 px secondary information on the second
        // line. The full controller string is "BARS:BEATS:TICKS / MM:SS".
        Item {
            width: stripRow.positionWidth
            height: parent.height
            readonly property var parts: app.positionText.split(" / ")

            // Loop-wrap pulse: the same accent cue as the ARRANGE loop band, so
            // a wrap is visible in the always-present transport strip too.
            Rectangle {
                id: wrapFlash
                anchors.fill: parent
                anchors.margins: -4
                radius: Style.corner
                color: Style.accent
                opacity: 0.0
                SequentialAnimation {
                    id: wrapPulse
                    PropertyAnimation { target: wrapFlash; property: "opacity";
                                        to: 0.35; duration: 40 }
                    PauseAnimation { duration: 120 }
                    PropertyAnimation { target: wrapFlash; property: "opacity";
                                        to: 0.0; duration: 120 }
                }
                Connections {
                    target: app
                    function onLoopWrapChanged() {
                        if (app.loopWrapFlash) wrapPulse.restart()
                    }
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                Text {
                    width: parent.width
                    text: parent.parent.parts[0]
                    color: Style.text
                    font.pixelSize: Style.fontValue - 2
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Row {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: parent.parent.parent.parts.length > 1
                              ? parent.parent.parent.parts[1] : ""
                        color: Style.textDim
                        font.pixelSize: Style.fontSecondary
                    }
                    // SONG mode stays a permanent, visible indication; the
                    // tag is the touch toggle and the panel's ring stop.
                    Rectangle {
                        width: 48
                        height: 22
                        radius: 4
                        color: app.songMode ? Style.accent : Style.panelDark
                        border.width: Style.outlineWidth
                        border.color: app.songMode ? Style.accent : Style.outline
                        property string focusId: "transport.song"

                        Text {
                            anchors.centerIn: parent
                            text: "SONG"
                            color: app.songMode ? Style.panelDark : Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                        }
                        TapHandler {
                            onTapped: app.toggleSongMode()
                        }
                    }
                }
            }
        }
        // Tempo is a control, not a readout. The chevron opens the TEMPO
        // popup (40..240 slider, +-1 buttons, TAP).
        TouchButton {
            width: 84
            height: Style.touchPrimary
            // The strip shows the BPM value; the popup carries the full label.
            text: app.tempoText.split(" ")[0]
            textSize: Style.fontLabel
            variant: "ghost"
            chevron: "down"
            focusId: "transport.tempo"
            onClicked: app.openTempoPopup()
        }
        TransportGlyphButton {
            width: Style.touchPrimary
            height: Style.touchPrimary
            glyph: "play"
            checked: app.rolling
            focusId: "transport.play"
            onClicked: app.play()
        }
        TransportGlyphButton {
            width: Style.touchPrimary
            height: Style.touchPrimary
            glyph: "stop"
            checked: !app.rolling
            focusId: "transport.stop"
            onClicked: app.stop()
        }
        TransportGlyphButton {
            width: Style.touchPrimary
            height: Style.touchPrimary
            glyph: "rec"
            checked: app.recording
            recording: app.recording
            focusId: "transport.rec"
            onClicked: app.recToggle()
            onLongPressed: app.openRecMode()
        }
        // RECMODE badge: always states what pressing REC will do. NOW is the
        // default and stays dim; COUNT-IN and PUNCH are accent because they
        // change the REC behaviour. Tap opens the picker.
        Rectangle {
            width: 44
            height: Style.touchPrimary
            radius: Style.corner
            color: Style.panelDark
            border.width: app.recModeText === "NOW" ? Style.outlineWidth : 2
            border.color: app.recModeText === "NOW" ? Style.outline : Style.accent
            property string focusId: "transport.recmode"
            readonly property string shortText: app.recModeText === "COUNT-IN" ? "CIN"
                                                : (app.recModeText === "PUNCH" ? "PCH" : "NOW")

            Text {
                anchors.centerIn: parent
                width: parent.width - 4
                text: parent.shortText
                color: app.recModeText === "NOW" ? Style.textDim : Style.accent
                font.pixelSize: Style.fontSecondary
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            TapHandler {
                onTapped: app.openRecMode()
            }
        }
        // Defined feedback area: the controller's notice or the transient
        // value readout replaces its content, never the row's layout.
        Rectangle {
            width: stripRow.readoutWidth
            height: Style.touchPrimary
            radius: Style.corner
            color: Style.panelDark
            border.width: Style.outlineWidth
            border.color: Style.outline

            readonly property bool showingValue: app.valuePopupVisible

            Text {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                text: parent.showingValue
                      ? (app.valuePopupLabel + "  " + app.valuePopupText)
                      : app.noticeText
                color: parent.showingValue ? Style.accent
                       : (app.noticeKind === "info" ? Style.accent : Style.record)
                font.pixelSize: Style.fontSecondary
                font.weight: parent.showingValue ? Font.DemiBold : Font.Normal
                horizontalAlignment: parent.showingValue ? Text.AlignRight
                                                         : Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        // Configure is the visible touch action that opens the second face.
        // PRIMARY (not "< PRIMA...") is the return label: it fits in 84 px at
        // 14 px and names the face it returns to.
        TouchButton {
            width: 88
            height: Style.touchPrimary
            text: app.panelMode === "shift" ? "PRIMARY" : "Configure"
            textSize: Style.fontSecondary
            variant: "ghost"
            checked: app.panelMode === "shift"
            focusId: "transport.shift"
            opacity: app.tabHasShiftFace(app.tab) ? 1.0 : 0.4
            onClicked: app.showShiftFace(app.panelMode !== "shift")
        }
        // One clear affordance for the moved choices.
        TouchButton {
            width: 84
            height: Style.touchPrimary
            text: "Options"
            textSize: Style.fontSecondary
            variant: "ghost"
            chevron: root.optionsOpen ? "down" : "up"
            onClicked: root.optionsOpen = !root.optionsOpen
        }
        // the bare-Pi marker is a chip, not a badge. Touch-only; it is not
        // a panel stop (there is no panel in no-Pico mode), so it carries no
        // focusId and the tap gate reaches it through its own scenario below.
        Rectangle {
            width: app.noPicoMode ? 52 : 0
            height: Style.touchPrimary
            visible: app.noPicoMode
            radius: Style.corner
            color: Style.panelDark
            border.width: Style.outlineWidth
            border.color: Style.record

            // Two 11 px lines rather than one clipped "NO PICO": the chip is
            // the at-a-glance bare-Pi marker, not a shrunken label.
            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "NO"
                    color: Style.record
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "PICO"
                    color: Style.record
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                }
            }
            TapHandler {
                onTapped: app.showNoPicoInfo()
            }
        }
    }
}
