import QtQuick
import Mnkybox 1.0

// Peak display for a take: the EDIT popup's big waveform, the recorder row's
// 44 px mini strip and the region preview.
//
// Two data feeds share one renderer:
//  - `peaks`: the high-resolution sidecar reduction as flat pairs
//    [lo0, hi0, lo1, hi1, ...] (magnitudes 0..1, EDIT popup). The envelope is
//    drawn from real per-column min/max, with a mirrored symmetric fill, the
//    antialiased upper/lower edges and a peak-hold top line.
//  - `values`: coarse magnitudes 0..1 (mini strips, preview).
// An empty feed draws the explicit placeholder block pattern, never a blocking
// scan; the popup puts its SCANNING badge above this item.
//
// `before`/`after` envelopes are the live preview: the stored region
// shape (dim, dashed) and the drafted shape (accent, solid) over the region
// span, so an op preview is a comparison.
Item {
    id: root

    property var values: []
    property var peaks: []
    // Envelope span inside the waveform, as ratios of the drawn width.
    property real envelopeStartRatio: 0
    property real envelopeSpanRatio: 1
    property bool showEnvelope: false
    property real gainDb: 0
    // Fade ramps as fractions of the envelope span (0..0.49 each).
    property real fadeInRatio: 0
    property real fadeOutRatio: 0
    property bool showGhostEnvelope: false
    property real ghostGainDb: 0
    property real ghostFadeInRatio: 0
    property real ghostFadeOutRatio: 0
    property real playheadRatio: -1
    property color waveColor: Style.accent
    property color envelopeColor: Style.textDim
    property string placeholderText: "NO PEAKS"

    onValuesChanged: canvas.requestPaint()
    onPeaksChanged: canvas.requestPaint()
    onShowEnvelopeChanged: canvas.requestPaint()
    onGainDbChanged: canvas.requestPaint()
    onFadeInRatioChanged: canvas.requestPaint()
    onFadeOutRatioChanged: canvas.requestPaint()
    onShowGhostEnvelopeChanged: canvas.requestPaint()
    onGhostGainDbChanged: canvas.requestPaint()
    onGhostFadeInRatioChanged: canvas.requestPaint()
    onGhostFadeOutRatioChanged: canvas.requestPaint()
    onEnvelopeStartRatioChanged: canvas.requestPaint()
    onEnvelopeSpanRatioChanged: canvas.requestPaint()
    onPlayheadRatioChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onVisibleChanged: canvas.requestPaint()
    // A delegate can be created (and painted) before its layout and model role
    // settle; repaint once both are real, so a list row never keeps a 0 x 0
    // placeholder paint.
    Component.onCompleted: canvas.requestPaint()

    readonly property bool hasPeaks: root.peaks !== undefined && root.peaks !== null &&
                                     root.peaks.length >= 2
    readonly property bool hasValues: root.values !== undefined && root.values !== null &&
                                      root.values.length > 0

    // The placeholder text is a Text item, not Canvas fillText: a Text elides
    // cleanly and does not leave a ghost when the width settles after the first
    // paint.
    Text {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        visible: !root.hasPeaks && !root.hasValues
        text: root.placeholderText
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        // Symmetric amplitude per x position, one entry per drawn column.
        function columnAmplitudes() {
            const amps = []
            if (root.hasPeaks) {
                const pairs = Math.floor(root.peaks.length / 2)
                for (let i = 0; i < pairs; ++i) {
                    const lo = Math.min(1, Math.max(0, Number(root.peaks[i * 2])))
                    const hi = Math.min(1, Math.max(0, Number(root.peaks[i * 2 + 1])))
                    amps.push(Math.max(lo, hi))
                }
                return amps
            }
            if (root.hasValues) {
                for (let i = 0; i < root.values.length; ++i) {
                    amps.push(Math.min(1, Math.max(0, Number(root.values[i]))))
                }
            }
            return amps
        }

        function drawEnvelope(ctx, gain, fadeIn, fadeOut, color, width, dashed) {
            const span = Math.max(0, Math.min(1, root.envelopeSpanRatio))
            const start = Math.max(0, Math.min(1, root.envelopeStartRatio))
            const x0 = start * width
            const x1 = Math.min(width, (start + span) * width)
            if (x1 <= x0) return
            const centre = height / 2
            const scale = Math.pow(10, gain / 20)
            const peak = (height / 2 - 2) * scale
            const inRatio = Math.min(0.49, Math.max(0, fadeIn))
            const outRatio = Math.min(0.49, Math.max(0, fadeOut))
            ctx.strokeStyle = color
            // 1 px dim = BEFORE (stored shape), 2 px accent = AFTER (draft).
            ctx.lineWidth = dashed ? 1 : 2
            ctx.lineJoin = "round"
            ctx.beginPath()
            const xIn = x0 + (x1 - x0) * inRatio
            const xOut = x1 - (x1 - x0) * outRatio
            ctx.moveTo(x0, inRatio > 0 ? centre : centre - peak)
            if (inRatio > 0) {
                ctx.lineTo(xIn, centre - peak)
            } else {
                ctx.lineTo(x0, centre - peak)
            }
            ctx.lineTo(Math.max(xIn, xOut), centre - peak)
            if (outRatio > 0) {
                ctx.lineTo(x1, centre)
            } else {
                ctx.lineTo(x1, centre - peak)
            }
            ctx.stroke()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const centre = height / 2
            const amps = columnAmplitudes()
            if (amps.length === 0) {
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
            const count = amps.length
            const step = count > 1 ? width / (count - 1) : width
            const bodyHeight = height / 2 - 2
            const points = []
            for (let i = 0; i < count; ++i) {
                points.push({x: i * step, y: Math.max(0.5, amps[i] * bodyHeight)})
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

            // Peak-hold top line: the highest amplitude in a 3-column window,
            // one pixel above the body, so a transient stays readable when the
            // column reduction has already averaged it.
            if (root.hasPeaks) {
                ctx.strokeStyle = Qt.rgba(root.waveColor.r, root.waveColor.g,
                                          root.waveColor.b, 0.75)
                ctx.lineWidth = 1
                ctx.beginPath()
                for (let i = 0; i < count; ++i) {
                    let hold = 0
                    for (let k = Math.max(0, i - 1); k <= Math.min(count - 1, i + 1); ++k) {
                        hold = Math.max(hold, amps[k])
                    }
                    const y = centre - Math.min(centre - 1, hold * bodyHeight + 1)
                    if (i === 0) ctx.moveTo(points[i].x, y)
                    else ctx.lineTo(points[i].x, y)
                }
                ctx.stroke()
            }

            if (root.showGhostEnvelope) {
                drawEnvelope(ctx, root.ghostGainDb, root.ghostFadeInRatio,
                             root.ghostFadeOutRatio,
                             Qt.rgba(root.envelopeColor.r, root.envelopeColor.g,
                                     root.envelopeColor.b, 0.55), width, true)
            }
            if (root.showEnvelope) {
                drawEnvelope(ctx, root.gainDb, root.fadeInRatio,
                             root.fadeOutRatio, root.waveColor, width, false)
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
