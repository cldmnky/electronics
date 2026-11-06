import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// Device selector for the System screen. The three model columns are shown, as
// the view model supplies them: the device name exactly as the backend
// enumerated it, the backend and the channel count. Two USB devices that share
// a generic name therefore stay distinguishable in the list, and the closed
// field carries the same three columns.
ComboBox {
    id: root

    property string focusId: ""

    height: Style.touch
    font.pixelSize: Style.fontBody
    textRole: "label"

    background: Rectangle {
        color: Style.panelDark
        radius: Style.corner
        border.width: (app.focusId === root.focusId && root.focusId !== "") ? 2
                                                                             : Style.outlineWidth
        border.color: (app.focusId === root.focusId && root.focusId !== "")
                      ? Style.accent : Style.outline
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: 24
        text: root.displayText
        color: Style.text
        font.pixelSize: Style.fontBody
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: root.width - width - 8
        y: root.topPadding + (root.availableHeight - height) / 2
        text: "v"
        color: Style.textDim
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
    }

    delegate: ItemDelegate {
        width: root.width
        height: Style.touch

        contentItem: Column {
            x: 8
            width: parent.width - 16
            spacing: 0

            Text {
                width: parent.width
                text: modelData.name
                color: highlighted ? Style.panelDark : Style.text
                font.pixelSize: Style.fontBody
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: modelData.detail
                color: highlighted ? Style.panelDark : Style.textDim
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: highlighted ? Style.accent : Style.panelDark
            radius: Style.corner
            border.width: Style.outlineWidth
            border.color: Style.outline
        }
    }

    popup.width: root.width
}
