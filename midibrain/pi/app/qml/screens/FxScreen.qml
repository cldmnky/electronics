import QtQuick
import Mnkybox 1.0
import "../components"

// FX: primary face is the first send slot's parameters; SHIFT is the second
// parameter bank plus both return levels. The per-strip send amount lives on
// the Mixer SHIFT face.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    FxSlotPanel {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        slot: fx.slotA
        slotLetter: "A"
        showReturn: false
    }

    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        Row {
            width: parent.width
            height: Style.touch
            spacing: 4

            Text {
                width: 64
                height: parent.height
                text: "RET A"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            TouchSlider {
                id: returnA
                width: parent.width - 68
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
            height: parent.height - Style.touch - 4
            slot: fx.slotB
            slotLetter: "B"
            focusPrefix: "shift.fx"
        }
    }
}
