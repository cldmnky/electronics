import QtQuick
import Mnkybox 1.0

// MIDI-learn capture panel used by the SYSTEM SHIFT face.
Item {
    id: root

    property string focusPrefix: "learn"

    Column {
        width: parent.width
        spacing: Style.gapTight

        Text {
            width: parent.width
            height: 16
            text: "MIDI LEARN - capture a control change"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            TouchComboBox {
                id: learnTarget
                width: parent.width - 148
                model: learn.targets
                focusId: root.focusPrefix + ".target"
                onActivated: (index) => learn.targetIndex = index
                Binding on currentIndex { value: learn.targetIndex; when: !learnTarget.popup.visible }
            }
            TouchButton {
                width: 144
                height: parent.height
                text: learn.capturing ? "WAIT FOR CC" : "ARM CAPTURE"
                textSize: Style.fontSmall
                checked: learn.capturing
                focusId: root.focusPrefix + ".arm"
                onClicked: learn.capturing ? learn.cancelCapture() : learn.startCapture()
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            Text {
                width: parent.width - 88
                height: parent.height
                text: learn.statusText
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
            height: Math.max(0, root.height - Style.touch * 2 - 19)
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
