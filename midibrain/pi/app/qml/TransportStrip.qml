import QtQuick
import Mnkybox 1.0
import "components"

// Persistent transport strip directly under the tab row: rolling/REC state,
// bar:beat plus elapsed time, the touch tempo button, the metronome toggle, the
// SHIFT chip and the notice slot.
// The play/stop/rec glyphs are always drawn: the panel owns them in hardware
// mode (so they carry no focus id there), but every bench and QA session with
// no panel harness still gets a touch transport. REC pulses while the engine is
// actually recording. The SHIFT chip is the touch twin of the panel's SHIFT
// key: it toggles the current tab's second face, and a tab without one renders
// it dimmed and says so.
Rectangle {
    id: root

    color: Style.panel
    border.width: 0

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
        spacing: Style.gapTight

        // Every width is fixed except the notice, so the row computes the
        // leftover instead of overlapping a neighbour.
        readonly property int positionWidth: app.noPicoMode ? 132 : 148
        readonly property int fixedWidth: 36 + positionWidth + 96 +
                                          (app.noPicoMode ? 52 : 0) + 3 * 44 +
                                          74 + 88 + 62 + 8
        readonly property int itemCount: app.noPicoMode ? 11 : 10

        Text {
            width: 36
            height: parent.height
            text: app.transportState
            color: app.transportState === "REC" ? Style.record
                   : (app.transportState === "PLAY" ? Style.accent : Style.textDim)
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: stripRow.positionWidth
            height: parent.height
            text: app.positionText
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        // A7: tempo is a control, not a readout. The chevron opens the TEMPO
        // popup (40..240 slider, +-1 buttons, TAP).
        TouchButton {
            width: 96
            height: Style.touch
            text: app.tempoText
            textSize: Style.fontSmall
            variant: "ghost"
            chevron: "down"
            focusId: "transport.tempo"
            onClicked: app.openTempoPopup()
        }
        // B5: the bare-Pi marker is a chip, not a badge. Tapping it explains
        // what is absent and what stays live, with an OPEN SYSTEM action.
        Rectangle {
            width: app.noPicoMode ? 52 : 0
            height: Style.touch
            visible: app.noPicoMode
            radius: Style.corner
            color: Style.panelDark
            border.width: Style.outlineWidth
            border.color: Style.record
            property string focusId: "transport.nopico"

            Text {
                anchors.centerIn: parent
                text: "NO PICO"
                color: Style.record
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
            TapHandler {
                onTapped: app.showNoPicoInfo()
            }
        }
        // A5: always available. The panel ring owns these stops only in
        // no-Pico mode, where no panel exists to press them.
        TransportGlyphButton {
            width: 44
            height: Style.touch
            glyph: "play"
            checked: app.rolling
            focusId: app.noPicoMode ? "transport.play" : ""
            onClicked: app.play()
        }
        TransportGlyphButton {
            width: 44
            height: Style.touch
            glyph: "stop"
            checked: !app.rolling
            focusId: app.noPicoMode ? "transport.stop" : ""
            onClicked: app.stop()
        }
        TransportGlyphButton {
            width: 44
            height: Style.touch
            glyph: "rec"
            checked: app.recording
            recording: app.recording
            // W2: the long press opens the NOW/COUNT-IN/PUNCH picker; the
            // RECMODE readout beside the glyph is the focusable twin.
            focusId: app.noPicoMode ? "transport.rec" : ""
            onClicked: app.recToggle()
            onLongPressed: app.openRecMode()
        }
        // W2 readout: which RECMODE is armed, next to the REC glyph.
        Text {
            width: 74
            height: parent.height
            // Panel focus stop: PRESS opens the RECMODE picker (the REC glyph's
            // long press is the touch gesture for the same popup).
            property string focusId: "transport.recmode"
            text: app.recModeText
            color: app.recModeText === "NOW" ? Style.textDim : Style.accent
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            TapHandler {
                onTapped: app.openRecMode()
            }
        }
        // C6: the slot is an inset well with severity colour, not a floating
        // label. B6: an ordinary value readout renders here (accent, right
        // aligned, 1.5 s TTL from the controller); the floating band is kept
        // for the fullscreen EQ face, which has no strip.
        Rectangle {
            width: Math.max(0, stripRow.width - stripRow.fixedWidth -
                               stripRow.spacing * (stripRow.itemCount - 1))
            height: Style.touch
            radius: Style.corner
            color: Style.panelDark
            border.width: Style.outlineWidth
            border.color: Style.outline

            readonly property bool showingValue: app.valuePopupVisible

            Text {
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 11
                text: parent.showingValue
                      ? (app.valuePopupLabel + "  " + app.valuePopupText)
                      : app.noticeText
                color: parent.showingValue ? Style.accent
                       : (app.noticeKind === "info" ? Style.accent : Style.record)
                font.pixelSize: Style.fontSmall
                font.weight: parent.showingValue ? Font.DemiBold : Font.Normal
                horizontalAlignment: parent.showingValue ? Text.AlignRight
                                                         : Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
        // B1: the SHIFT face is reachable by touch. Dimmed on a tab without a
        // second face, and a tap there names the reason (D3).
        TouchButton {
            width: 88
            height: Style.touch
            text: app.panelMode === "shift" ? "< PRIMARY" : "SHIFT >"
            textSize: Style.fontSmall
            variant: "ghost"
            checked: app.panelMode === "shift"
            focusId: "transport.shift"
            opacity: app.tabHasShiftFace(app.tab) ? 1.0 : 0.4
            onClicked: app.showShiftFace(app.panelMode !== "shift")
        }
        TouchButton {
            width: 62
            height: Style.touch
            text: app.clickLabel
            textSize: Style.fontSmall
            variant: "ghost"
            checked: app.clickEnabled
            focusId: "transport.click"
            onClicked: app.setClickEnabled(!app.clickEnabled)
            // A8: a long press opens the metronome popup; a tap only toggles.
            onLongClicked: app.openConfig("metronome")
        }
        // C5: the dirty indicator lives once, next to the project name; the
        // transport strip keeps the 8 px end margin only.
        Item {
            width: 8
            height: parent.height
        }
    }
}
