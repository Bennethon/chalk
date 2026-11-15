import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A row of the right-click menu: what it does on one side, the keys that do
// the same on the other. A hidden row takes no room.
MenuItem {
    id: row
    property string keys: ""

    implicitWidth: 260
    implicitHeight: 32
    height: visible ? implicitHeight : 0
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0

    contentItem: Item {
        Label {
            anchors.left: parent.left
            anchors.right: hint.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: row.text
            color: theme.text
            opacity: row.enabled ? 1 : 0.38
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Label {
            id: hint
            anchors.right: parent.right
            anchors.rightMargin: row.subMenu ? 14 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: row.keys
            color: theme.muted
            opacity: row.enabled ? 1 : 0.38
            font.pixelSize: 12
        }
    }
    background: Rectangle {
        implicitWidth: 260
        implicitHeight: 32
        color: row.highlighted || row.hovered ? theme.hover : "transparent"
    }
}
