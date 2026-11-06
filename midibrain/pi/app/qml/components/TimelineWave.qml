import QtQuick
import Mnkybox 1.0

// Peak overview for one take lane or the region-ops preview. `values` is the
// reduced `.peaks` overview (0..1); an empty list draws the explicit placeholder
// block pattern, never a blocking scan. The waveform is an antialiased polyline
// with a mirrored fill, and `playheadRatio` shades the part of the lane ahead of
// the transport (EDIT). The optional envelope draws the draft gain and linear
// fade ramps used by the region-ops popup.
Item {
    id: root

    property var values: []
    property bool showEnvelope: false
    property real gainDb: 0
    property real fadeInRatio: 0
    property real fadeOutRatio: 0
    property real playheadRatio: -1
    property color waveColor: Style.accent
    property color envelopeColor: Style.textDim
    property string placeholderText: "NO PEAKS"

    onValuesChanged: canvas.requestPaint()
    onShowEnvelopeChanged: canvas.requestPaint()
    onGainDbChanged: canvas.requestPaint()
    onFadeInRatioChanged: canvas.requestPaint()
    onFadeOutRatioChanged: canvas.requestPaint()
    onPlayheadRatioChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    // The placeholder text is a Text item, not Canvas fillText: a Text elides
    // cleanly and does not leave a ghost when the lane width settles after the
    // first paint.
    Text {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        visible: !root.values || root.values.length === 0
        text: root.placeholderText
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const centre = height / 2
            if (!root.values || root.values.length === 0) {
                // Placeholder: dotted block rows along the top and bottom edge,
                // so a missing/stale sidecar is visibly "no overview", not a
                // flat signal, and the centre strip stays clear for the label.
                ctx.fillStyle = Style.outline
                for (let row = 0; row < 2; ++row) {
                    const y = row === 0 ? 3 : height - 6
                    for (let x = 2; x < width; x += 14) {
                        ctx.fillRect(x, y, 8, 3)
                    }
                }
                return
            }
            const count = root.values.length
            const step = width / Math.max(1, count - 1)
            const points = []
            for (let i = 0; i < count; ++i) {
                const amp = Math.min(1, Math.max(0, Number(root.values[i])))
                points.push({x: i * step, y: Math.max(1, amp * (height / 2 - 2))})
            }

            // Mirrored fill: upper envelope forward, lower envelope back.
            ctx.beginPath()
            ctx.moveTo(points[0].x, centre - points[0].y)
            for (let i = 1; i < count; ++i) {
                ctx.lineTo(points[i].x, centre - points[i].y)
            }
            for (let i = count - 1; i >= 0; --i) {
                ctx.lineTo(points[i].x, centre + points[i].y)
            }
            ctx.closePath()
            const fill = ctx.createLinearGradient(0, 0, 0, height)
            fill.addColorStop(0.0, Qt.rgba(root.waveColor.r, root.waveColor.g,
                                           root.waveColor.b, 0.10))
            fill.addColorStop(0.5, Qt.rgba(root.waveColor.r, root.waveColor.g,
                                           root.waveColor.b, 0.32))
            fill.addColorStop(1.0, Qt.rgba(root.waveColor.r, root.waveColor.g,
                                           root.waveColor.b, 0.10))
            ctx.fillStyle = fill
            ctx.fill()

            // Antialiased upper and lower edges.
            ctx.strokeStyle = root.waveColor
            ctx.lineWidth = 1.2
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(points[0].x, centre - points[0].y)
            for (let i = 1; i < count; ++i) {
                ctx.lineTo(points[i].x, centre - points[i].y)
            }
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(points[0].x, centre + points[0].y)
            for (let i = 1; i < count; ++i) {
                ctx.lineTo(points[i].x, centre + points[i].y)
            }
            ctx.stroke()

            if (root.showEnvelope) {
                const gain = Math.pow(10, root.gainDb / 20)
                const fadeIn = Math.min(0.49, Math.max(0, root.fadeInRatio))
                const fadeOut = Math.min(0.49, Math.max(0, root.fadeOutRatio))
                ctx.strokeStyle = root.envelopeColor
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(0, centre - (height / 2 - 2) * gain * (fadeIn > 0 ? 0 : 1))
                if (fadeIn > 0) {
                    ctx.lineTo(width * fadeIn, centre - (height / 2 - 2) * gain)
                } else {
                    ctx.lineTo(0, centre - (height / 2 - 2) * gain)
                }
                if (fadeOut > 0) {
                    ctx.lineTo(width * (1 - fadeOut), centre - (height / 2 - 2) * gain)
                    ctx.lineTo(width, centre)
                } else {
                    ctx.lineTo(width, centre - (height / 2 - 2) * gain)
                }
                ctx.stroke()
            }

            // Playhead shade for the part of the take still ahead of transport.
            const ratio = root.playheadRatio
            if (ratio >= 0 && ratio <= 1) {
                const x = ratio * width
                ctx.fillStyle = Qt.rgba(0.02, 0.03, 0.04, 0.42)
                ctx.fillRect(x, 0, Math.max(0, width - x), height)
            }
        }
    }
}
