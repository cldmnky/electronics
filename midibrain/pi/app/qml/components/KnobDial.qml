import QtQuick
import Mnkybox 1.0

// Circular parameter indicator: a raised knob disc, a 270-degree arc track and
// an accent arc from the minimum to the current value. It paints only; the
// interactive wrapper is TouchKnob.qml. The arc starts at 135 degrees (lower
// left) and sweeps clockwise, so a centred value points straight up.
Item {
    id: root

    property real fraction: 0.0
    property color tone: Style.accent
    property bool dimmed: false

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const centreX = width / 2
            const centreY = height / 2
            const radius = Math.max(4, Math.min(width, height) / 2 - 4)
            const start = Math.PI * 0.75
            const sweep = Math.PI * 1.5
            const value = Math.max(0, Math.min(1, root.fraction))

            // Raised disc with a hairline edge.
            const disc = ctx.createRadialGradient(centreX - radius * 0.3,
                                                  centreY - radius * 0.35, radius * 0.2,
                                                  centreX, centreY, radius)
            disc.addColorStop(0.0, root.dimmed ? Style.panelDark.toString()
                                               : Style.raised.toString())
            disc.addColorStop(1.0, Style.panelDark.toString())
            ctx.fillStyle = disc
            ctx.beginPath()
            ctx.arc(centreX, centreY, radius, 0, Math.PI * 2)
            ctx.fill()
            ctx.strokeStyle = Style.outline.toString()
            ctx.lineWidth = 1
            ctx.stroke()

            // Arc track and value arc.
            ctx.lineCap = "round"
            ctx.strokeStyle = Style.outline.toString()
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.arc(centreX, centreY, radius - 2, start, start + sweep)
            ctx.stroke()
            if (value > 0.001) {
                ctx.strokeStyle = root.dimmed ? Style.textDim.toString()
                                              : root.tone.toString()
                ctx.lineWidth = 3
                ctx.beginPath()
                ctx.arc(centreX, centreY, radius - 2, start, start + sweep * value)
                ctx.stroke()
            }

            // Pointer along the value angle.
            const angle = start + sweep * value
            ctx.strokeStyle = root.dimmed ? Style.textDim.toString() : Style.text.toString()
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(centreX + Math.cos(angle) * radius * 0.34,
                       centreY + Math.sin(angle) * radius * 0.34)
            ctx.lineTo(centreX + Math.cos(angle) * radius * 0.74,
                       centreY + Math.sin(angle) * radius * 0.74)
            ctx.stroke()
        }
    }

    onFractionChanged: canvas.requestPaint()
    onToneChanged: canvas.requestPaint()
    onDimmedChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
