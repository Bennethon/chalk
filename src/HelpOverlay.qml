import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Every shortcut, over the dimmed window. Any click, ? or Escape closes it.
Rectangle {
    id: overlay
    anchors.fill: parent
    color: theme.helpScrim
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    signal dismissed()

    readonly property var sections: [
        { title: qsTr("Tools"), rows: [
            ["V", qsTr("Select and move")],
            ["R", qsTr("Rectangle")],
            ["O", qsTr("Ellipse")],
            ["D", qsTr("Diamond")],
            ["Y", qsTr("Triangle")],
            ["T", qsTr("Text")],
            ["L", qsTr("Line")],
            ["A", qsTr("Arrow")],
            ["P", qsTr("Pen: click for corners, drag for curves")],
            ["F", qsTr("Freehand pencil")],
            ["Enter", qsTr("Edit text or path points; finish a path")],
            ["Esc", qsTr("Back out, one step at a time")]
        ] },
        { title: qsTr("Edit"), rows: [
            ["Ctrl+Z", qsTr("Undo")],
            ["Ctrl+Shift+Z", qsTr("Redo")],
            ["Ctrl+C  Ctrl+X  Ctrl+V", qsTr("Copy, cut, paste")],
            ["Ctrl+D", qsTr("Duplicate")],
            ["Ctrl+A", qsTr("Select everything")],
            ["Delete", qsTr("Delete")],
            ["← ↑ → ↓", qsTr("Nudge (Shift for bigger steps)")],
            ["Shift", qsTr("Add to the selection; keep proportions; lock to an axis")],
            ["Ctrl", qsTr("Drag to copy; click to reach inside a group")],
            ["Alt", qsTr("Move or resize without snapping")],
            ["Ctrl+B  Ctrl+I", qsTr("Bold, italic")]
        ] },
        { title: qsTr("Arrange"), rows: [
            ["Ctrl+G", qsTr("Group")],
            ["Ctrl+Shift+G", qsTr("Ungroup")],
            ["Ctrl+]  Ctrl+[", qsTr("Bring forward, send backward")],
            ["Ctrl+Shift+]  Ctrl+Shift+[", qsTr("Bring to front, send to back")],
            ["Alt+A  Alt+H  Alt+D", qsTr("Align left, center, right")],
            ["Alt+W  Alt+V  Alt+S", qsTr("Align top, middle, bottom")],
            ["Alt+Shift+H  Alt+Shift+V", qsTr("Space out evenly across, down")]
        ] },
        { title: qsTr("Board"), rows: [
            ["Space", qsTr("Hold and drag to move around")],
            ["Ctrl+Scroll", qsTr("Zoom")],
            ["+  −", qsTr("Zoom in, out")],
            ["0", qsTr("Actual size")],
            ["1", qsTr("Fit everything")],
            ["Ctrl+N", qsTr("New board")],
            ["Ctrl+O", qsTr("Open")],
            ["Ctrl+S", qsTr("Save")],
            ["Ctrl+Shift+S", qsTr("Save as")],
            ["Ctrl+E", qsTr("Export PNG, SVG, PDF or JPG")],
            ["Ctrl+Shift+C", qsTr("Copy as an image")],
            ["Ctrl+Shift+L", qsTr("Switch between light and dark")],
            ["?", qsTr("Show these shortcuts")],
            ["Q", qsTr("Quit")]
        ] }
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.dismissed()
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 32
        contentWidth: width
        contentHeight: Math.max(height, grid.height)
        clip: true
        interactive: grid.height > height

        Grid {
            id: grid
            anchors.centerIn: parent
            columns: overlay.width > 1100 ? 4 : 2
            columnSpacing: 40
            rowSpacing: 28

            Repeater {
                model: overlay.sections
                Column {
                    required property var modelData
                    spacing: 7
                    width: 250
                    Label {
                        text: modelData.title
                        color: theme.accentInk
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                        bottomPadding: 4
                    }
                    Repeater {
                        model: modelData.rows
                        Column {
                            required property var modelData
                            width: 250
                            spacing: 1
                            Label {
                                text: modelData[0]
                                color: theme.strong
                                font.pixelSize: 12
                                font.family: "monospace"
                            }
                            Label {
                                width: parent.width
                                text: modelData[1]
                                color: theme.muted
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
