import QtQuick
import Mnkybox 1.0
import "../components"

// MIDI MONITOR primary screen: per-port note/CC/clock/other counters on the
// left, the last-N message log newest-first on the right. The data is the
// MidiGlue monitor ring, so it is what the Pi actually handled: TRS inputs from
// the Pico link, the USB input callback, routed outputs as the link thread
// flushes them and the Pi-side USB clock mirror. The Pico generates clock and
// transport for the TRS outputs itself and does not echo them back, so TRS OUT
// rows count routed messages only.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 2

        Row {
            width: parent.width
            height: parent.height - 12

            spacing: 6

            // ---- counters --------------------------------------------------
            Column {
                width: parent.width * 0.46
                height: parent.height
                spacing: 3

                Row {
                    width: parent.width
                    height: Style.touch
                    spacing: 4

                    Text {
                        width: parent.width - 192
                        height: parent.height
                        text: "PORTS  n / cc / clk / other"
                        color: Style.accent
                        font.pixelSize: Style.fontSmall
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: midimon.hideClock ? "NO CLK" : "CLK"
                        textSize: Style.fontSmall
                        checked: midimon.hideClock
                        focusId: "midimon.clock.filter"
                        onClicked: midimon.hideClock = !midimon.hideClock
                    }
                    TouchButton {
                        width: 92
                        height: parent.height
                        text: "RESET"
                        textSize: Style.fontSmall
                        focusId: "midimon.reset"
                        onClicked: midimon.reset()
                    }
                }

                ListView {
                    width: parent.width
                    height: parent.height - Style.touch - 3
                    clip: true
                    model: midimon.ports
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: Style.touch
                        color: index === midimon.selectedPort ? Style.outline : Style.panelDark
                        border.width: app.focusId === ("midimon.port." + index) ? 2
                                                                               : Style.outlineWidth
                        border.color: app.focusId === ("midimon.port." + index) ? Style.accent
                                                                                : Style.outline

                        Text {
                            x: 8
                            width: 78
                            height: parent.height
                            text: model.name
                            color: model.direction === "IN" ? Style.text : Style.textDim
                            font.pixelSize: Style.fontSmall
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 92
                            width: parent.width - 100
                            height: parent.height
                            text: "n " + model.notes + "  cc " + model.cc +
                                  "  clk " + model.clock + "  o " + model.other
                            color: (model.notes + model.cc + model.clock + model.other) > 0
                                   ? Style.text : Style.textDim
                            font.pixelSize: Style.fontSmall
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        TapHandler {
                            onTapped: {
                                midimon.selectedPort = index
                                app.showTextPopup("MIDI " + model.name,
                                                  midimon.portSummary(index))
                            }
                        }
                    }
                }
            }

            // ---- event log -------------------------------------------------
            Column {
                width: parent.width - parent.width * 0.46 - 6
                height: parent.height
                spacing: 3

                Text {
                    width: parent.width
                    height: Style.touch
                    text: midimon.headerText
                    color: Style.accent
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                ListView {
                    width: parent.width
                    height: parent.height - Style.touch - 3
                    clip: true
                    model: midimon.events
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 22
                        color: index % 2 === 0 ? Style.panel : Style.panelDark

                        Text {
                            x: 6
                            width: 84
                            height: parent.height
                            text: midimon.portName(model.port)
                            color: model.kind === "IN" ? Style.text : Style.textDim
                            font.pixelSize: 10
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 94
                            width: parent.width - 156
                            height: parent.height
                            text: model.text
                            color: Style.text
                            font.pixelSize: Style.fontSmall
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            width: 54
                            height: parent.height
                            text: model.age
                            color: Style.textDim
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width
            height: 12
            text: "Pi-side view - Pico-generated TRS OUT clock/transport is not echoed back over the link"
            color: Style.textDim
            font.pixelSize: 9
            elide: Text.ElideRight
        }
    }
}
