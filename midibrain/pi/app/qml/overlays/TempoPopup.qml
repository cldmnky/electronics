import QtQuick
import Mnkybox 1.0
import "../components"

// Touch tempo popup: a 44 px slider over 40..240 BPM, +-1 buttons and a
// TAP button that averages three taps. The BPM button in the transport strip
// opens it; DONE closes it and the panel's PRESS/encoder work the same popup.
Rectangle {
    id: root

    anchors.fill: parent
    visible: app.tempoPopupOpen
    color: "#9e000000"
    z: 96

    // Swallow touches outside the panel; no dismiss (the same contract as the
    // config popup).
    MouseArea {
        anchors.fill: parent
        onClicked: {}
    }

    Rectangle {
        id: panel
        width: Math.min(560, root.width - 40)
        height: 256
        anchors.centerIn: parent
        color: Style.raised
        radius: Style.cornerPanel
        border.width: 2
        border.color: Style.accent

        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: Style.gap

            Row {
                width: parent.width
                height: 24

                Text {
                    width: parent.width - 120
                    height: parent.height
                    text: "TEMPO"
                    color: Style.text
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    width: 120
                    height: parent.height
                    text: app.tempoText
                    color: Style.accent
                    font.pixelSize: Style.fontTitle
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Text {
                width: parent.width
                height: 16
                text: "drag or +-1  -  TAP three times for tap tempo"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }

            TouchSlider {
                id: tempoSlider
                width: parent.width
                height: Style.touch
                minimumValue: 40
                maximumValue: 240
                value: app.tempoBpm
                popupLabel: "TEMPO"
                popupKind: "bpm"
                Binding on value { value: app.tempoBpm; when: !tempoSlider.dragging }
                onMoved: (value) => app.setTempoBpm(Math.round(value))
            }

            Row {
                width: parent.width
                height: Style.touchPrimary
                spacing: Style.gap

                TouchButton {
                    width: 64
                    height: parent.height
                    text: "-1"
                    textSize: Style.fontBody
                    onClicked: app.setTempoBpm(Math.round(app.tempoBpm) - 1)
                }
                TouchButton {
                    width: 64
                    height: parent.height
                    text: "+1"
                    textSize: Style.fontBody
                    onClicked: app.setTempoBpm(Math.round(app.tempoBpm) + 1)
                }
                TouchButton {
                    width: 110
                    height: parent.height
                    text: "TAP"
                    textSize: Style.fontBody
                    onClicked: app.tapTempo()
                }
                TouchButton {
                    width: parent.width - 64 - 64 - 110 - Style.gap * 3
                    height: parent.height
                    text: "DONE"
                    variant: "primary"
                    focusId: "tempo.done"
                    onClicked: app.closeTempoPopup()
                }
            }
        }
    }
}
