import QtQuick
import Mnkybox 1.0
import "../components"

// Projects: one directory per project under ~/Music/mnkybox-projects. Rows load
// a project (after a dirty confirmation), and NEW/SAVE/RENAME/DELETE manage the
// folder. The dirty dot appears on SAVE, on the Projects tab and in the
// transport strip.
Item {
    id: screen

    Column {
        anchors.fill: parent
        anchors.margins: Style.margin
        spacing: 3

        Item {
            width: parent.width
            height: Style.touch

            TouchButton {
                width: 88
                height: parent.height
                text: "NEW"
                focusId: "projects.new"
                onClicked: projects.newProject()
            }
            TouchButton {
                id: saveButton
                x: 94
                width: 88
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
            TouchButton {
                x: 188
                width: 100
                height: parent.height
                text: "RENAME"
                textSize: Style.fontSmall
                active: projects.selectedRow >= 0
                focusId: "projects.rename"
                onClicked: app.requestRenameProject()
            }
            TouchButton {
                x: 294
                width: 100
                height: parent.height
                text: "DELETE"
                textSize: Style.fontSmall
                active: projects.selectedRow >= 0
                idleColor: "#5b2222"
                focusId: "projects.delete"
                onClicked: app.requestDeleteProject(projects.selectedRow)
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
                text: "no projects yet - NEW creates one in ~/Music/mnkybox-projects"
                color: Style.textDim
                font.pixelSize: Style.fontBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
