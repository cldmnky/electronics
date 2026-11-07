import QtQuick
import Mnkybox 1.0
import "../components"

// PUNCH page: PLAY FROM / PUNCH IN / PUNCH OUT measure markers. A marker is a
// session v2 punch point (enabled + 1-based measure); the transport consumes
// it. The app owns the values; the firmware only receives PLAY/STOP.
Item {
    id: page

    property string focusPrefix: "punch"
    property bool compact: false

    Column {
        width: parent.width
        spacing: page.compact ? Style.gapTight : Style.gap

        Text {
            width: parent.width
            height: 18
            text: "PUNCH MARKERS  -  1-based session measures"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        PunchRow {
            width: parent.width
            label: "PLAY FROM"
            enabledValue: session.punchPlayFromEnabled
            measure: session.punchPlayFromMeasure
            focusPrefix: page.focusPrefix + ".playfrom"
            onToggled: session.punchPlayFromEnabled = !session.punchPlayFromEnabled
            onMeasureMoved: (value) => session.punchPlayFromMeasure = value
        }
        PunchRow {
            width: parent.width
            label: "PUNCH IN"
            enabledValue: session.punchInEnabled
            measure: session.punchInMeasure
            focusPrefix: page.focusPrefix + ".in"
            onToggled: session.punchInEnabled = !session.punchInEnabled
            onMeasureMoved: (value) => session.punchInMeasure = value
        }
        PunchRow {
            width: parent.width
            label: "PUNCH OUT"
            enabledValue: session.punchOutEnabled
            measure: session.punchOutMeasure
            focusPrefix: page.focusPrefix + ".out"
            onToggled: session.punchOutEnabled = !session.punchOutEnabled
            onMeasureMoved: (value) => session.punchOutMeasure = value
        }
    }

    component PunchRow: Row {
        property string label: ""
        property bool enabledValue: false
        property int measure: 1
        property string focusPrefix: ""
        signal toggled()
        signal measureMoved(int value)

        height: page.compact ? 50 : 64
        spacing: Style.gap

        TouchButton {
            width: 110
            height: parent.height
            text: parent.enabledValue ? "ON" : "OFF"
            textSize: Style.fontBody
            checked: parent.enabledValue
            focusId: parent.focusPrefix + ".enable"
            onClicked: parent.toggled()
        }
        Text {
            width: 130
            height: parent.height
            text: parent.label
            color: Style.text
            font.pixelSize: Style.fontTitle
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        // The measure is a tight numeric field: an arc-dial chip with the two
        // fine-step buttons beside it, not a stepper readout (refresh law).
        KnobChip {
            width: parent.width - 110 - 130 - 60 * 2 - Style.gap * 4
            height: parent.height
            label: "MEASURE"
            value: parent.measure
            valueText: parent.measure
            decimals: 0
            stepSize: 1
            minimumValue: 1
            dialScale: Math.max(16, parent.measure)
            popupLabel: "PUNCH MEASURE"
            popupKind: "generic"
            focusId: parent.focusPrefix + ".measure"
            onMoved: (value) => parent.measureMoved(Math.max(1, Math.round(value)))
        }
        TouchButton {
            width: 60
            height: parent.height
            text: "-"
            textSize: 22
            focusId: parent.focusPrefix + ".measure.dec"
            onClicked: parent.measureMoved(parent.measure - 1)
        }
        TouchButton {
            width: 60
            height: parent.height
            text: "+"
            textSize: 22
            focusId: parent.focusPrefix + ".measure.inc"
            onClicked: parent.measureMoved(parent.measure + 1)
        }
    }
}
