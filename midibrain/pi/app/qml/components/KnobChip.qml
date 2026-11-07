import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// A value in a tight cell: a framed arc dial with its label and value. This is
// the refresh law's replacement for a slider outside a dedicated wide fader
// (mixer strip, TRACK/master fader): drag the chip, turn the panel encoder or
// step it with the buttons the screen puts beside it.
//
// `stepSize` picks the drag behaviour: 0 maps the drag over the range like a
// dial, a positive value nudges the value in fixed steps (a beat/measure field
// keeps its precision and needs no upper bound), and `dialScale` is the value
// the arc shows as full scale.
Rectangle {
    id: root

    property string label: ""
    property real value: 0.0
    property real minimumValue: 0.0
    // 0 = no upper clamp (the model owns the limit for a stepped field).
    property real maximumValue: 0.0
    // Full-scale value of the arc; 0 means "use maximumValue".
    property real dialScale: 0.0
    property real stepSize: 0.0
    property string valueText: ""
    property string suffix: ""
    property int decimals: 1
    property string popupLabel: ""
    property string popupKind: "generic"
    property string focusId: ""
    property bool active: true
    property bool dimmed: false

    signal moved(real value)

    readonly property real fullScale: dialScale > 0 ? dialScale
                                    : (maximumValue > minimumValue ? maximumValue : 1.0)
    readonly property real fraction: fullScale > 0
                                     ? Math.max(0, Math.min(1, value / fullScale)) : 0.0
    readonly property bool focused: app.focusId === focusId && focusId !== ""
    // A9: outlined while a learn target capture waits for a touch.
    readonly property bool learnOutline: app.learnArmed && app.isLearnableFocus(focusId)

    implicitHeight: Style.touch
    radius: Style.corner
    color: Style.panelDark
    border.width: root.focused || root.learnOutline ? 2 : Style.outlineWidth
    border.color: root.focused || root.learnOutline ? Style.accent : Style.outline
    opacity: active ? 1.0 : 0.55

    KnobDial {
        id: dial
        width: Math.max(24, Math.min(36, parent.height - 8))
        height: width
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        fraction: root.fraction
        dimmed: root.dimmed || !root.active
    }

    Column {
        anchors.left: dial.right
        anchors.leftMargin: 6
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Text {
            width: parent.width
            height: 13
            text: root.label
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            height: 17
            text: (root.valueText !== "" ? root.valueText
                                         : Number(root.value).toFixed(root.decimals))
                  + root.suffix
            color: Style.text
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.active
        preventStealing: true

        property real pressY: 0
        property real pressX: 0
        property real pressValue: 0
        property bool pickOnly: false
        // Tap-vs-drag threshold: a finger that moves less than this many pixels
        // is a tap (open the precision sheet); anything more is a drag and
        // never opens it. The two gestures can never fight.
        property bool dragActive: false
        readonly property real tapThreshold: 10

        onPressed: (mouse) => {
            if (root.learnOutline) {
                pickOnly = true
                app.learnTargetTouched(root.focusId)
                return
            }
            pressX = mouse.x
            pressY = mouse.y
            pressValue = root.value
            dragActive = false
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
        }
        onPositionChanged: (mouse) => {
            if (pickOnly) return
            if (!pressed) return
            if (!dragActive &&
                Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) >
                tapThreshold) {
                dragActive = true
            }
            let next
            if (root.stepSize > 0) {
                next = pressValue + Math.round((pressY - mouse.y) / 24) * root.stepSize
            } else {
                const span = root.maximumValue - root.minimumValue
                if (span <= 0) return
                next = pressValue + (pressY - mouse.y) / 180 * span
            }
            next = Math.max(root.minimumValue, next)
            if (root.maximumValue > root.minimumValue) {
                next = Math.min(root.maximumValue, next)
            }
            app.touchAnchorY = mapToItem(null, mouse.x, mouse.y).y
            app.showValuePopup(root.popupLabel !== "" ? root.popupLabel : root.focusId,
                               root.popupKind, next)
            root.moved(next)
        }
        onReleased: {
            if (!pickOnly && !dragActive && root.active) precision.open()
            pickOnly = false
            app.touchAnchorY = -1
        }
        onCanceled: {
            pickOnly = false
            app.touchAnchorY = -1
        }
    }

    // Tap-to-edit precision sheet. The chip advertises "tap = edit precisely":
    // one tap opens it, drag still nudges the value in place, and the panel
    // encoder keeps driving the same value behind it. Outside a tap dismisses
    // it: nothing is committed here (the value is already live), so the cheap
    // escape is the right one. CLOSE is the explicit, large exit.
    Popup {
        id: precision
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 340
        height: 246
        modal: true
        dim: true
        closePolicy: Popup.CloseOnPressOutside

        onOpened: app.setKnobSheetOpen(true)
        onClosed: app.setKnobSheetOpen(false)

        // The controller takes the sheet down with the overlay it was opened
        // from (channel popout closed, tab jump, panel EXIT).
        Connections {
            target: app
            function onKnobSheetCloseRequested() {
                if (precision.opened) precision.close()
            }
        }

        background: Rectangle {
            color: Style.raised
            radius: Style.cornerPanel
            border.width: 2
            border.color: Style.accent
        }

        contentItem: Column {
            spacing: Style.gap
            padding: 12

            Text {
                width: parent.width - 24
                height: 24
                text: root.popupLabel !== "" ? root.popupLabel : root.label
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width - 24
                height: 40
                text: (root.valueText !== "" ? root.valueText
                                             : Number(root.value).toFixed(root.decimals))
                      + root.suffix
                color: Style.accent
                font.pixelSize: Style.fontValue
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Row {
                width: parent.width - 24
                height: Style.touchPrimary
                spacing: Style.gap

                TouchButton {
                    width: 96
                    height: parent.height
                    text: "-"
                    textSize: Style.fontLabel
                    onClicked: precision.step(-1)
                }
                TouchButton {
                    width: 96
                    height: parent.height
                    text: "+"
                    textSize: Style.fontLabel
                    onClicked: precision.step(1)
                }
                TouchButton {
                    width: parent.width - 96 - 96 - Style.gap * 2
                    height: parent.height
                    text: "CLOSE"
                    textSize: Style.fontLabel
                    variant: "primary"
                    onClicked: precision.close()
                }
            }
        }

        function step(direction) {
            let delta = root.stepSize > 0 ? root.stepSize
                                          : (root.maximumValue - root.minimumValue) / 100
            if (delta <= 0) delta = 1
            let next = root.value + direction * delta
            next = Math.max(root.minimumValue, next)
            if (root.maximumValue > root.minimumValue) {
                next = Math.min(root.maximumValue, next)
            }
            app.showValuePopup(root.popupLabel !== "" ? root.popupLabel : root.focusId,
                               root.popupKind, next)
            root.moved(next)
        }
    }
}
