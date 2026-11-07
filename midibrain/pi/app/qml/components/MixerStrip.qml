import QtQuick
import Mnkybox 1.0

// One mixer strip of the dynamic three-per-page performance bank: the track
// name (tap selects, 600 ms hold renames), a separate CH control that opens the
// full channel popout, a tall 64 px fader beside a 30 px meter, a readable gain
// readout and the three large M/S/ARM switches. Pan, input assignment, sends
// and the mode selector live in the channel popout; the strip keeps only the
// exceptional mode state (OVERDUB/LIVE) as a legible status pill. Taps on the
// name, the CH control and the fader are three different gestures: none of them
// opens the channel popout by accident while a finger rides the fader.
Rectangle {
    id: root

    // Slot index (0..17) of this strip, not its row: the model is dynamic and
    // a track keeps its slot when another track before it is removed.
    property int stripIndex: 0
    property string stripName: ""
    property real gainDb: 0.0
    property real level: 0.0
    property bool clip: false
    // Track mode (docs/daw-plan.md W1): 0 PLAY, 1 ARM, 2 OVERDUB, 3 LIVE, and
    // the word the pill shows. The full selector lives in the channel popout.
    property int mode: 1
    property string modeWord: "ARM"
    property bool mute: false
    property bool solo: false
    property bool arm: false
    property bool selected: false
    readonly property bool focused: app.focusId === ("mixer.select." + stripIndex)
    readonly property bool exceptionalMode: mode === 2 || mode === 3
    readonly property string nameText: stripName !== "" ? stripName
                                                        : "IN " + String(stripIndex + 1).padStart(2, "0")

    color: Style.panel
    radius: Style.corner
    border.width: (selected || focused) ? 2 : Style.outlineWidth
    border.color: (selected || focused) ? Style.accent : Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 3
        spacing: Style.gapTight
        clip: true

        // Name = select/rename; CH = channel details. Two separate targets so
        // a strip tap never opens the popout.
        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            Text {
                id: nameLabel
                width: parent.width - 56 - Style.gapTight
                height: parent.height
                text: root.nameText
                color: Style.text
                font.pixelSize: Style.fontLabel
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                // Panel stop for selecting the strip; the touch TapHandler
                // below is the same gesture.
                property string focusId: "mixer.select." + root.stripIndex

                TapHandler {
                    onTapped: app.selectStrip(root.stripIndex)
                    onLongPressed: app.requestRenameTrack(root.stripIndex)
                }
            }
            TouchButton {
                width: 56
                height: parent.height
                text: "CH"
                textSize: Style.fontSecondary
                chevron: "up"
                variant: "ghost"
                focusId: "mixer.expand." + root.stripIndex
                onClicked: app.openChannel(root.stripIndex)
                // B2: the chevron states its payoff without stealing the tap.
                onLongClicked: app.showTextPopup(
                    "CHANNEL", "fader, pan, sends, monitor, I/O, insert, EQ")
            }
        }

        // Readable gain value plus, only when it matters, the track mode as a
        // legible pill instead of a 9 px mode selector.
        Row {
            width: parent.width
            height: 30
            spacing: Style.gapTight

            Text {
                width: parent.width - (modePill.visible ? modePill.width + Style.gapTight
                                                        : 0)
                height: parent.height
                text: Number(root.gainDb).toFixed(1) + " dB"
                color: Style.text
                font.pixelSize: Style.fontValue
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Rectangle {
                id: modePill
                visible: root.exceptionalMode
                width: Math.min(parent.width * 0.5, modeText.implicitWidth + 16)
                height: 24
                anchors.verticalCenter: parent.verticalCenter
                radius: 12
                color: Style.panelDark
                border.width: 1
                border.color: root.mode === 2 ? Style.accent : Style.ok

                Text {
                    id: modeText
                    anchors.centerIn: parent
                    text: root.modeWord
                    color: root.mode === 2 ? Style.accent : Style.ok
                    font.pixelSize: Style.fontSecondary
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Tall fader + a real meter, both 64/30 px wide so a finger can ride
        // the fader without reaching any other control.
        Row {
            width: Style.touchPrimary + Style.gap + 30
            height: parent.height - Style.touch - 30 - Style.touchSecondary -
                    Style.gapTight * 3
            x: (parent.width - width) / 2
            spacing: Style.gap

            TouchSlider {
                id: gain
                width: Style.touchPrimary
                height: parent.height
                vertical: true
                minimumValue: -60
                maximumValue: 12
                value: root.gainDb
                focusId: "mixer.gain." + root.stripIndex
                popupLabel: root.nameText + " GAIN"
                popupKind: "db"
                Binding on value { value: root.gainDb; when: !gain.dragging }
                onMoved: (value) => mixer.setGainDb(root.stripIndex, value)
            }

            // Per-strip post-gain meter with a latched clip crown. The shared
            // component owns the dB taper, the 10 ms peak hold and the crown.
            Item {
                id: meter
                width: 30
                height: parent.height

                Meter {
                    anchors.fill: parent
                    level: root.level
                    clip: root.clip
                    vertical: true
                }
                // B7: the crown is a control. A tap drops the latch and says
                // so; without a latch the same tap explains the crown.
                TapHandler {
                    onTapped: {
                        if (root.clip) {
                            mixer.clearClip(root.stripIndex)
                            app.showTextPopup("CLIP", "CLIP CLEARED")
                        } else {
                            app.showTextPopup("METER",
                                "the crown latches when the strip hits full scale - tap it to clear")
                        }
                    }
                }
            }
        }

        // Large switches: MUTE gray, SOLO green, ARM record red.
        Row {
            width: parent.width
            height: Style.touchSecondary
            spacing: Style.gapTight

            TouchButton {
                width: (parent.width - Style.gapTight * 2) / 3
                height: parent.height
                text: "MUTE"
                textSize: Style.fontSecondary
                checked: root.mute
                checkedColor: Style.textDim
                checkedTextColor: Style.text
                focusId: "mixer.mute." + root.stripIndex
                onClicked: mixer.toggleMute(root.stripIndex)
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 2) / 3
                height: parent.height
                text: "SOLO"
                textSize: Style.fontSecondary
                checked: root.solo
                checkedColor: Style.ok
                checkedTextColor: Style.panelDark
                focusId: "mixer.solo." + root.stripIndex
                onClicked: mixer.toggleSolo(root.stripIndex)
            }
            TouchButton {
                width: (parent.width - Style.gapTight * 2) / 3
                height: parent.height
                text: "ARM"
                textSize: Style.fontSecondary
                checked: root.arm
                checkedColor: Style.record
                focusId: "mixer.arm." + root.stripIndex
                onClicked: mixer.toggleArm(root.stripIndex)
            }
        }
    }
}
