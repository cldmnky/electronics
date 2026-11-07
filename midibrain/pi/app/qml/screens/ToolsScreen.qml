import QtQuick
import QtQuick.Controls
import Mnkybox 1.0

// DIAGNOSTICS: the troubleshooting surface behind System > DIAGNOSTICS. It is
// deliberately the raw audio request/effective note, the raw protocol counter
// block and the app log tail; the System primary page carries only the readable
// summaries. Read-only, so the panel reaches it through the SHIFT page tabs and
// does not need a ring of its own.
Item {
    id: page

    Column {
        id: body
        width: parent.width
        height: parent.height
        spacing: Style.gapTight

        // The two dumps split whatever the four headings and the health block
        // leave, so the page never grows past the face it is loaded into.
        readonly property real dumpHeight: height - audioHeading.height -
                                           audioNote.height - healthHeading.height -
                                           healthText.height - statsHeading.height -
                                           logHeading.height - Style.gapTight * 6

        Text {
            id: audioHeading
            width: parent.width
            height: 18
            text: "AUDIO - REQUEST / EFFECTIVE"
            color: Style.textDim
            font.pixelSize: Style.fontSecondary
            font.weight: Font.DemiBold
        }
        Text {
            id: audioNote
            width: parent.width
            height: 18
            // Long by nature: the counter block below carries the same facts, so
            // this line may elide.
            text: system.noteText
            color: Style.text
            font.pixelSize: Style.fontSecondary
            elide: Text.ElideRight
        }
        Text {
            id: healthHeading
            width: parent.width
            height: 18
            text: "LINK HEALTH - must-stay-zero counters in plain language"
            color: Style.textDim
            font.pixelSize: Style.fontSecondary
            font.weight: Font.DemiBold
        }
        Text {
            id: healthText
            width: parent.width
            height: 54
            text: system.linkHealthText
            color: Style.text
            font.family: "monospace"
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
            clip: true
        }
        Text {
            id: statsHeading
            width: parent.width
            height: 18
            text: "LINK / CLOCK / TRANSPORT COUNTERS"
            color: Style.textDim
            font.pixelSize: Style.fontSecondary
            font.weight: Font.DemiBold
        }
        TextArea {
            width: parent.width
            height: body.dumpHeight * 0.62
            readOnly: true
            text: system.statsText
            color: Style.text
            font.family: "monospace"
            font.pixelSize: Style.fontSmall
            wrapMode: TextEdit.NoWrap
            clip: true
            background: Rectangle {
                color: Style.panelDark
                radius: Style.corner
            }
        }
        Text {
            id: logHeading
            width: parent.width
            height: 18
            text: "APP LOG - last 120 lines"
            color: Style.textDim
            font.pixelSize: Style.fontSecondary
            font.weight: Font.DemiBold
        }
        TextArea {
            width: parent.width
            height: body.dumpHeight * 0.38
            readOnly: true
            text: system.logText
            color: Style.textDim
            font.family: "monospace"
            font.pixelSize: Style.fontSmall
            wrapMode: TextEdit.NoWrap
            clip: true
            background: Rectangle {
                color: Style.panelDark
                radius: Style.corner
            }
        }
    }
}
