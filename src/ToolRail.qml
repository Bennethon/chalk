import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The tools, down the side of the board.
Rectangle {
    id: rail
    width: 48
    color: theme.panel

    // Each tool's name as the backend knows it, its shortcut, and what to
    // call it. Main.qml binds the shortcuts from this same list.
    readonly property var tools: [
        { name: "select", key: "V", label: qsTr("Select") },
        { name: "rect", key: "R", label: qsTr("Rectangle") },
        { name: "ellipse", key: "O", label: qsTr("Ellipse") },
        { name: "diamond", key: "D", label: qsTr("Diamond") },
        { name: "triangle", key: "Y", label: qsTr("Triangle") },
        { name: "text", key: "T", label: qsTr("Text") },
        { name: "line", key: "L", label: qsTr("Line") },
        { name: "arrow", key: "A", label: qsTr("Arrow") },
        { name: "pen", key: "P", label: qsTr("Pen") },
        { name: "pencil", key: "F", label: qsTr("Pencil") }
    ]

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: theme.border
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2
        Repeater {
            model: rail.tools
            IconButton {
                required property var modelData
                glyph: modelData.name
                tip: modelData.label
                keys: modelData.key
                active: backend.tool === modelData.name
                onClicked: backend.tool = modelData.name
            }
        }
    }
}
