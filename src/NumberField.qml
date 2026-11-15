import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A compact number box with its label inside. Enter or leaving it applies
// the number, Up and Down step it, Escape puts the old one back. Numbers are
// read and written the way the Omarchy language writes them.
Rectangle {
    id: field
    property string label: ""
    property real value: 0
    // Shown empty: nothing to report, or the selection disagrees.
    property bool blank: false
    property real from: -100000
    property real to: 100000
    property real step: 1
    property int decimals: 0
    signal edited(real value)

    implicitWidth: 80
    implicitHeight: 28
    radius: Math.min(4, theme.radius)
    color: theme.field
    border.width: 1
    border.color: input.activeFocus ? theme.accent : theme.fieldBorder

    function shown() {
        if (blank)
            return ""
        var rounded = Number(value.toFixed(decimals))
        translator.language
        return rounded.toLocaleString(Qt.locale(), 'f', rounded % 1 === 0 ? 0 : decimals)
    }
    function sync() { input.text = shown() }
    function parse(text) {
        try {
            return Number.fromLocaleString(Qt.locale(), text)
        } catch (error) {
            return parseFloat(text)
        }
    }
    function apply(number) {
        if (isNaN(number)) {
            sync()
            return
        }
        number = Math.max(from, Math.min(to, number))
        if (blank || Math.abs(number - value) > 1e-9)
            field.edited(number)
        sync()
    }
    onValueChanged: if (!input.activeFocus) sync()
    onBlankChanged: if (!input.activeFocus) sync()
    Component.onCompleted: sync()
    Connections {
        target: translator
        function onChanged() { field.sync() }
    }

    Label {
        id: prefix
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: field.label
        color: theme.muted
        font.pixelSize: 11
    }
    TextInput {
        id: input
        anchors.left: prefix.right
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: TextInput.AlignRight
        color: theme.text
        selectionColor: theme.accent
        selectedTextColor: theme.accentForeground
        font.pixelSize: 12
        selectByMouse: true
        clip: true
        onActiveFocusChanged: if (activeFocus) selectAll(); else field.apply(field.parse(text))
        onAccepted: ApplicationWindow.window.focusBoard()
        Keys.onEscapePressed: {
            field.sync()
            ApplicationWindow.window.focusBoard()
        }
        Keys.onUpPressed: {
            field.apply((field.blank ? 0 : field.value) + field.step)
            selectAll()
        }
        Keys.onDownPressed: {
            field.apply((field.blank ? 0 : field.value) - field.step)
            selectAll()
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.IBeamCursor
    }
}
