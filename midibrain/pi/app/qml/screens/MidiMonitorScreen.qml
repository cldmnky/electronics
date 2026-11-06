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
        spacing: Style.gapTight

        Row {
            width: parent.width
            height: parent.height - 14 - Style.gapTight

            spacing: Style.gap

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
                        font.weight: Font.DemiBold
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
                    height: parent.height - Style.touch - Style.gap
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
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 92
                            width: parent.width - 100
                            height: parent.height
                            text: model.available
                                  ? "n " + model.notes + "  cc " + model.cc +
                                    "  clk " + model.clock + "  o " + model.other
                                  : "UNAVAILABLE - NO PICO (Pico link disabled)"
                            color: model.available &&
                                   (model.notes + model.cc + model.clock + model.other) > 0
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
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Item {
                    width: parent.width
                    height: parent.height - Style.touch - Style.gap

                ListView {
                    id: eventList
                    anchors.fill: parent
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
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
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
                            font.pixelSize: Style.fontSmall
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Text {
                    visible: eventList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    text: midimon.hideClock
                          ? "clock messages hidden - tap NO CLK to show them"
                          : "no MIDI yet - play a note or move a control"
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignHCenter
                }
                }
            }
        }

        Text {
            width: parent.width
            height: 14
            text: midimon.noPicoMode
                  ? "NO PICO: TRS IN/OUT and their clock are unavailable; USB MIDI and the Pi clock mirror are live"
                  : "Pi-side view - Pico-generated TRS OUT clock/transport is not echoed back over the link"
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }
    }
}
