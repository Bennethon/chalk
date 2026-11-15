import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The board's own actions, always in reach of the pointer: files, undo and
// redo, the zoom, light or dark, and the shortcuts.
Rectangle {
    id: bar
    height: 44
    color: theme.panel

    // The CanvasView, for the zoom.
    property var view
    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal exportRequested()
    signal modeRequested()
    signal helpRequested()

    component Divider: Item {
        width: 13
        height: 32
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 18
            color: theme.border
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme.border
    }

    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        IconButton { text: qsTr("New"); tip: qsTr("New board"); keys: "Ctrl+N"; onClicked: bar.newRequested() }
        IconButton { text: qsTr("Open"); tip: qsTr("Open a board"); keys: "Ctrl+O"; onClicked: bar.openRequested() }
        IconButton { text: qsTr("Save"); tip: qsTr("Save this board"); keys: "Ctrl+S"; onClicked: bar.saveRequested() }
        IconButton { text: qsTr("Save as"); tip: qsTr("Save under another name"); keys: "Ctrl+Shift+S"; onClicked: bar.saveAsRequested() }
        IconButton { text: qsTr("Export"); tip: qsTr("Export PNG, SVG, PDF or JPG"); keys: "Ctrl+E"; onClicked: bar.exportRequested() }
        Divider {}
        IconButton { glyph: "undo"; tip: qsTr("Undo"); keys: "Ctrl+Z"; enabled: backend.canUndo; onClicked: backend.undo() }
        IconButton { glyph: "redo"; tip: qsTr("Redo"); keys: "Ctrl+Shift+Z"; enabled: backend.canRedo; onClicked: backend.redo() }
    }

    Label {
        anchors.centerIn: parent
        width: Math.max(0, bar.width - 2 * Math.max(left.width, right.width) - 48)
        horizontalAlignment: Text.AlignHCenter
        text: (backend.dirty ? "● " : "") + backend.fileName
        color: theme.label
        font.pixelSize: 13
        elide: Text.ElideMiddle
    }

    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        IconButton { glyph: "minus"; tip: qsTr("Zoom out"); keys: "−"; onClicked: bar.view.zoomBy(0.8) }
        IconButton {
            implicitWidth: 56
            tip: qsTr("Actual size")
            keys: "0"
            text: {
                translator.language
                return qsTr("%1%").arg(Math.round((bar.view ? bar.view.zoom : 1) * 100).toLocaleString(Qt.locale(), 'f', 0))
            }
            onClicked: bar.view.resetZoom()
        }
        IconButton { glyph: "plus"; tip: qsTr("Zoom in"); keys: "+"; onClicked: bar.view.zoomBy(1.25) }
        IconButton { glyph: "fit"; tip: qsTr("Fit everything"); keys: "1"; onClicked: bar.view.fitContent() }
        Divider {}
        IconButton {
            glyph: theme.dark ? "sun" : "moon"
            tip: theme.dark ? qsTr("Switch to light mode") : qsTr("Switch to dark mode")
            keys: "Ctrl+Shift+L"
            onClicked: bar.modeRequested()
        }
        IconButton { text: "?"; implicitWidth: 32; tip: qsTr("Shortcuts"); keys: "?"; onClicked: bar.helpRequested() }
    }
}
