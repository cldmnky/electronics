import QtQuick
import Mnkybox 1.0

// Built-in reverb (send 2) with its FULL parameter set on the FX tab: model,
// pre-delay, decay, damping, mix and size. The X/Y grid only owns size (X) and
// mix (Y); the model picker opens its option list (tap-to-open) and applies the
// preset's decay/damping/size immediately.
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
                text: "REVERB  -  SEND 2"
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
                label: "PREDELAY"
                value: root.builtin.reverbPredelayMs
                minimumValue: 0
                maximumValue: 200
                displayText: Math.round(root.builtin.reverbPredelayMs) + " ms"
                popupKind: "ms"
                popupLabel: "REVERB PREDELAY"
                focusId: "fx.reverb.predelay"
                onMoved: (value) => root.builtin.setReverbPredelayMs(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "DECAY"
                value: root.builtin.reverbDecay
                minimumValue: 0
                maximumValue: 0.98
                displayText: Number(root.builtin.reverbDecay).toFixed(2)
                popupKind: "generic"
                popupLabel: "REVERB DECAY"
                focusId: "fx.reverb.decay"
                onMoved: (value) => root.builtin.setReverbDecay(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "DAMPING"
                value: root.builtin.reverbDamping
                minimumValue: 0
                maximumValue: 1
                displayText: Number(root.builtin.reverbDamping).toFixed(2)
                popupKind: "generic"
                popupLabel: "REVERB DAMPING"
                focusId: "fx.reverb.damping"
                onMoved: (value) => root.builtin.setReverbDamping(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "MIX"
                value: root.builtin.reverbMix
                minimumValue: 0
                maximumValue: 1
                displayText: Math.round(root.builtin.reverbMix * 100) + "%"
                popupKind: "percent"
                popupLabel: "REVERB MIX"
                focusId: "fx.reverb.mix"
                onMoved: (value) => root.builtin.setReverbMix(value)
            }
            FxParamKnob {
                width: parent.width / 5
                height: parent.height
                label: "SIZE"
                value: root.builtin.reverbSize
                minimumValue: 0.25
                maximumValue: 2
                displayText: Number(root.builtin.reverbSize).toFixed(2)
                popupKind: "generic"
                popupLabel: "REVERB SIZE"
                focusId: "fx.reverb.size"
                onMoved: (value) => root.builtin.setReverbSize(value)
            }
        }

        Row {
            width: parent.width
            height: 44
            spacing: 4

            TouchButton {
                width: parent.width - 4
                height: parent.height
                text: "MODEL   " + root.builtin.reverbModelText
                textSize: Style.fontSmall
                variant: "ghost"
                chevron: "down"
                focusId: "fx.reverb.model"
                onClicked: app.openConfigFrom("reverbmodel", this)
            }
        }
    }
}
