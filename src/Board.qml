import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Chalk

// The board and everything that happens on it with the pointer. It turns
// presses, drags and scrolls into board coordinates for the backend, moves
// the view around, and lays the text being typed over the shape it's in.
Item {
    id: board
    focus: true
    clip: true

    property bool spaceHeld: false
    property bool panning: false
    property point last
    property point pointer: Qt.point(width / 2, height / 2)
    property bool pointerInside: false
    readonly property alias view: canvas
    signal exportRequested()

    // Where something new should land: under the pointer when it's over
    // the board, in the middle of what's showing otherwise.
    function insertPoint() {
        var p = pointerInside ? pointer : Qt.point(width / 2, height / 2)
        return canvas.toScene(p.x, p.y)
    }
    function commitText() { typed.commit() }

    // Holding Space turns the pointer into a hand that moves the board.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space) {
            if (!event.isAutoRepeat)
                spaceHeld = true
            event.accepted = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceHeld = false
            event.accepted = true
        }
    }
    onActiveFocusChanged: if (!activeFocus) spaceHeld = false

    CanvasView {
        id: canvas
        anchors.fill: parent
        editor: backend
        accent: theme.accent
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: board.panning ? Qt.ClosedHandCursor : board.spaceHeld ? Qt.OpenHandCursor : backend.cursor

        onPressed: (m) => {
            typed.commit()
            board.forceActiveFocus()
            var s = canvas.toScene(m.x, m.y)
            if (m.button === Qt.RightButton) {
                // What's under the pointer becomes what the menu acts on.
                backend.contextAt(s.x, s.y, canvas.zoom)
                menu.at = s
                menu.popup(m.x, m.y)
                return
            }
            if (m.button === Qt.MiddleButton || board.spaceHeld) {
                board.panning = true
                board.last = Qt.point(m.x, m.y)
                return
            }
            backend.press(s.x, s.y, m.modifiers, canvas.zoom)
        }
        onPositionChanged: (m) => {
            board.pointer = Qt.point(m.x, m.y)
            if (board.panning) {
                canvas.panBy(m.x - board.last.x, m.y - board.last.y)
                board.last = Qt.point(m.x, m.y)
                return
            }
            var s = canvas.toScene(m.x, m.y)
            if (pressedButtons & Qt.RightButton)
                return
            if (pressed)
                backend.move(s.x, s.y, m.modifiers, canvas.zoom)
            else
                backend.hover(s.x, s.y, canvas.zoom)
        }
        onReleased: (m) => {
            if (m.button === Qt.RightButton)
                return
            if (board.panning) {
                board.panning = false
                return
            }
            var s = canvas.toScene(m.x, m.y)
            backend.release(s.x, s.y, m.modifiers, canvas.zoom)
        }
        onDoubleClicked: (m) => {
            if (m.button !== Qt.LeftButton || board.spaceHeld)
                return
            var s = canvas.toScene(m.x, m.y)
            backend.doubleClick(s.x, s.y, m.modifiers, canvas.zoom)
        }
        onEntered: board.pointerInside = true
        onExited: board.pointerInside = false
        onWheel: (w) => {
            if (w.modifiers & Qt.ControlModifier) {
                canvas.zoomAt(w.x, w.y, Math.pow(1.0018, w.angleDelta.y))
                return
            }
            // Touchpads report pixels; a wheel reports notches.
            var dx = w.pixelDelta.x !== 0 ? w.pixelDelta.x : w.angleDelta.x / 2
            var dy = w.pixelDelta.y !== 0 ? w.pixelDelta.y : w.angleDelta.y / 2
            if ((w.modifiers & Qt.ShiftModifier) && dx === 0) {
                dx = dy
                dy = 0
            }
            canvas.panBy(dx, dy)
        }
    }

    BoardMenu {
        id: menu
        onExportRequested: board.exportRequested()
        onClosed: board.forceActiveFocus()
    }

    PinchHandler {
        target: null
        onScaleChanged: (delta) => canvas.zoomAt(centroid.position.x, centroid.position.y, delta)
    }

    // The text being typed, laid over the board at the board's own scale so
    // it wraps exactly where the finished text will.
    Item {
        id: typed
        anchors.fill: parent
        visible: backend.editingId !== ""
        property string activeId: ""
        readonly property var e: backend.editing
        readonly property bool box: e.mode === "box"
        readonly property bool label: e.mode === "label"

        function commit() {
            if (backend.editingId !== "")
                backend.commitText(field.text)
        }

        Connections {
            target: backend
            function onEditingChanged() {
                if (backend.editingId === typed.activeId)
                    return
                typed.activeId = backend.editingId
                if (backend.editingId !== "") {
                    field.text = backend.editing.text
                    field.forceActiveFocus()
                    field.selectAll()
                } else {
                    board.forceActiveFocus()
                }
            }
        }

        TextEdit {
            id: field
            transformOrigin: Item.TopLeft
            scale: canvas.zoom
            // Loose text has no size of its own: undefined hands the width
            // and height back to what the words need.
            width: typed.box ? typed.e.w : undefined
            height: typed.box ? typed.e.h : undefined
            // A box is typed into where it stands; loose text grows from
            // its anchor, the way its alignment says.
            x: (typed.e.x - canvas.originX) * canvas.zoom
               - (typed.box ? 0 : typed.e.hAlign === 1 ? width * scale / 2 : typed.e.hAlign === 2 ? width * scale : 0)
            y: (typed.e.y - canvas.originY) * canvas.zoom - (typed.label ? height * scale / 2 : 0)

            color: typed.e.color
            selectionColor: theme.accent
            selectedTextColor: theme.accentForeground
            font.family: typed.e.family
            font.pixelSize: typed.e.fontSize
            font.bold: typed.e.bold
            font.italic: typed.e.italic
            font.hintingPreference: Font.PreferNoHinting
            textFormat: TextEdit.PlainText
            wrapMode: typed.box ? TextEdit.WordWrap : TextEdit.NoWrap
            horizontalAlignment: typed.e.hAlign === 0 ? TextEdit.AlignLeft
                               : typed.e.hAlign === 2 ? TextEdit.AlignRight : TextEdit.AlignHCenter
            verticalAlignment: !typed.box || typed.e.vAlign === 0 ? TextEdit.AlignTop
                             : typed.e.vAlign === 2 ? TextEdit.AlignBottom : TextEdit.AlignVCenter
            selectByMouse: true
            cursorVisible: activeFocus

            Keys.onEscapePressed: typed.commit()
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    typed.commit()
                    event.accepted = true
                }
            }
            onActiveFocusChanged: if (!activeFocus) typed.commit()

            // A line's label clears a patch of paper behind what's typed.
            Rectangle {
                z: -1
                anchors.fill: parent
                anchors.leftMargin: -6
                anchors.rightMargin: -6
                anchors.topMargin: -3
                anchors.bottomMargin: -3
                visible: typed.label
                color: typed.e.paper
            }
        }
    }

    // --- What the board has to say ---

    Label {
        anchors.centerIn: parent
        visible: backend.empty && backend.tool === "select" && backend.editingId === ""
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Press R for a rectangle, T for text, L for a line.\nDouble-click anywhere to type. Press ? for every shortcut.")
        color: backend.paper === "light" ? "#8a8a93" : "#6a6a72"
        font.pixelSize: 14
        lineHeight: 1.5
    }

    // What the tool in hand expects next.
    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        visible: text !== "" && !toast.visible
        text: backend.penActive ? qsTr("Click to add points, drag to curve them. Enter finishes; click the first point to close.")
            : backend.nodeEditing ? qsTr("Drag points and handles. Double-click a point to curve or sharpen it, or the line to add one. Esc when done.")
            : backend.tool === "pen" ? qsTr("Click to start a path.")
            : backend.tool === "pencil" ? qsTr("Drag to draw. Esc when done.")
            : backend.tool === "line" || backend.tool === "arrow" ? qsTr("Drag from one shape to another to connect them.")
            : backend.tool === "text" ? qsTr("Click to type, or drag out a box.")
            : backend.tool !== "select" ? qsTr("Drag to draw, or click to place one.")
            : ""
        color: backend.paper === "light" ? "#66666e" : "#8a8a93"
        font.pixelSize: 12
    }

    // Results and failures, said in the window.
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: Math.min(toastLabel.implicitWidth + 28, parent.width - 40)
        height: toastLabel.implicitHeight + 16
        radius: Math.min(4, theme.radius)
        visible: backend.message !== ""
        color: backend.messageIsError ? theme.errorFill : theme.dialog
        border.width: 1
        border.color: backend.messageIsError ? "#e5484d" : theme.dialogBorder

        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.parent.width - 68)
            text: backend.message
            color: backend.messageIsError ? theme.errorText : theme.text
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        TapHandler { onTapped: backend.clearMessage() }
        // Good news fades by itself; a failure stays until it's been seen.
        Timer {
            interval: 3500
            running: toast.visible && !backend.messageIsError
            onTriggered: backend.clearMessage()
        }
    }
}
