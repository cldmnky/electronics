import QtQuick
import Mnkybox 1.0
import "../components"

// Banked mixer: nine strips plus a pinned master column. The master stays
// visible in both banks; the bank buttons are touch targets as well.
Item {
    id: screen

    Row {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Item {
            id: stripsArea
            width: parent.width - Style.masterWidth - 3
            height: parent.height

            Repeater {
                model: mixer
                delegate: MixerStrip {
                    visible: index >= mixer.bank * mixer.stripsPerBank &&
                             index < (mixer.bank + 1) * mixer.stripsPerBank
                    width: visible ? stripsArea.width / mixer.stripsPerBank : 0
                    height: stripsArea.height
                    x: visible ? (index % mixer.stripsPerBank) * width : 0
                    stripIndex: index
                    gainDb: model.gainDb
                    pan: model.pan
                    panText: model.panText
                    sendA: model.sendA
                    sendB: model.sendB
                    mute: model.mute
                    solo: model.solo
                    arm: model.arm
                    selected: model.selected
                }
            }
        }

        Column {
            width: Style.masterWidth
            height: parent.height
            spacing: 3

            Row {
                width: parent.width
                height: Style.touch
                spacing: 2

                TouchButton {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    text: "1-9"
                    textSize: Style.fontSmall
                    checked: mixer.bank === 0
                    focusId: "mixer.bank.0"
                    onClicked: mixer.setBank(0)
                }
                TouchButton {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    text: "10-18"
                    textSize: Style.fontSmall
                    checked: mixer.bank === 1
                    focusId: "mixer.bank.1"
                    onClicked: mixer.setBank(1)
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height - Style.touch - 3
                color: Style.panelDark
                radius: Style.corner
                border.width: Style.outlineWidth
                border.color: Style.outline

                Column {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 4

                    Text {
                        width: parent.width
                        height: 18
                        text: "MASTER"
                        color: Style.text
                        font.pixelSize: Style.fontTitle
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    TouchSlider {
                        id: masterGain
                        width: parent.width
                        height: 112
                        vertical: true
                        minimumValue: -60
                        maximumValue: 12
                        value: mixer.masterDb
                        focusId: "mixer.master"
                        Binding on value { value: mixer.masterDb; when: !masterGain.dragging }
                        onMoved: (value) => mixer.setMasterDb(value)
                    }

                    Row {
                        width: parent.width
                        height: Math.max(40, parent.height - 18 - 112 - 8)
                        spacing: 5

                        Repeater {
                            model: 2
                            delegate: Item {
                                width: (parent.width - 5) / 2
                                height: parent.height

                                Rectangle {
                                    anchors.fill: parent
                                    color: Style.panel
                                    border.width: Style.outlineWidth
                                    border.color: Style.outline
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 1
                                    height: Math.max(0, (parent.height - 2) *
                                                     (index === 0 ? meterFill(mixer.meterLeft)
                                                                  : meterFill(mixer.meterRight)))
                                    color: (index === 0 ? mixer.meterLeft : mixer.meterRight) > 0.95
                                           ? Style.record : Style.ok
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Meter fill in dB, floored at -48 dB like the engine meter mapping.
    function meterFill(peak) {
        if (!(peak > 0)) return 0
        const db = 20 * Math.log(peak) / Math.LN10
        if (db <= -48) return 0
        return Math.min(1, (db + 48) / 48)
    }
}
