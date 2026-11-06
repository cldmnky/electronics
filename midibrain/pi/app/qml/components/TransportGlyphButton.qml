import QtQuick
import Mnkybox 1.0

// Drawn transport glyph (play triangle / stop square / record circle), never a
// font glyph, with the panel NAV focus ring. REC pulses while the transport is
// actually recording, so the touch strip shows live state instead of a label.
Item {
    id: root

    property string glyph: "play"       // "play" | "stop" | "rec"
    property bool checked: false
    property bool recording: false
    property bool active: true
    property string focusId: ""

    signal clicked()

    implicitWidth: Style.touch
    implicitHeight: Style.touch
    opacity: active ? 1.0 : 0.5

    readonly property bool focused: app.focusId === focusId && focusId !== ""
    readonly property color tone: !active ? Style.textDim
                                 : (checked ? (glyph === "rec" ? Style.record : Style.accent)
                                            : Style.text)

    property real pulse: 1.0

    onRecordingChanged: if (!root.recording) pulse = 1.0

    SequentialAnimation on pulse {
        running: root.glyph === "rec" && root.recording
        loops: Animation.Infinite
        NumberAnimation { from: 1.0; to: 0.45; duration: 420; easing.type: Easing.InOutSine }
        NumberAnimation { from: 0.45; to: 1.0; duration: 420; easing.type: Easing.InOutSine }
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.corner
        color: tap.pressed ? Qt.lighter(Style.raised, 1.08)
                           : (root.checked ? Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.16)
                                           : Style.panel)
        border.width: root.focused ? 2 : Style.outlineWidth
        border.color: root.focused ? Style.accent : Style.outline
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 10
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = root.tone.toString()
            if (root.glyph === "play") {
                ctx.beginPath()
                ctx.moveTo(1, 1)
                ctx.lineTo(width - 1, height / 2)
                ctx.lineTo(1, height - 1)
                ctx.closePath()
                ctx.fill()
            } else if (root.glyph === "stop") {
                ctx.fillRect(1, 1, width - 2, height - 2)
            } else {
                const radius = Math.min(width, height) / 2 - 1
                if (radius > 0) {
                    ctx.globalAlpha = root.pulse
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, radius, 0, Math.PI * 2)
                    ctx.fill()
                    ctx.globalAlpha = 1.0
                }
            }
        }
    }

    onToneChanged: canvas.requestPaint()
    onPulseChanged: canvas.requestPaint()
    onGlyphChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    TapHandler {
        id: tap
        enabled: root.active
        onTapped: root.clicked()
    }
}
