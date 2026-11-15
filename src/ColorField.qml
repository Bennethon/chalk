import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A swatch that opens a small palette. Besides plain colors it offers "ink"
// and "paper", which follow the board's paper, and no color at all.
Item {
    id: field
    property string value: "none"
    property string tip: ""
    // Also offer see-through tints, which suit fills.
    property bool tints: false
    signal picked(string value)

    implicitWidth: 32
    implicitHeight: 28

    readonly property var hues: ["#e5484d", "#f76b15", "#f5c400", "#30a46c", "#12a594", "#3e8ef7", "#8e4ec6", "#e93d82"]

    component Swatch: Rectangle {
        id: swatch
        property string name: "none"
        property bool current: false
        width: 22
        height: 22
        radius: Math.min(4, theme.radius)
        color: {
            backend.paper
            return backend.colorOf(name)
        }
        border.width: current ? 2 : 1
        border.color: current ? theme.accent : theme.fieldBorder
        GlyphIcon {
            anchors.fill: parent
            anchors.margins: 2
            visible: swatch.name === "none"
            name: "none"
            color: "#e5484d"
        }
    }

    AbstractButton {
        id: button
        anchors.fill: parent
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onClicked: popup.opened ? popup.close() : popup.open()
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: theme.field
            border.width: 1
            border.color: popup.opened ? theme.accent : theme.fieldBorder
        }
        contentItem: Item {
            Swatch {
                anchors.centerIn: parent
                width: 20
                height: 16
                name: field.value
            }
        }
        ToolTip.visible: hovered && field.tip !== "" && !popup.opened
        ToolTip.delay: 600
        ToolTip.text: field.tip
    }

    Popup {
        id: popup
        // Opens beside the panel, over the board, so it never covers the
        // controls it belongs to.
        x: mirrored ? field.width + 10 : -width - 10
        y: -8
        padding: 12
        focus: true
        onAboutToShow: hex.text = field.value
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: DialogPanel {}
        onClosed: ApplicationWindow.window.focusBoard()

        contentItem: Column {
            spacing: 8
            Row {
                spacing: 6
                Repeater {
                    model: ["none", "ink", "paper", "#8b8d98"]
                    Swatch {
                        required property string modelData
                        name: modelData
                        current: field.value === modelData
                        TapHandler { onTapped: field.choose(parent.name) }
                    }
                }
            }
            Row {
                spacing: 6
                Repeater {
                    model: field.hues
                    Swatch {
                        required property string modelData
                        name: modelData
                        current: field.value === modelData
                        TapHandler { onTapped: field.choose(parent.name) }
                    }
                }
            }
            Row {
                spacing: 6
                visible: field.tints
                Repeater {
                    model: field.hues
                    Swatch {
                        required property string modelData
                        name: "#40" + modelData.slice(1)
                        current: field.value === name
                        TapHandler { onTapped: field.choose(parent.name) }
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: 28
                radius: Math.min(4, theme.radius)
                color: theme.field
                border.width: 1
                border.color: hex.activeFocus ? theme.accent : theme.fieldBorder
                TextInput {
                    id: hex
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.text
                    selectionColor: theme.accent
                    selectedTextColor: theme.accentForeground
                    font.pixelSize: 12
                    font.family: "monospace"
                    selectByMouse: true
                    text: field.value
                    onAccepted: {
                        var typed = text.trim().toLowerCase()
                        if (/^[0-9a-f]{6}([0-9a-f]{2})?$/.test(typed))
                            typed = "#" + typed
                        if (/^#[0-9a-f]{6}([0-9a-f]{2})?$/.test(typed) || typed === "none" || typed === "ink" || typed === "paper")
                            field.choose(typed)
                        else
                            text = field.value
                    }
                }
            }
        }
    }

    readonly property bool mirrored: Qt.application.layoutDirection === Qt.RightToLeft
    function choose(name) {
        field.picked(name)
        popup.close()
    }
}
