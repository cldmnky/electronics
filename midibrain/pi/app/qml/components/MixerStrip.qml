import QtQuick
import Mnkybox 1.0

// One banked mixer strip: gain fader, pan, input source and the three track
// switches. The outer border is the selected-strip indicator; every control
// carries a panel focus id. The meter is the shared gradient/peak-hold/clip
// component so the primary face stays perform-only.
Rectangle {
    id: root

    property int stripIndex: 0
    property real gainDb: 0.0
    property real pan: 0.5
    property string panText: "C"
    property real sendA: 0.0
    property real sendB: 0.0
    property real level: 0.0
    property bool clip: false
    property int monitor: 1
    property string monitorMark: ""
    property bool mute: false
    property bool solo: false
    property bool arm: false
    property bool selected: false
    // Current input source of this strip: "NONE", "IN 03" or a stereo pair
    // "IN 03/04" (session inCh/inPair through MixerViewModel).
    property string inputText: ""
    readonly property bool focused: app.focusId === ("mixer.select." + stripIndex)
    readonly property string stripName: "IN " + String(stripIndex + 1).padStart(2, "0")

    color: Style.panel
    radius: Style.corner
    border.width: (selected || focused) ? 2 : Style.outlineWidth
    border.color: (selected || focused) ? Style.accent : Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 0
        clip: true

        Item {
            width: parent.width
            height: 14
            Text {
                anchors.left: parent.left
                text: root.stripName
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                text: root.panText
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
        }

        Row {
            width: parent.width
            height: Math.max(Style.touch, root.height - 14 - 5 * Style.touch)
            spacing: 2

            TouchSlider {
                id: gain
                width: parent.width - 18
                height: parent.height
                vertical: true
                minimumValue: -60
                maximumValue: 12
                value: root.gainDb
                focusId: "mixer.gain." + root.stripIndex
                popupLabel: root.stripName + " GAIN"
                popupKind: "db"
                Binding on value { value: root.gainDb; when: !gain.dragging }
                onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
            }

            // Per-strip post-gain meter with a latched clip crown. The shared
            // component owns the dB taper, the 10 ms peak hold and the crown.
            Item {
                id: meter
                width: 16
                height: parent.height

                Meter {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: mark.top
                    level: root.level
                    clip: root.clip
                    vertical: true
                }
                // Per-track input monitoring: PLAY/BOTH/MON always, AUTO only
                // when an active take makes Auto choose the take over live input.
                Text {
                    id: mark
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 10
                    text: root.monitorMark.charAt(0)
                    color: root.monitor === 1 ? Style.textDim : Style.accent
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        TouchSlider {
            id: panSlider
            width: parent.width
            height: Style.touch
            minimumValue: 0
            maximumValue: 1
            value: root.pan
            focusId: "mixer.pan." + root.stripIndex
            popupLabel: root.stripName + " PAN"
            popupKind: "pan"
            Binding on value { value: root.pan; when: !panSlider.dragging }
            onMoved: (value) => mixer.setPan(root.stripIndex, value)
        }

        // Compact input source: NONE, mono IN 01..18 or a stereo pair
        // IN 01/02..17/18. Tap steps forward, long press switches MONO/PAIR and
        // the value readout confirms both, so a strip's source no longer needs
        // the TRACK SHIFT face. Panel: NAV_PRESS enters EDIT and the encoder
        // steps the same list (focus mixer.in.<strip>).
        TouchButton {
            width: parent.width
            height: Style.touch
            text: root.inputText
            textSize: Style.fontSmall
            variant: "ghost"
            focusId: "mixer.in." + root.stripIndex
            onClicked: app.showTextPopup(root.stripName + " INPUT",
                                         mixer.cycleInSource(root.stripIndex, 1))
            onLongClicked: app.showTextPopup(root.stripName + " INPUT",
                                             mixer.toggleInPair(root.stripIndex))
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "M"
            checked: root.mute
            focusId: "mixer.mute." + root.stripIndex
            onClicked: mixer.toggleMute(root.stripIndex)
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "S"
            checked: root.solo
            focusId: "mixer.solo." + root.stripIndex
            onClicked: mixer.toggleSolo(root.stripIndex)
        }

        TouchButton {
            width: parent.width
            height: Style.touch
            text: "ARM"
            textSize: Style.fontSmall
            checked: root.arm
            checkedColor: Style.record
            focusId: "mixer.arm." + root.stripIndex
            onClicked: mixer.toggleArm(root.stripIndex)
        }
    }

    TapHandler {
        onTapped: app.selectStrip(root.stripIndex)
    }
}
