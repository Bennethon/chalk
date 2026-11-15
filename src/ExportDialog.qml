import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// What to export and how, over the dimmed window. Return exports, Escape
// (handled by the owner) backs out, and the save dialog follows.
Rectangle {
    id: dialog
    anchors.fill: parent
    color: theme.scrim
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    property string format: "png"
    property real scale: 2
    property bool background: true
    property bool selectionOnly: false
    signal accepted(var options)
    signal dismissed()

    onVisibleChanged: {
        if (!visible)
            return
        selectionOnly = backend.selectionCount > 0
        exportButton.forceActiveFocus()
    }
    function accept() {
        dialog.accepted({ format: format, scale: scale, background: background,
                          selectionOnly: selectionOnly && backend.selectionCount > 0 })
    }

    MouseArea {
        anchors.fill: parent
        onClicked: dialog.dismissed()
    }

    component Choice: IconButton {
        implicitHeight: 30
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: parent.active ? theme.accent : parent.hovered ? theme.buttonHover : theme.button
        }
    }
    component Caption: Label {
        width: 96
        height: 30
        verticalAlignment: Text.AlignVCenter
        color: theme.muted
        font.pixelSize: 13
    }

    DialogPanel {
        anchors.centerIn: parent
        width: column.width + 64
        height: column.height + 48

        MouseArea { anchors.fill: parent }

        Column {
            id: column
            anchors.centerIn: parent
            spacing: 12

            Label {
                text: qsTr("Export")
                color: theme.strong
                font.pixelSize: 16
                font.weight: Font.DemiBold
                bottomPadding: 4
            }
            Row {
                spacing: 6
                Caption { text: qsTr("Format") }
                Repeater {
                    model: ["png", "svg", "pdf", "jpg"]
                    Choice {
                        required property string modelData
                        text: modelData.toUpperCase()
                        active: dialog.format === modelData
                        onClicked: dialog.format = modelData
                    }
                }
            }
            Row {
                spacing: 6
                enabled: dialog.format === "png" || dialog.format === "jpg"
                opacity: enabled ? 1 : 0.35
                Caption { text: qsTr("Size") }
                Repeater {
                    model: [1, 2, 4]
                    Choice {
                        required property int modelData
                        text: modelData + "×"
                        active: dialog.scale === modelData
                        onClicked: dialog.scale = modelData
                    }
                }
            }
            Row {
                spacing: 6
                enabled: dialog.format !== "jpg"
                opacity: enabled ? 1 : 0.35
                Caption { text: qsTr("Background") }
                Choice {
                    text: qsTr("Paper")
                    active: dialog.background || dialog.format === "jpg"
                    onClicked: dialog.background = true
                }
                Choice {
                    text: qsTr("Transparent")
                    active: !dialog.background && dialog.format !== "jpg"
                    onClicked: dialog.background = false
                }
            }
            Row {
                spacing: 6
                enabled: backend.selectionCount > 0
                opacity: enabled ? 1 : 0.35
                Caption { text: qsTr("Include") }
                Choice {
                    text: qsTr("Whole board")
                    active: !dialog.selectionOnly || backend.selectionCount === 0
                    onClicked: dialog.selectionOnly = false
                }
                Choice {
                    text: qsTr("Selection")
                    active: dialog.selectionOnly && backend.selectionCount > 0
                    onClicked: dialog.selectionOnly = true
                }
            }
            Row {
                anchors.right: parent.right
                spacing: 10
                topPadding: 8
                DialogButton {
                    id: cancelButton
                    text: qsTr("Cancel")
                    mnemonic: qsTr("C", "the letter that presses Cancel")
                    onClicked: dialog.dismissed()
                    Keys.onLeftPressed: exportButton.forceActiveFocus()
                    Keys.onRightPressed: exportButton.forceActiveFocus()
                    Keys.onTabPressed: exportButton.forceActiveFocus()
                    Keys.onBacktabPressed: exportButton.forceActiveFocus()
                }
                DialogButton {
                    id: exportButton
                    text: qsTr("Export")
                    mnemonic: qsTr("E", "the letter that presses Export")
                    primary: true
                    onClicked: dialog.accept()
                    Keys.onLeftPressed: cancelButton.forceActiveFocus()
                    Keys.onRightPressed: cancelButton.forceActiveFocus()
                    Keys.onTabPressed: cancelButton.forceActiveFocus()
                    Keys.onBacktabPressed: cancelButton.forceActiveFocus()
                }
            }
        }
    }
}
