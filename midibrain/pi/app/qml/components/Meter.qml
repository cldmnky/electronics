import QtQuick
import Mnkybox 1.0

// Meter renderer: 3 dB-tapered fill (the engine maps -48 dB..0 dB onto the
// rail), a 10 ms peak-hold dot and a latched clip crown. The model owns the
// latch (`clip` stays true until the mixer clears it); this component only
// draws it. Level is the post-gain linear peak, so the taper is computed here
// exactly like the master meter did before the refresh.
Item {
    id: root

    property real level: 0.0
    property bool clip: false
    property bool vertical: true
    property bool showPeak: true

    readonly property real fraction: {
        if (!(level > 0)) return 0
        const db = 20 * Math.log(level) / Math.LN10
        if (db <= -48) return 0
        return Math.min(1, (db + 48) / 48)
    }

    property real hold: 0.0
    property int holdAgeMs: 0

    onLevelChanged: {
        if (fraction >= hold) {
            hold = fraction
            holdAgeMs = 0
        }
    }

    Timer {
        // 10 ms tick: hold the peak for 500 ms, then decay it slowly.
        interval: 10
        repeat: true
        running: root.visible && root.hold > 0
        onTriggered: {
            root.holdAgeMs += 10
            if (root.holdAgeMs > 500) {
                root.hold = Math.max(root.fraction, root.hold - 0.008)
            }
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Style.panelDark.toString()
            ctx.fillRect(0, 0, width, height)

            const inset = 1
            const innerW = Math.max(1, width - inset * 2)
            const innerH = Math.max(1, height - inset * 2)
            const fill = root.vertical ? innerH * root.fraction : innerW * root.fraction
            const gradient = root.vertical
                  ? ctx.createLinearGradient(0, height - inset, 0, inset)
                  : ctx.createLinearGradient(inset, 0, width - inset, 0)
            gradient.addColorStop(0.0, "#1e6f85")
            gradient.addColorStop(0.55, "#2fa7cc")
            gradient.addColorStop(1.0, Style.accent.toString())
            ctx.fillStyle = gradient
            if (root.vertical) {
                ctx.fillRect(inset, height - inset - fill, innerW, fill)
            } else {
                ctx.fillRect(inset, inset, fill, innerH)
            }

            // Hairline rail, drawn last so the fill never hides the scale edge.
            ctx.strokeStyle = Style.outline.toString()
            ctx.lineWidth = 1
            ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
        }
    }

    // Peak-hold dot: a plain Rectangle binding (10 ms hold timer above), so the
    // Canvas only repaints when the 15 Hz level changes.
    Rectangle {
        visible: root.showPeak && root.hold > 0.01
        width: root.vertical ? Math.max(1, parent.width - 2) : 2
        height: root.vertical ? 2 : Math.max(1, parent.height - 2)
        x: root.vertical ? 1
                         : Math.min(parent.width - 3,
                                    1 + (parent.width - 2) * root.hold)
        y: root.vertical ? Math.max(1,
                                    parent.height - 1 -
                                    (parent.height - 2) * root.hold)
                         : 1
        color: Style.text
    }

    // Clip crown latches at the output end of the rail.
    Rectangle {
        visible: root.clip
        width: root.vertical ? Math.max(1, parent.width - 2)
                             : Math.min(6, Math.max(1, parent.width - 2))
        height: root.vertical ? Math.min(6, Math.max(1, parent.height - 2))
                              : Math.max(1, parent.height - 2)
        x: root.vertical ? 1 : parent.width - width - 1
        y: root.vertical ? 1 : 1
        radius: 2
        color: Style.record
    }

    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onFractionChanged: canvas.requestPaint()
}
