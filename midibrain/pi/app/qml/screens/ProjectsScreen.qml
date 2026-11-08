import QtQuick
import Mnkybox 1.0
import "../components"

// PROJECTS primary: project list plus save/load. SHIFT: new/rename/delete and
// template operations (session v2 isTemplate). The header carries the mnkybox
// wordmark; the list is the only primary content.
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                id: saveButton
                width: 100
                height: parent.height
                text: "SAVE"
                variant: "primary"
                focusId: "projects.save"
                onClicked: projects.saveProject()
            }
            Wordmark {
                width: 118
                height: parent.height
            }
            Text {
                width: parent.width - 100 - 118 - 100 - Style.gap * 3
                height: parent.height
                text: "tap loads  -  ... selects  -  SHIFT > manage"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Text {
                width: 100
                height: parent.height
                text: projects.isTemplate ? "TEMPLATE" : ""
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        // one dirty indicator, next to the thing it describes. The tab dot
        // and the transport dot are gone.
        Row {
            width: parent.width
            height: 18
            spacing: 6

            Text {
                width: Math.min(implicitWidth,
                                parent.width - (app.dirty ? 92 : 0))
                height: parent.height
                text: "project: " + projects.currentName
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Rectangle {
                visible: app.dirty
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: Style.accent
            }
            Text {
                visible: app.dirty
                anchors.verticalCenter: parent.verticalCenter
                text: "unsaved"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
            }
        }
        Text {
            width: parent.width
            height: 16
            text: projects.statusText
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }

        ListView {
            id: projectList
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 - Style.touch -
                                18 - 16 - Style.gap * 4)
            clip: true
            model: projects.model
            boundsBehavior: Flickable.StopAtBounds

            // the panel ring can move past the visible rows; keep the
            // focused project row in view instead of leaving focus off-screen.
            function syncFocus() {
                const id = app.focusId
                if (!id.startsWith("projects.row.")) return
                const row = parseInt(id.substring("projects.row.".length))
                if (!isNaN(row)) positionViewAtIndex(row, ListView.Contain)
            }
            Connections {
                target: app
                function onFocusChanged() { projectList.syncFocus() }
            }
            Component.onCompleted: syncFocus()

            delegate: Rectangle {
                width: ListView.view.width
                height: Style.touch
                property string focusId: "projects.row." + index
                radius: Style.corner
                color: index === projects.selectedRow ? Style.raised
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === focusId ? 2 : 0
                border.color: Style.text

                // Selection (the SHIFT verb subject) is a left bar; panel focus
                // is the border. They were both the accent border before.
                Rectangle {
                    visible: index === projects.selectedRow
                    width: 4
                    height: parent.height
                    radius: 2
                    color: Style.accent
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    width: parent.width / 2
                    height: parent.height
                    text: model.name
                    color: model.current ? Style.accent : Style.text
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    anchors.right: selectTarget.left
                    anchors.rightMargin: 6
                    width: parent.width / 2 - 50
                    height: parent.height
                    text: "session  -  " + model.modified + (model.current ? "  -  current" : "")
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                // the row body loads; "..." only selects, so RENAME/DELETE
                // can name a visible row without loading it first.
                TapHandler {
                    onTapped: app.requestLoadProject(index)
                }
                Rectangle {
                    id: selectTarget
                    width: Style.touch
                    height: Style.touch
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Style.corner
                    color: index === projects.selectedRow ? Style.raised : Style.panelDark
                    border.width: index === projects.selectedRow ? 2 : 0
                    border.color: Style.accent

                    Text {
                        anchors.centerIn: parent
                        text: "..."
                        color: Style.textDim
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                    }
                    TapHandler {
                        onTapped: app.selectProjectRow(index)
                    }
                }
            }

            Text {
                visible: projectList.count === 0
                anchors.centerIn: parent
                width: parent.width - 20
                text: "no projects yet - open SHIFT > and tap NEW"
                color: Style.textDim
                font.pixelSize: Style.fontBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }

    Column {
        visible: screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: Style.gap

        Row {
            width: parent.width
            height: 20
            spacing: Style.gap
            Text {
                width: parent.width - 118 - Style.gap
                height: parent.height
                text: "PROJECTS  -  manage + templates"
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Wordmark {
                width: 118
                height: parent.height
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: 92
                height: parent.height
                text: "NEW"
                focusId: "shift.projects.new"
                onClicked: projects.newProject()
            }
            TouchButton {
                width: 100
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                opacity: projects.selectedRow >= 0 ? 1.0 : 0.4
                focusId: "shift.projects.rename"
                onClicked: {
                    if (projects.selectedRow < 0) {
                        app.showTextPopup("RENAME PROJECT", "select a row first (...)")
                        return
                    }
                    app.requestRenameProject()
                }
            }
            TouchButton {
                width: 100
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                variant: "danger"
                opacity: projects.selectedRow >= 0 ? 1.0 : 0.4
                focusId: "shift.projects.delete"
                onClicked: {
                    if (projects.selectedRow < 0) {
                        app.showTextPopup("DELETE PROJECT", "select a row first (...)")
                        return
                    }
                    app.requestDeleteProject(projects.selectedRow)
                }
            }
            Text {
                width: parent.width - 300 - Style.gap * 3
                height: parent.height
                text: projects.isTemplate ? "current project is a template"
                                          : "current project is a normal session"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
            }
        }

        // the mirrored list names every row the destructive buttons can
        // act on (five rows, 44 px, dimmed 0.8, selected row in accent).
        ListView {
            id: shiftList
            width: parent.width
            height: Math.min(5, projects.model.count) * Style.touch
            clip: true
            model: projects.model
            boundsBehavior: Flickable.StopAtBounds
            opacity: 0.8

            delegate: Rectangle {
                width: ListView.view.width
                height: Style.touch
                color: "transparent"
                border.width: index === projects.selectedRow ? 2 : 0
                border.color: Style.accent

                Text {
                    x: 8
                    width: parent.width * 0.6
                    height: parent.height
                    text: model.name
                    color: index === projects.selectedRow ? Style.accent : Style.text
                    font.pixelSize: Style.fontBody
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width: parent.width * 0.36
                    height: parent.height
                    text: model.modified
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TapHandler {
                    onTapped: app.selectProjectRow(index)
                }
            }
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: Style.gap

            TouchButton {
                width: 150
                height: parent.height
                text: "NEW TEMPLATE"
                textSize: Style.fontSmall
                focusId: "shift.projects.template.new"
                onClicked: projects.newTemplate()
            }
            TouchButton {
                width: 150
                height: parent.height
                text: "SAVE TEMPLATE"
                textSize: Style.fontSmall
                focusId: "shift.projects.template.save"
                onClicked: projects.saveTemplate()
            }
            TouchButton {
                width: 150
                height: parent.height
                text: "CLEAR FLAG"
                textSize: Style.fontSmall
                focusId: "shift.projects.template.clear"
                onClicked: projects.clearTemplate()
            }
        }

    }
}
