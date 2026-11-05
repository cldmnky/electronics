import QtQuick
import Mnkybox 1.0

// Touch-first slider: a 44 px hit area around a 5 px track and a 22 px thumb.
// `dragging` lets a screen bind model values without fighting the finger. The
// focus ring is the panel NAV focus.
Item {
    id: root

    property real value: 0.0
    property real minimumValue: 0.0
    property real maximumValue: 1.0
    property bool vertical: false
    property bool active: true
    property bool dragging: false
    property string focusId: ""
    property string popupLabel: ""
    property string popupKind: "generic"

    signal moved(real value)

    implicitWidth: vertical ? Style.touch : 140
    implicitHeight: vertical ? 100 : Style.touch
    opacity: active ? 1.0 : 0.45

    readonly property real fraction: maximumValue > minimumValue
                                     ? Math.max(0, Math.min(1,
                                           (value - minimumValue) / (maximumValue - minimumValue)))
                                     : 0.0

    Rectangle {
        id: focusRing
        anchors.fill: parent
        color: "transparent"
        radius: Style.corner
        border.width: (app.focusId === focusId && focusId !== "") ? 2 : 0
        border.color: Style.accent
    }

    Rectangle {
        id: track
        color: Style.outline
        radius: Style.corner
        anchors.centerIn: parent
        width: root.vertical ? Style.track : parent.width - Style.thumb
        height: root.vertical ? parent.height - Style.thumb : Style.track
    }

    Rectangle {
        id: fill
        color: root.active ? Style.accent : Style.textDim
        radius: Style.corner
        anchors.left: root.vertical ? undefined : track.left
        anchors.bottom: root.vertical ? track.bottom : undefined
        anchors.horizontalCenter: root.vertical ? track.horizontalCenter : undefined
        anchors.verticalCenter: root.vertical ? undefined : track.verticalCenter
        width: root.vertical ? Style.track : track.width * root.fraction
        height: root.vertical ? track.height * root.fraction : Style.track
    }

    Rectangle {
        id: thumb
        width: Style.thumb
        height: Style.thumb
        radius: width / 2
        color: root.active ? Style.text : Style.textDim
        border.color: Style.panelDark
        border.width: 2
        x: root.vertical ? (track.x + track.width / 2 - width / 2)
                         : (track.x + root.fraction * track.width - width / 2)
        y: root.vertical ? (track.y + track.height - root.fraction * track.height - height / 2)
                         : (track.y + track.height / 2 - height / 2)
    }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: root.active
        preventStealing: true
        onPressed: (mouse) => {
            root.dragging = true
            root.trackFinger(mouse.x, mouse.y)
            root.applyPointer(mouse.x, mouse.y)
        }
        onPositionChanged: (mouse) => {
            if (pressed) {
                root.trackFinger(mouse.x, mouse.y)
                root.applyPointer(mouse.x, mouse.y)
            }
        }
        onReleased: {
            root.dragging = false
            app.touchAnchorY = -1
        }
        onCanceled: {
            root.dragging = false
            app.touchAnchorY = -1
        }
    }

    // The readout avoids the finger: hand its window-Y to the popup while the
    // drag lasts and clear it on release, so the panel path keeps the top band.
    function trackFinger(x, y) {
        app.touchAnchorY = mapToItem(null, x, y).y
    }

    function applyPointer(x, y) {
        const span = maximumValue - minimumValue
        if (span <= 0) return
        let next
        if (vertical) {
            next = minimumValue + (1.0 - Math.max(0, Math.min(1, y / height))) * span
        } else {
            next = minimumValue + Math.max(0, Math.min(1, x / width)) * span
        }
        // Pan centre detent: a 2 % snap makes a centred pan reachable by touch
        // and the popup then confirms "Center".
        if (popupKind === "pan" && Math.abs(next - 0.5) <= 0.02) {
            next = 0.5
        }
        value = next
        if (popupLabel !== "" || focusId !== "") {
            app.showValuePopup(popupLabel !== "" ? popupLabel : focusId, popupKind, next)
        }
        moved(next)
    }
}
