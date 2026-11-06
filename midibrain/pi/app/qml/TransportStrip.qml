import QtQuick
import Mnkybox 1.0
import "components"

// Persistent transport strip directly under the tab row: rolling/REC state,
// bar:beat plus elapsed time, BPM, the metronome toggle and the dirty dot.
// No-Pico mode adds the touch transport because the panel buttons live on the
// absent Pico. The touch buttons are drawn glyphs, and REC pulses while the
// engine is actually recording. In hardware mode the physical panel owns them.
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
        spacing: Style.gap

        // Every width is fixed except the notice, so the row computes the
        // leftover instead of overlapping the touch transport in no-Pico mode.
        readonly property int baseWidth: 44 + (app.noPicoMode ? 150 : 190) +
                                         (app.noPicoMode ? 84 : 90) +
                                         (app.noPicoMode ? 56 : 0) + 72 + 10
        readonly property int noPicoWidth: app.noPicoMode ? 3 * 48 : 0
        readonly property int itemCount: app.noPicoMode ? 10 : 6

        Text {
            width: 44
            height: parent.height
            text: app.transportState
            color: app.transportState === "REC" ? Style.record
                   : (app.transportState === "ROLL" ? Style.accent : Style.textDim)
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: app.noPicoMode ? 150 : 190
            height: parent.height
            text: app.positionText
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: app.noPicoMode ? 84 : 90
            height: parent.height
            text: app.tempoText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            // Persistent bare-Pi marker: the mixer/transport shots must show
            // that this run has no Pico even when the System screen is closed.
            width: app.noPicoMode ? 56 : 0
            height: parent.height
            visible: app.noPicoMode
            text: "NO PICO"
            color: Style.record
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        TransportGlyphButton {
            width: app.noPicoMode ? 48 : 0
            height: Style.touch
            visible: app.noPicoMode
            glyph: "play"
            checked: app.rolling
            focusId: "transport.play"
            onClicked: app.play()
        }
        TransportGlyphButton {
            width: app.noPicoMode ? 48 : 0
            height: Style.touch
            visible: app.noPicoMode
            glyph: "stop"
            checked: !app.rolling
            focusId: "transport.stop"
            onClicked: app.stop()
        }
        TransportGlyphButton {
            width: app.noPicoMode ? 48 : 0
            height: Style.touch
            visible: app.noPicoMode
            glyph: "rec"
            checked: app.recording
            recording: app.recording
            focusId: "transport.rec"
            onClicked: app.recToggle()
        }
        Text {
            width: Math.max(0, stripRow.width - stripRow.baseWidth -
                               stripRow.noPicoWidth -
                               stripRow.spacing * (stripRow.itemCount - 1))
            height: parent.height
            text: app.noticeText
            color: Style.accent
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        TouchButton {
            width: 72
            height: Style.touch
            anchors.verticalCenter: parent.verticalCenter
            text: "CLICK"
            textSize: Style.fontSmall
            variant: "ghost"
            checked: app.clickEnabled
            focusId: "transport.click"
            onClicked: app.setClickEnabled(!app.clickEnabled)
        }
        Item {
            width: 10
            height: parent.height
            Rectangle {
                visible: app.dirty
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: Style.accent
            }
        }
    }
}
