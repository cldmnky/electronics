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
        width: parent.width - 8
        text: root.text
        color: root.labelColor
        font.pixelSize: root.textSize
        font.weight: root.bold ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    TapHandler {
        id: tap
        enabled: root.active
        // A long press fires longClicked() and the release that follows must not
        // also fire clicked(); the flag is reset when the next press starts.
        property bool heldLong: false
        onPressedChanged: if (pressed) heldLong = false
        onTapped: if (!heldLong) root.clicked()
        onLongPressed: {
            heldLong = true
            root.longClicked()
        }
    }
}
