import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A small square button holding an icon, or a few words when it has no
// icon. It never takes the keyboard: that stays on the board.
AbstractButton {
    id: control
    property string glyph: ""
    property string tip: ""
    property string keys: ""
    property bool active: false
    // A filled face, for a worded button that should look pressable at rest.
    property bool raised: false

    implicitWidth: glyph === "" ? label.implicitWidth + 20 : 32
    implicitHeight: 32
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    opacity: enabled ? 1 : 0.32

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: control.active ? theme.accent
             : control.down ? theme.pressed : control.hovered ? theme.hover : control.raised ? theme.button : "transparent"
    }
    contentItem: Item {
        GlyphIcon {
            anchors.centerIn: parent
            visible: control.glyph !== ""
            name: control.glyph
            color: control.active ? theme.accentForeground : theme.text
        }
        Label {
            id: label
            anchors.centerIn: parent
            visible: control.glyph === ""
            text: control.text
            color: control.active ? theme.accentForeground : theme.text
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 600
    ToolTip.text: keys === "" ? tip : tip + "   " + keys
}
