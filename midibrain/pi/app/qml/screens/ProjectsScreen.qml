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
            Rectangle {
                visible: app.dirty
                x: saveButton.x + saveButton.width - 13
                y: 5
                width: 8
                height: 8
                radius: 4
                color: Style.accent
            }
            Wordmark {
                width: 118
                height: parent.height
            }
            Text {
                width: parent.width - 100 - 118 - 100 - Style.gap * 3
                height: parent.height
                text: "SHIFT: manage + templates"
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

        Text {
            width: parent.width
            height: 18
            text: "project: " + projects.currentName
            color: Style.text
            font.pixelSize: Style.fontTitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
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

            delegate: Rectangle {
                width: ListView.view.width
                height: Style.touch
                radius: Style.corner
                color: index === projects.selectedRow ? Style.raised
                       : (index % 2 === 0 ? Style.panel : Style.panelDark)
                border.width: app.focusId === ("projects.row." + index) ? 2 : 0
                border.color: Style.accent

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
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width: parent.width / 2
                    height: parent.height
                    text: "session  -  " + model.modified + (model.current ? "  -  current" : "")
                    color: Style.textDim
                    font.pixelSize: Style.fontSmall
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TapHandler {
                    onTapped: app.requestLoadProject(index)
                }
            }

            Text {
                visible: projectList.count === 0
                anchors.centerIn: parent
                width: parent.width - 20
                text: "no projects yet - SHIFT/NEW creates one in ~/Music/mnkybox-projects"
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
            height: 28
            spacing: Style.gap
            Text {
                width: parent.width - 118 - Style.gap
                height: parent.height
                text: "SHIFT  PROJECT MANAGEMENT / TEMPLATES"
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
                active: projects.selectedRow >= 0
                focusId: "shift.projects.rename"
                onClicked: app.requestRenameProject()
            }
            TouchButton {
                width: 100
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                active: projects.selectedRow >= 0
                variant: "danger"
                focusId: "shift.projects.delete"
                onClicked: app.requestDeleteProject(projects.selectedRow)
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

        Text {
            width: parent.width
            text: "Templates are settings-only session v2 files (isTemplate). " +
                  "Loading a template keeps the current take registry unless the " +
                  "template file carries one."
            color: Style.textDim
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
