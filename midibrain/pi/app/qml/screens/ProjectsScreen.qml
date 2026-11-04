import QtQuick
import Mnkybox 1.0
import "../components"

// PROJECTS primary: project list plus save/load. SHIFT: new/rename/delete and
// template operations (session v2 isTemplate).
Item {
    id: screen

    readonly property bool shiftFace: app.panelMode === "shift"

    Column {
        visible: !screen.shiftFace
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Row {
            width: parent.width
            height: Style.touch
            spacing: 4

            TouchButton {
                id: saveButton
                width: 100
                height: parent.height
                text: "SAVE"
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
            Text {
                width: parent.width - 210
                height: parent.height
                text: "SHIFT: manage + templates"
                color: Style.textDim
                font.pixelSize: Style.fontSmall
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                width: 100
                height: parent.height
                text: projects.isTemplate ? "TEMPLATE" : ""
                color: Style.accent
                font.pixelSize: Style.fontSmall
                font.bold: true
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            width: parent.width
            height: 18
            text: "project: " + projects.currentName
            color: Style.text
            font.pixelSize: Style.fontBody
            font.bold: true
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

        Item {
            width: parent.width
            height: Math.max(0, screen.height - Style.margin * 2 - Style.touch - 34 - 9)

            ListView {
                id: projectList
                anchors.fill: parent
                clip: true
                model: projects.model
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: ListView.view.width
                    height: Style.touch
                    color: index === projects.selectedRow ? Style.outline
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
                        font.bold: true
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
        spacing: 3

        Text {
            width: parent.width
            height: 16
            text: "SHIFT  PROJECT MANAGEMENT / TEMPLATES"
            color: Style.accent
            font.pixelSize: Style.fontSmall
            font.bold: true
        }

        Row {
            width: parent.width
            height: Style.touch
            spacing: 4

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
                idleColor: "#5b2222"
                focusId: "shift.projects.delete"
                onClicked: app.requestDeleteProject(projects.selectedRow)
            }
            Text {
                width: parent.width - 300
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
            spacing: 4

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
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }
}
