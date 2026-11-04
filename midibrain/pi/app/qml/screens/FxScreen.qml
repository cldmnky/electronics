import QtQuick
import Mnkybox 1.0
import "../components"

// Two CLAP send slots. The send level itself is per strip on the Mixer screen;
// the return level, bypass and plugin state live here.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 4

        FxSlotPanel {
            width: parent.width
            height: (parent.height - 4) / 2
            slot: fx.slotA
            slotLetter: "A"
        }
        FxSlotPanel {
            width: parent.width
            height: (parent.height - 4) / 2
            slot: fx.slotB
            slotLetter: "B"
        }
    }
}
