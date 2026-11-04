import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// One CLAP send slot: identity/load/eject/state buttons, return level, bypass
// and the plugin's own parameters in a touch-scrollable list.
Rectangle {
    id: root

    property var slot
    property string slotLetter: "A"
    property string focusPrefix: "fx"
    property bool showReturn: true

    color: Style.panel
    radius: Style.corner
    border.width: Style.outlineWidth
    border.color: Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 3

            Text {
                width: 64
                height: parent.height
                text: "SEND " + root.slotLetter
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: Math.max(Style.touch, parent.width - 64 - 4 * 56 - 12)
                height: parent.height
                text: root.slot && root.slot.pluginName !== "" ? root.slot.pluginName
                                                              : "pick plugin"
                textSize: Style.fontSmall
                focusId: root.focusPrefix + "." + root.slotLetter + ".plugin"
                onClicked: root.slot.loadDefault()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "EJECT"
                textSize: Style.fontSmall
                focusId: root.focusPrefix + "." + root.slotLetter + ".eject"
                onClicked: root.slot.eject()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "SAVE"
                textSize: Style.fontSmall
                focusId: root.focusPrefix + "." + root.slotLetter + ".save"
                onClicked: root.slot.saveState()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "LOAD"
                textSize: Style.fontSmall
                focusId: root.focusPrefix + "." + root.slotLetter + ".load"
                onClicked: root.slot.loadState()
            }
        }

        Row {
            visible: root.showReturn
            width: parent.width
            height: visible ? Style.touch : 0
            spacing: 4

            Text {
                width: 56
                height: parent.height
                text: "RETURN"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            TouchSlider {
                id: returnSlider
                width: Math.max(120, parent.width - 56 - 88 - 8)
                height: Style.touch
                minimumValue: -60
                maximumValue: 12
                value: root.slot ? root.slot.returnDb : 0
                focusId: root.focusPrefix + "." + root.slotLetter + ".return"
                popupLabel: "SEND " + root.slotLetter + " RETURN"
                popupKind: "db"
                Binding on value { value: root.slot.returnDb; when: !returnSlider.dragging }
                onMoved: (value) => root.slot.returnDb = value
            }
            TouchButton {
                width: 84
                height: parent.height
                text: "BYPASS"
                textSize: Style.fontSmall
                checked: root.slot ? root.slot.bypassed : false
                checkedColor: Style.soloOn
                focusId: root.focusPrefix + "." + root.slotLetter + ".bypass"
                onClicked: root.slot.toggleBypass()
            }
        }

        Text {
            width: parent.width
            height: 15
            text: root.slot ? root.slot.statusText : ""
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }

        ListView {
            width: parent.width
            height: Math.max(0, parent.height - Style.touch - (root.showReturn ? Style.touch : 0) - 3 * 3 - 15)
            clip: true
            model: root.slot ? root.slot.params : null
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 6
            }

            delegate: Row {
                width: ListView.view.width
                height: Style.touch
                spacing: 4

                Text {
                    width: 82
                    height: parent.height
                    text: model.name
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                TouchSlider {
                    id: paramSlider
                    width: parent.width - 86
                    height: Style.touch
                    minimumValue: model.minimum
                    maximumValue: model.maximum
                    value: model.value
                    focusId: root.focusPrefix + "." + root.slotLetter + ".param." + index
                    popupLabel: model.name
                    popupKind: "generic"
                    Binding on value { value: model.value; when: !paramSlider.dragging }
                    onMoved: (value) => root.slot.setParam(index, value)
                }
            }
        }
    }
}
