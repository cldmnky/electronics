import QtQuick
import Mnkybox 1.0

// Small mnkybox mark for the SYSTEM and PROJECTS headers: a drawn three-bar
// waveform in a rounded tile plus the wordmark. It is decoration, never a
// control: no focusId and no touch handler.
Item {
    id: root

    implicitWidth: 118
    implicitHeight: 28

    Rectangle {
        id: tile
        width: 26
        height: 26
        anchors.verticalCenter: parent.verticalCenter
        radius: Style.corner
        color: Style.raised
        border.width: Style.outlineWidth
        border.color: Style.outline

        Canvas {
            anchors.fill: parent
            anchors.margins: 5
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.fillStyle = Style.accent.toString()
                const bars = [0.42, 1.0, 0.68]
                const barWidth = Math.max(2, width / 5)
                for (let i = 0; i < bars.length; ++i) {
                    const h = height * bars[i]
                    ctx.fillRect(i * (barWidth + 2), (height - h) / 2, barWidth, h)
                }
            }
        }
    }

    Text {
        anchors.left: tile.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: "mnkybox"
        color: Style.textDim
        font.pixelSize: Style.fontBody
        font.weight: Font.DemiBold
        font.letterSpacing: 0.5
    }
}
