import QtQuick
import Mnkybox 1.0
import "../components"

// FX: the primary face shows the two built-in send effects with their FULL
// settings (delay and reverb are no longer X/Y-only) beside the two CLAP send
// slots. SHIFT keeps the large CLAP parameter editor for slot B with both return
// levels, which is where a plugin's own parameter list gets the whole height.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Item {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin

        Column {
            anchors.fill: parent
            spacing: Style.gap

            Row {
                width: parent.width
                height: (parent.height - Style.gap) / 2
                spacing: Style.gap

                BuiltinDelayPanel {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                }
                BuiltinReverbPanel {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                }
            }

            Row {
                width: parent.width
                height: (parent.height - Style.gap) / 2
                spacing: Style.gap

                FxSlotPanel {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    slot: fx.slotA
                    slotLetter: "A"
                    showReturn: false
                }
                FxSlotPanel {
                    width: (parent.width - Style.gap) / 2
                    height: parent.height
                    slot: fx.slotB
                    slotLetter: "B"
                    showReturn: false
                }
            }
        }
    }

    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            Text {
                width: 64
                height: parent.height
                text: "RET A"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            TouchSlider {
                id: returnA
                width: parent.width - 72
                height: parent.height
                minimumValue: -60
                maximumValue: 12
                value: fx.slotA.returnDb
                focusId: "shift.fx.A.return"
                popupLabel: "SEND A RETURN"
                popupKind: "db"
                Binding on value { value: fx.slotA.returnDb; when: !returnA.dragging }
                onMoved: (value) => fx.slotA.returnDb = value
            }
        }

        FxSlotPanel {
            width: parent.width
            height: parent.height - Style.touch - Style.gap
            slot: fx.slotB
            slotLetter: "B"
            focusPrefix: "shift.fx"
        }
    }
}
