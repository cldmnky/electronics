import QtQuick
import Mnkybox 1.0

// MIDI-learn capture panel used by the SYSTEM SHIFT face.
// A9: the primary flow is arm-then-touch. ARM CAPTURE arms the target capture
// with no target; every learnable control outlines in the accent colour and
// the next one touched becomes the target, then the next incoming CC completes
// the binding. Picking an internal id by hand is the secondary path, labelled
// PICK TARGET MANUALLY; binding rows use human names only.
Item {
    id: root

    property string focusPrefix: "learn"

    Column {
        width: parent.width
        spacing: Style.gapTight

        Text {
            width: parent.width
            height: 16
            text: "MIDI LEARN - arm, then touch the control you want to map"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            TouchButton {
                width: 200
                height: parent.height
                text: learn.capturing ? "WAIT FOR CC"
                      : (learn.targetArmed ? "TOUCH A CONTROL" : "ARM CAPTURE")
                textSize: Style.fontSmall
                checked: learn.capturing || learn.targetArmed
                focusId: root.focusPrefix + ".arm"
                onClicked: {
                    if (learn.capturing || learn.targetArmed) {
                        learn.cancelCapture()
                        return
                    }
                    // With the manual picker open the picked id is the target;
                    // otherwise arm the touch-to-pick flow.
                    if (manualRow.expanded) learn.startCapture()
                    else app.armLearnTargetCapture()
                }
            }
            Text {
                width: parent.width - 200 - Style.gapTight
                height: parent.height
                text: learn.statusText
                color: learn.capturing || learn.targetArmed ? Style.accent : Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        // Secondary path: pick an id by hand. Demoted below the arm-then-touch
        // flow and collapsed by default.
        Row {
            id: manualRow
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight
            property bool expanded: false

            TouchButton {
                width: 200
                height: parent.height
                text: manualRow.expanded ? "PICK TARGET MANUALLY <" : "PICK TARGET MANUALLY >"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + ".target"
                onClicked: manualRow.expanded = !manualRow.expanded
            }
            TouchComboBox {
                id: learnTarget
                visible: manualRow.expanded
                width: parent.width - 200 - Style.gapTight
                height: parent.height
                model: learn.targetLabels
                onActivated: (index) => learn.targetIndex = index
                Binding on currentIndex { value: learn.targetIndex; when: !learnTarget.popup.visible }
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            Text {
                width: parent.width - 88
                height: parent.height
                text: learn.bindings.length > 0
                      ? learn.bindings.length + " binding(s)"
                      : "no bindings - ARM CAPTURE, then move a knob on your controller"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "CLEAR"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + ".clear"
                onClicked: learn.clearAll()
            }
        }

        ListView {
            width: parent.width
            height: Math.max(0, root.height - Style.touch * 3 - 16 - Style.gapTight * 3)
            model: learn.bindings
            clip: true
            delegate: Row {
                width: ListView.view.width
                height: Style.touch
                spacing: Style.gapTight

                Text {
                    width: parent.width - 92
                    height: parent.height
                    text: modelData
                    color: Style.text
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                TouchButton {
                    width: 88
                    height: parent.height
                    text: "REMOVE"
                    textSize: Style.fontSmall
                    focusId: root.focusPrefix + ".binding." + index
                    onClicked: learn.removeBinding(index)
                }
            }
        }
    }
}
