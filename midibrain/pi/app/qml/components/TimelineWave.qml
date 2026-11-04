import QtQuick
import Mnkybox 1.0

// Peak overview for one take lane or the region-ops preview. `values` is the
// reduced `.peaks` overview (0..1); an empty list draws the explicit placeholder
// block pattern, never a blocking scan. The optional envelope draws the draft
// gain and linear fade ramps used by the region-ops popup.
Item {
    id: root

    property var values: []
    property bool showEnvelope: false
    property real gainDb: 0
    property real fadeInRatio: 0
    property real fadeOutRatio: 0
    property color waveColor: Style.accent
    property color envelopeColor: Style.soloOn
    property string placeholderText: "NO PEAKS"

    onValuesChanged: canvas.requestPaint()
    onShowEnvelopeChanged: canvas.requestPaint()
    onGainDbChanged: canvas.requestPaint()
    onFadeInRatioChanged: canvas.requestPaint()
    onFadeOutRatioChanged: canvas.requestPaint()
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
        font.pixelSize: 10
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
            const step = width / root.values.length
            ctx.fillStyle = root.waveColor
            for (let i = 0; i < root.values.length; ++i) {
                const amp = Math.min(1, Math.max(0, Number(root.values[i])))
                const h = Math.max(1, amp * (height - 4))
                ctx.fillRect(i * step, centre - h / 2, Math.max(1, step * 0.6), h)
            }
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
        }
    }
}
