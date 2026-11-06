import QtQuick
import Mnkybox 1.0
import "../components"

// TRACK FX: the per-track FX rack, opened from the channel popout's TRACK FX
// row (and from the X/Y screen). The engine runs the two slots as a series
// chain, post-EQ and pre-fader, and each slot crossfades dry/wet, so this popup
// edits exactly what the strip plays:
//
//   FX1 card -> FX2 card          (subline states the order and the scope)
//   selector  NONE / DELAY / REVERB / PLUGIN (greyed: no per-track CLAP host)
//   WET       the slot's dry/wet crossfade
//   BYPASS    the slot's hard dry switch
//   params    the effect's own controls as arc-dial chips (slider law)
//
// Every control is live (no apply button); CLOSE only closes. The panel focus
// ring is trapped here while the popup is open.
Rectangle {
    id: root

    anchors.fill: parent
    visible: fx.open
    color: "#9e000000"
    // Above the channel popout, below the config popup the selectors open.
    z: 92

    // Swallow touches outside the panel; the popup never dismisses on a stray
    // touch, exactly like the config popup and the channel popout.
    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        id: panel
        width: Math.min(768, root.width - 16)
        height: root.height - 16
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: Style.gapTight

            Row {
                width: parent.width
                height: Style.touch
                spacing: Style.gapTight

                Text {
                    width: parent.width - 128 - Style.gapTight
                    height: parent.height
                    text: fx.title
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TouchButton {
                    width: 128
                    height: parent.height
                    text: "CLOSE"
                    variant: "primary"
                    focusId: "trackfx.close"
                    onClicked: app.closeTrackFx()
                }
            }

            Row {
                width: parent.width
                height: 16

                Text {
                    width: parent.width / 2
                    height: parent.height
                    text: fx.subline
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width / 2
                    height: parent.height
                    text: fx.footer
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            Row {
                width: parent.width
                height: parent.height - Style.touch - 16 - Style.gapTight * 2
                spacing: Style.gap

                SlotCard {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    card: fx.fx1
                    slot: 0
                }
                SlotCard {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    card: fx.fx2
                    slot: 1
                }
            }
        }
    }

    // One rack slot. `card` is the FxSlotCard the view model exposes; every
    // write goes back through fx.*, so the rack, the session and the engine
    // stay one value.
    component SlotCard: Rectangle {
        id: slotCard

        property var card: null
        property int slot: 0
        readonly property string prefix: "trackfx.fx" + (slot + 1)

        color: Style.panel
        radius: Style.cornerPanel
        border.width: app.focusId.startsWith(slotCard.prefix) ? 2 : Style.outlineWidth
        border.color: app.focusId.startsWith(slotCard.prefix) ? Style.accent : Style.outline

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: Style.gapTight

            Text {
                width: parent.width
                height: 16
                text: (slotCard.slot === 0 ? "FX1" : "FX2") + "  " +
                      (slotCard.card !== null ? slotCard.card.effectName : "")
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            // Effect selector: a tap opens the slot list (NONE / DELAY / REVERB /
            // PLUGIN greyed). The closed state names what the slot runs.
            TouchButton {
                width: parent.width
                height: Style.touch
                text: slotCard.card !== null ? slotCard.card.effectName : "NONE"
                textSize: Style.fontSmall
                variant: "ghost"
                chevron: "down"
                focusId: slotCard.prefix + ".select"
                onClicked: app.openFxSlot(slotCard.slot)
            }

            Row {
                width: parent.width
                height: Style.touch
                spacing: Style.gapTight

                KnobChip {
                    width: parent.width - 104 - Style.gapTight
                    height: parent.height
                    label: "WET"
                    value: slotCard.card !== null ? slotCard.card.wet : 0
                    valueText: Math.round((slotCard.card !== null ? slotCard.card.wet : 0) * 100)
                    suffix: "%"
                    minimumValue: 0
                    maximumValue: 1
                    popupLabel: (slotCard.slot === 0 ? "FX1" : "FX2") + " WET"
                    popupKind: "percent"
                    focusId: slotCard.prefix + ".wet"
                    onMoved: (value) => fx.setWet(slotCard.slot, value)
                }
                TouchButton {
                    width: 104
                    height: parent.height
                    text: "BYPASS"
                    textSize: Style.fontSmall
                    checked: slotCard.card !== null ? slotCard.card.bypass : false
                    focusId: slotCard.prefix + ".bypass"
                    onClicked: fx.toggleBypass(slotCard.slot)
                }
            }

            Text {
                width: parent.width
                height: 16
                text: slotCard.card !== null ? slotCard.card.note : ""
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            // The effect's own parameters, in its stable order: a tight cell is
            // an arc-dial chip (the slider law), two per row.
            Grid {
                id: paramGrid
                width: parent.width
                columns: 2
                spacing: Style.gapTight

                Repeater {
                    model: slotCard.card !== null ? slotCard.card.params : []

                    delegate: KnobChip {
                        width: (paramGrid.width - paramGrid.spacing) / paramGrid.columns
                        height: Style.touch
                        label: modelData.name
                        value: modelData.value
                        valueText: modelData.text
                        suffix: modelData.unit
                        minimumValue: modelData.minimum
                        maximumValue: modelData.maximum
                        popupLabel: (slotCard.slot === 0 ? "FX1" : "FX2") + " " + modelData.name
                        popupKind: "generic"
                        focusId: slotCard.prefix + ".param." + modelData.index
                        onMoved: (value) => fx.setParam(slotCard.slot, modelData.index, value)
                    }
                }
            }
        }
    }
}
