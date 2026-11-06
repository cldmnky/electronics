import QtQuick
import Mnkybox 1.0

// One mixer strip of the dynamic four-per-page bank: the track name and expand
// affordance, a large gain fader with its meter, pan, the input-source selector
// and the three track switches. Every selector is a value button with a chevron
// that opens its option list (ConfigPopup); nothing steps through choices in
// place, and every change applies immediately. The outer border is the
// selected-strip indicator; every control carries a panel focus id.
Rectangle {
    id: root

    // Slot index (0..17) of this strip, not its row: the model is dynamic and
    // a track keeps its slot when another track before it is removed.
    property int stripIndex: 0
    property string stripName: ""
    property real gainDb: 0.0
    property real pan: 0.5
    property real level: 0.0
    property bool clip: false
    // Track mode (docs/daw-plan.md W1): 0 PLAY, 1 ARM, 2 OVERDUB, 3 LIVE, and
    // the word the pill shows. One tap cycles, a long press picks from the list.
    property int mode: 1
    property string modeWord: "ARM"
    property bool mute: false
    property bool solo: false
    property bool arm: false
    property bool selected: false
    // Current input source of this strip: "NONE", "IN 03" or a stereo pair
    // "IN 03/04" (session inCh/inPair through MixerViewModel).
    property string inputText: ""
    readonly property bool focused: app.focusId === ("mixer.select." + stripIndex)
    readonly property string nameText: stripName !== "" ? stripName
                                                        : "IN " + String(stripIndex + 1).padStart(2, "0")

    color: Style.panel
    radius: Style.corner
    border.width: (selected || focused) ? 2 : Style.outlineWidth
    border.color: (selected || focused) ? Style.accent : Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        clip: true

        Row {
            width: parent.width
            height: Style.touch
            spacing: 2

            Text {
                id: nameText
                width: parent.width - Style.touch - 2
                height: parent.height
                text: root.nameText
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter

                // A4 entry point 2: tap the name selects, a 600 ms hold opens
                // the rename keypad.
                TapHandler {
                    onTapped: app.selectStrip(root.stripIndex)
                    onLongPressed: app.requestRenameTrack(root.stripIndex)
                }
            }
            TouchButton {
                width: Style.touch
                height: parent.height
                text: ""
                chevron: "up"
                variant: "ghost"
                focusId: "mixer.expand." + root.stripIndex
                onClicked: app.openChannel(root.stripIndex)
                // B2: the chevron states its payoff without stealing the tap.
                onLongClicked: app.showTextPopup(
                    "CHANNEL", "fader, pan, sends, monitor, I/O, insert, EQ")
            }
        }

        Row {
            width: parent.width
            // Six 44 px rows plus the 2 px gaps and the 3 px margins must fit
            // inside the strip, so the fader takes what is left over.
            height: Math.max(Style.touch, root.height - Style.touch * 4 - 24 -
                                         (root.armMutesBadge ? 24 : 0))
            spacing: 3

            TouchSlider {
                id: gain
                width: parent.width - 20
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
                width: 17
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

        Row {
            width: parent.width
            height: Style.touch
            spacing: 2

            KnobChip {
                width: parent.width - 68
                height: parent.height
                label: "PAN"
                value: root.pan
                valueText: mixer.panTextAt(root.stripIndex)
                minimumValue: 0
                maximumValue: 1
                focusId: "mixer.pan." + root.stripIndex
                popupLabel: root.nameText + " PAN"
                popupKind: "pan"
                onMoved: (value) => mixer.setPan(root.stripIndex, value)
            }
            // W1: the track mode is a word + a colour, never a letter. The pill
            // is 66 x 44 px, wide enough for OVERDUB at 9 px, and stays a 44 px
            // tall touch target so the mode is always readable on the strip.
            Rectangle {
                width: 66
                height: Style.touch
                radius: Style.corner
                color: Style.panelDark
                border.width: 1
                border.color: root.mode === 1 ? Style.record
                              : (root.mode === 0 ? Style.outline : Style.accent)
                // Panel focus stop for the pill (it is a tap/long-press target).
                property string focusId: "mixer.trackmode." + root.stripIndex

                Text {
                    anchors.fill: parent
                    text: root.modeWord
                    color: root.mode === 1 ? Style.record
                           : (root.mode === 0 ? Style.textDim : Style.accent)
                    font.pixelSize: root.modeWord.length > 4 ? 9 : Style.fontSmall
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TapHandler {
                    onTapped: app.cycleTrackMode(root.stripIndex)
                }
                TapHandler {
                    // Long press picks from the list instead of cycling.
                    onLongPressed: app.openTrackMode(root.stripIndex)
                }
            }
        }

        // Input-source selector: the list of none, mono IN 01..18 and the nine
        // stereo pairs opens as a popup and the pick applies immediately.
        TouchButton {
            width: parent.width
            height: Style.touch
            text: root.inputText
            textSize: Style.fontSmall
            variant: "ghost"
            chevron: "down"
            focusId: "mixer.in." + root.stripIndex
            onClicked: app.openInputSource(root.stripIndex)
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 2

            // C1 colour contract: mute is a dim fill with a readable label,
            // solo is the ok green, ARM stays record red.
            TouchButton {
                width: (parent.width - 4) / 3
                height: parent.height
                text: "M"
                checked: root.mute
                checkedColor: Style.textDim
                checkedTextColor: Style.text
                focusId: "mixer.mute." + root.stripIndex
                onClicked: mixer.toggleMute(root.stripIndex)
            }
            TouchButton {
                width: (parent.width - 4) / 3
                height: parent.height
                text: "S"
                checked: root.solo
                checkedColor: Style.ok
                checkedTextColor: Style.panelDark
                focusId: "mixer.solo." + root.stripIndex
                onClicked: mixer.toggleSolo(root.stripIndex)
            }
            TouchButton {
                width: (parent.width - 4) / 3
                height: parent.height
                text: "ARM"
                textSize: Style.fontSmall
                checked: root.arm
                checkedColor: Style.record
                focusId: "mixer.arm." + root.stripIndex
                onClicked: mixer.toggleArm(root.stripIndex)
            }
        }
    }

    TapHandler {
        onTapped: app.selectStrip(root.stripIndex)
    }
}
