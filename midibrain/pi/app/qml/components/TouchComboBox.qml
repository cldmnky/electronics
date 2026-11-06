import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// Touch-first combo box: 44 px field and 44 px popup rows, dark themed, with the
// panel NAV focus ring. The screen supplies model/currentIndex/onActivated.
ComboBox {
    id: root

    property string focusId: ""

    height: Style.touch
    font.pixelSize: Style.fontBody

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

        contentItem: Text {
            leftPadding: 8
            text: modelData
            color: highlighted ? Style.panelDark : Style.text
            font.pixelSize: Style.fontBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
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
