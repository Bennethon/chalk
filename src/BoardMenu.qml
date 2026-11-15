import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The right-click menu: everything that can be done to what's under the
// pointer, or to the board when nothing is.
PlainMenu {
    id: menu
    // Where on the board the click landed, which is where a paste goes.
    property point at
    signal exportRequested()

    readonly property var s: backend.style
    readonly property bool has: backend.selectionCount > 0

    component Rule: MenuSeparator {
        height: visible ? implicitHeight : 0
        topPadding: 5
        bottomPadding: 5
        contentItem: Rectangle {
            implicitWidth: 260
            implicitHeight: 1
            color: theme.dialogBorder
        }
    }

    MenuRow { text: qsTr("Edit text"); keys: "Enter"; visible: menu.s.single === "shape" || menu.s.single === "line"; onTriggered: backend.enter() }
    MenuRow { text: qsTr("Edit points"); keys: "Enter"; visible: menu.s.single === "path"; onTriggered: backend.enter() }
    Rule { visible: menu.s.single !== "" }

    MenuRow { text: qsTr("Cut"); keys: "Ctrl+X"; visible: menu.has; onTriggered: backend.cut() }
    MenuRow { text: qsTr("Copy"); keys: "Ctrl+C"; visible: menu.has; onTriggered: backend.copy() }
    MenuRow { text: qsTr("Paste"); keys: "Ctrl+V"; onTriggered: backend.paste(menu.at.x, menu.at.y) }
    MenuRow { text: qsTr("Duplicate"); keys: "Ctrl+D"; visible: menu.has; onTriggered: backend.duplicate() }
    MenuRow { text: qsTr("Delete"); keys: "Delete"; visible: menu.has; onTriggered: backend.deleteSelection() }

    Rule { visible: menu.has }
    MenuRow { text: qsTr("Group"); keys: "Ctrl+G"; visible: menu.has; enabled: menu.s.canGroup; onTriggered: backend.group() }
    MenuRow { text: qsTr("Ungroup"); keys: "Ctrl+Shift+G"; visible: menu.has; enabled: menu.s.canUngroup; onTriggered: backend.ungroup() }

    Rule { visible: menu.has }
    PlainMenu {
        title: qsTr("Order")
        enabled: menu.has
        MenuRow { text: qsTr("Bring to front"); keys: "Ctrl+Shift+]"; onTriggered: backend.order("front") }
        MenuRow { text: qsTr("Bring forward"); keys: "Ctrl+]"; onTriggered: backend.order("forward") }
        MenuRow { text: qsTr("Send backward"); keys: "Ctrl+["; onTriggered: backend.order("backward") }
        MenuRow { text: qsTr("Send to back"); keys: "Ctrl+Shift+["; onTriggered: backend.order("back") }
    }
    PlainMenu {
        title: qsTr("Align")
        // Lining up takes at least two things to line up.
        enabled: menu.s.units >= 2
        MenuRow { text: qsTr("Align left"); keys: "Alt+A"; onTriggered: backend.align("left") }
        MenuRow { text: qsTr("Align center"); keys: "Alt+H"; onTriggered: backend.align("hcenter") }
        MenuRow { text: qsTr("Align right"); keys: "Alt+D"; onTriggered: backend.align("right") }
        Rule {}
        MenuRow { text: qsTr("Align top"); keys: "Alt+W"; onTriggered: backend.align("top") }
        MenuRow { text: qsTr("Align middle"); keys: "Alt+V"; onTriggered: backend.align("vcenter") }
        MenuRow { text: qsTr("Align bottom"); keys: "Alt+S"; onTriggered: backend.align("bottom") }
        Rule {}
        MenuRow { text: qsTr("Space out evenly across"); keys: "Alt+Shift+H"; enabled: menu.s.units >= 3; onTriggered: backend.distribute("h") }
        MenuRow { text: qsTr("Space out evenly down"); keys: "Alt+Shift+V"; enabled: menu.s.units >= 3; onTriggered: backend.distribute("v") }
    }

    Rule {}
    MenuRow { text: qsTr("Select everything"); keys: "Ctrl+A"; onTriggered: backend.selectAll() }
    MenuRow { text: qsTr("Copy as an image"); keys: "Ctrl+Shift+C"; enabled: !backend.empty; onTriggered: backend.copyImage() }
    MenuRow { text: qsTr("Export…"); keys: "Ctrl+E"; enabled: !backend.empty; onTriggered: menu.exportRequested() }
}
