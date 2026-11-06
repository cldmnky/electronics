import QtQuick
import Mnkybox 1.0
import "../components"

// TOOLS status: take-volume free space with the low-space banner, the screen
// brightness control and the app/firmware version readout. The banner is a
// measured disk stat, not a guess; brightness is a best-effort Pi sysfs write.
Item {
    id: page

    property string focusPrefix: "tools"

    Column {
        width: parent.width
        spacing: Style.gapTight

        Rectangle {
            width: parent.width
            height: 44
            visible: tools.lowSpace
            color: Style.dangerFill
            border.width: 1
            border.color: Style.record
            Text {
                anchors.fill: parent
                anchors.margins: 8
                text: tools.bannerText
                color: Style.text
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            width: parent.width
            height: 18
            text: "TAKE VOLUME"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            height: 26
            text: tools.diskText
            color: tools.lowSpace ? Style.record : Style.text
            font.pixelSize: Style.fontTitle
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: parent.width
            height: 16
            text: tools.takeDirectoryText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideMiddle
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Style.outline
        }

        Text {
            width: parent.width
            height: 18
            text: "SCREEN BRIGHTNESS"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Row {
            width: parent.width
            height: Style.touch
            spacing: 6
            TouchSlider {
                id: brightness
                width: parent.width - 120
                height: parent.height
                minimumValue: 5
                maximumValue: 100
                value: tools.brightness
                active: tools.brightnessAvailable
                focusId: page.focusPrefix + ".brightness"
                popupLabel: "BRIGHTNESS"
                popupKind: "percent"
                Binding on value { value: tools.brightness; when: !brightness.dragging }
                onMoved: (value) => tools.brightness = Math.round(value)
            }
            Text {
                width: 114
                height: parent.height
                text: tools.brightness + " %"
                color: tools.brightnessAvailable ? Style.text : Style.textDim
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
            }
        }
        Text {
            width: parent.width
            height: 30
            text: tools.brightnessNote
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Style.outline
        }

        Text {
            width: parent.width
            height: 18
            text: "VERSIONS"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            height: 20
            text: "APP      " + tools.appVersion
            color: Style.text
            font.pixelSize: Style.fontBody
            font.family: "monospace"
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            width: parent.width
            height: 20
            text: "FIRMWARE " + tools.firmwareVersion
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.family: "monospace"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
