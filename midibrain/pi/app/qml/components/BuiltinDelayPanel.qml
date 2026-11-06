import QtQuick
import Mnkybox 1.0

// Built-in delay (send 1) with its FULL parameter set on the FX tab: time,
// feedback-path cutoff and width, feedback, mix, beat sync, division and
// ping-pong. The X/Y grid only owns cutoff (X) and width (Y); these are the
// same instances, so both editors show the same values. Division opens its
// option list (tap-to-open), beat and ping-pong are live toggles.
Rectangle {
    id: root

    color: Style.panel
    radius: Style.cornerPanel
    border.width: Style.outlineWidth
    border.color: Style.outline

    readonly property var builtin: fx.builtin

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        Row {
            width: parent.width
            height: 18
            Text {
                width: parent.width - 90
                height: parent.height
                text: "DELAY  -  SEND 1"
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: 90
                height: parent.height
                text: "BUILT-IN"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Row {
            width: parent.width
            height: 72
            spacing: 0

            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "TIME"
                value: root.builtin.delayTimeMs
                minimumValue: 1
                maximumValue: 2000
                displayText: Math.round(root.builtin.delayTimeMs) + " ms"
                popupKind: "ms"
                popupLabel: "DELAY TIME"
                focusId: "fx.delay.time"
                onMoved: (value) => root.builtin.setDelayTimeMs(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "FEEDBACK"
                value: root.builtin.delayFeedback
                minimumValue: 0
                maximumValue: 0.95
                displayText: Math.round(root.builtin.delayFeedback * 100) + "%"
                popupKind: "percent"
                popupLabel: "DELAY FEEDBACK"
                focusId: "fx.delay.feedback"
                onMoved: (value) => root.builtin.setDelayFeedback(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "MIX"
                value: root.builtin.delayMix
                minimumValue: 0
                maximumValue: 1
                displayText: Math.round(root.builtin.delayMix * 100) + "%"
                popupKind: "percent"
                popupLabel: "DELAY MIX"
                focusId: "fx.delay.mix"
                onMoved: (value) => root.builtin.setDelayMix(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "BP CUTOFF"
                value: root.builtin.delayCutoffHz
                minimumValue: 20
                maximumValue: 20000
                displayText: root.builtin.delayCutoffHz >= 1000
                             ? (root.builtin.delayCutoffHz / 1000).toFixed(1) + " kHz"
                             : Math.round(root.builtin.delayCutoffHz) + " Hz"
                popupKind: "hz"
                popupLabel: "DELAY BP CUTOFF"
                focusId: "fx.delay.cutoff"
                onMoved: (value) => root.builtin.setDelayCutoffHz(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "BP WIDTH"
                value: root.builtin.delayWidthOct
                minimumValue: 0.1
                maximumValue: 4
                displayText: Number(root.builtin.delayWidthOct).toFixed(1) + " oct"
                popupKind: "generic"
                popupLabel: "DELAY BP WIDTH"
                focusId: "fx.delay.width"
                onMoved: (value) => root.builtin.setDelayWidthOct(value)
            }
        }

        Row {
            width: parent.width
            height: 44
            spacing: 4

            TouchButton {
                width: (parent.width - 8) / 3
                height: parent.height
                text: root.builtin.delayBeat ? "BEAT ON" : "BEAT OFF"
                textSize: Style.fontSmall
                checked: root.builtin.delayBeat
                focusId: "fx.delay.beat"
                onClicked: root.builtin.delayBeat = !root.builtin.delayBeat
            }
            TouchButton {
                width: (parent.width - 8) / 3
                height: parent.height
                text: root.builtin.delayPingPong ? "PING ON" : "PING OFF"
                textSize: Style.fontSmall
                checked: root.builtin.delayPingPong
                focusId: "fx.delay.ping"
                onClicked: root.builtin.delayPingPong = !root.builtin.delayPingPong
            }
            TouchButton {
                width: (parent.width - 8) / 3
                height: parent.height
                text: root.builtin.delayDivisionText
                textSize: Style.fontSmall
                variant: "ghost"
                chevron: "down"
                focusId: "fx.delay.division"
                onClicked: app.openConfigFrom("delaydivision", this)
            }
        }
    }
}
