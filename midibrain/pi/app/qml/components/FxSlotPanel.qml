import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// One CLAP send slot: identity/load/eject/state buttons, return level, bypass
// and the plugin's own parameters. Parameters are circular knobs (the return
// stays a fader); the list scrolls at 56 px rows so every knob keeps a 44 px
// touch target.
Rectangle {
    id: root

    property var slot
    property string slotLetter: "A"
    property string focusPrefix: "fx"
    property bool showReturn: true

    color: Style.panel
    radius: Style.cornerPanel
    border.width: Style.outlineWidth
    border.color: Style.outline

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: Style.gapTight

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gapTight

            Text {
                width: 64
                height: parent.height
                text: "SEND " + root.slotLetter
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            TouchButton {
                width: Math.max(Style.touch, parent.width - 64 - 3 * 56 - 12)
                height: parent.height
                text: root.slot && root.slot.pluginName !== "" ? root.slot.pluginName
                                                              : "pick plugin"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + "." + root.slotLetter + ".plugin"
                onClicked: root.slot.loadDefault()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "EJECT"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + "." + root.slotLetter + ".eject"
                onClicked: root.slot.eject()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "SAVE"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + "." + root.slotLetter + ".save"
                onClicked: root.slot.saveState()
            }
            TouchButton {
                width: 52
                height: parent.height
                text: "LOAD"
                textSize: Style.fontSmall
                variant: "ghost"
                focusId: root.focusPrefix + "." + root.slotLetter + ".load"
                onClicked: root.slot.loadState()
            }
        }

        Row {
            visible: root.showReturn
            width: parent.width
            height: visible ? Style.touch : 0
            spacing: Style.gapTight

            Text {
                width: 56
                height: parent.height
                text: "RETURN"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
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

        Item {
            width: parent.width
            height: Math.max(0, parent.height - Style.touch - (root.showReturn ? Style.touch : 0) - 3 * Style.gapTight - 15)

        ListView {
            id: paramList
            anchors.fill: parent
            clip: true
            model: root.slot ? root.slot.params : null
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 6
            }

            delegate: Row {
                width: ListView.view.width
                height: 56
                spacing: Style.gapTight

                Text {
                    width: 86
                    height: parent.height
                    text: model.name
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                TouchKnob {
                    id: paramKnob
                    width: parent.width - 90
                    height: parent.height
                    showText: true
                    dialSize: 44
                    minimumValue: model.minimum
                    maximumValue: model.maximum
                    value: model.value
                    label: ""
                    valueText: Number(model.value).toFixed(2)
                    focusId: root.focusPrefix + "." + root.slotLetter + ".param." + index
                    popupLabel: model.name
                    popupKind: "generic"
                    Binding on value { value: model.value; when: !paramKnob.dragging }
                    onMoved: (value) => root.slot.setParam(index, value)
                }
            }

        }

        Text {
            visible: paramList.count === 0
            anchors.centerIn: parent
            width: parent.width - 20
            text: "no plugin loaded - tap pick plugin"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        }
    }
}
