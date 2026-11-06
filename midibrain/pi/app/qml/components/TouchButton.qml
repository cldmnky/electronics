import QtQuick
import Mnkybox 1.0

// Touch-first button with the three-level hierarchy from the refresh spec:
// primary (filled cyan), secondary (outlined), ghost (text only), plus danger
// (red outline/fill, record and destructive actions only). `checked` is bound by
// the screen to model state; a tap emits clicked() and the screen performs the
// state change, so the visual never lies about a failed write. The focus ring is
// the panel NAV focus, not a hover/keyboard highlight.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    property bool active: true
    property color checkedColor: Style.accent
    property color checkedTextColor: Style.panelDark
    property string focusId: ""
    property int textSize: Style.fontBody
    property bool bold: true
    // "secondary" | "primary" | "ghost" | "danger"
    property string variant: "secondary"
    // Selector affordance: "" | "down" | "up". A popup-opening button draws a
    // chevron (down = opens a list, up = expands the channel) so a tap-to-open
    // selector is visibly different from a plain toggle. With no text the
    // chevron is the whole label (a 44 px icon button).
    property string chevron: ""

    signal clicked()
    // Long press (the tab row's SHIFT gesture): the screen decides what the
    // second action means, so a control only carries the signal.
    signal longClicked()

    readonly property bool focused: app.focusId === focusId && focusId !== ""
    readonly property color idleFill: variant === "ghost" ? "transparent"
                                  : (variant === "danger" ? Style.dangerFill
                                     : (variant === "primary" ? Style.accent
                                                              : Style.panel))
    readonly property color fill: !active ? Style.panelDark
                                   : (tap.pressed ? Qt.lighter(root.checked ? root.checkedColor
                                                                          : root.idleFill, 1.08)
                                      : (root.checked ? root.checkedColor : root.idleFill))
    readonly property color labelColor: !active ? Style.textDim
                                        : (root.checked || variant === "primary"
                                           ? root.checkedTextColor
                                           : (variant === "danger" ? Style.record
                                              : (variant === "ghost" ? (root.checked ? Style.accent
                                                                                    : Style.textDim)
                                                                   : Style.text)))

    implicitWidth: Math.max(Style.touch, label.implicitWidth + 16)
    implicitHeight: Style.touch
    radius: Style.corner
    color: root.fill
    border.width: root.focused ? 2
                 : (root.variant === "ghost" ? 0
                    : (root.variant === "danger" ? 1
                       : (root.variant === "secondary" ? Style.outlineWidth : 0)))
    border.color: root.focused ? Style.accent
                 : (root.variant === "danger" ? Style.record
                    : (root.variant === "secondary" ? Style.outline : "transparent"))
    opacity: active ? 1.0 : 0.55

    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 8 - (root.chevron !== "" && root.text !== "" ? 14 : 0)
        anchors.horizontalCenterOffset: root.chevron !== "" && root.text !== "" ? -7 : 0
        text: root.text
        color: root.labelColor
        font.pixelSize: root.textSize
        font.weight: root.bold ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    Canvas {
        id: chevronCanvas
        visible: root.chevron !== ""
        width: root.text === "" ? 16 : 12
        height: 8
        anchors.verticalCenter: parent.verticalCenter
        x: root.text === "" ? (parent.width - width) / 2 : parent.width - width - 4
        Component.onCompleted: requestPaint()
        onVisibleChanged: requestPaint()
        Connections {
            target: root
            function onChevronChanged() { chevronCanvas.requestPaint() }
            function onCheckedChanged() { chevronCanvas.requestPaint() }
            function onVariantChanged() { chevronCanvas.requestPaint() }
            function onTextChanged() { chevronCanvas.requestPaint() }
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = root.labelColor
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const down = root.chevron === "down"
            ctx.beginPath()
            ctx.moveTo(1, down ? 2 : 6)
            ctx.lineTo(width / 2, down ? 6 : 2)
            ctx.lineTo(width - 1, down ? 2 : 6)
            ctx.stroke()
        }
    }

    TapHandler {
        id: tap
        enabled: root.active
        // A long press fires longClicked() at the A2 600 ms threshold and the
        // release that follows must not also fire clicked(); the flag resets
        // when the next press starts. The threshold is our own Timer because
        // TapHandler's longPressed follows the platform press-and-hold (800 ms
        // default) - past both the spec and the tap gate's 700 ms hold, so the
        // long-press gesture never fired and the release read as a click.
        property bool heldLong: false
        onPressedChanged: {
            if (pressed) {
                heldLong = false
                longPressTimer.restart()
            } else {
                longPressTimer.stop()
            }
        }
        onTapped: if (!heldLong) root.clicked()
    }

    Timer {
        id: longPressTimer
        interval: 600
        onTriggered: {
            tap.heldLong = true
            root.longClicked()
        }
    }
}
