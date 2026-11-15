import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQml.Models

ApplicationWindow {
    id: win
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: qsTr("%1 — Chalk").arg((backend.dirty ? "● " : "") + backend.fileName)
    color: theme.window

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool exportVisible: false
    // What is waiting on "save, discard or cancel?": quit, new or open.
    property string confirming: ""
    // What to carry on with once a save that was asked for has landed.
    property string afterSave: ""
    // Set once quitting has been settled, so the close that follows isn't
    // asked about a second time.
    property bool quitting: false

    readonly property bool overlayOpen: helpVisible || exportVisible || confirming !== ""
    // Typing into the board's text or one of the panel's fields: single
    // keys are letters then, not tools.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    // Shortcuts that act on the board go quiet under a dialog and while typing.
    readonly property bool live: !overlayOpen && !typing

    function focusBoard() { board.forceActiveFocus() }

    // Anything that would throw away the board asks first.
    function guarded(action) {
        board.commitText()
        if (backend.dirty)
            confirming = action
        else
            run(action)
    }
    function run(action) {
        if (action === "quit") {
            // Quitting closes the window, and closing the window is where
            // unsaved work gets asked about; this quit already was.
            quitting = true
            Qt.quit()
        }
        else if (action === "new")
            backend.newDocument()
        else if (action === "open")
            backend.chooseOpen()
    }
    function insertPoint() { return board.insertPoint() }

    onClosing: (close) => {
        if (quitting)
            return
        board.commitText()
        if (backend.dirty) {
            close.accepted = false
            confirming = "quit"
        }
    }

    // Light or dark for the whole app: the window, and the paper of the
    // board in hand with it.
    function toggleMode() {
        theme.dark = !theme.dark
        backend.paper = theme.dark ? "dark" : "light"
    }
    function save() {
        board.commitText()
        backend.save()
    }
    function saveAs() {
        board.commitText()
        backend.chooseSave()
    }
    function showExport() {
        board.commitText()
        exportVisible = true
    }
    Binding {
        target: backend
        property: "defaultPaper"
        value: theme.dark ? "dark" : "light"
    }

    Connections {
        target: backend
        function onSaved() {
            var action = win.afterSave
            win.afterSave = ""
            if (action !== "")
                win.run(action)
        }
        function onSaveAbandoned() { win.afterSave = "" }
    }

    // --- Shortcuts ---

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.exportVisible && win.confirming === ""; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: win.guarded("quit") }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        // The text being typed and the panel's fields take their own Escape.
        enabled: !win.typing
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.exportVisible) win.exportVisible = false
            else if (win.confirming !== "") win.confirming = ""
            else backend.escape()
            win.focusBoard()
        }
    }
    Shortcut { sequences: ["Return", "Enter", "F2"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.enter() }

    Instantiator {
        model: rail.tools
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.key
            context: Qt.ApplicationShortcut
            enabled: win.live
            onActivated: backend.tool = modelData.name
        }
    }

    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.redo() }
    Shortcut { sequences: [StandardKey.Copy]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.copy() }
    Shortcut { sequences: [StandardKey.Cut]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.cut() }
    Shortcut {
        sequences: [StandardKey.Paste]
        context: Qt.ApplicationShortcut
        enabled: win.live
        onActivated: {
            var p = win.insertPoint()
            backend.paste(p.x, p.y)
        }
    }
    Shortcut { sequences: [StandardKey.SelectAll]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.selectAll() }
    Shortcut { sequence: "Ctrl+D"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.duplicate() }
    Shortcut { sequences: ["Delete", "Backspace"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.deleteSelection() }

    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(-1, 0) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(1, 0) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(0, -1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(0, 1) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(-10, 0) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(10, 0) }
    Shortcut { sequence: "Shift+Up"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(0, -10) }
    Shortcut { sequence: "Shift+Down"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.nudge(0, 10) }

    Shortcut { sequence: "Ctrl+G"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.group() }
    Shortcut { sequence: "Ctrl+Shift+G"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.ungroup() }
    Shortcut { sequence: "Ctrl+]"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.order("forward") }
    Shortcut { sequence: "Ctrl+["; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.order("backward") }
    // With Shift held the bracket keys arrive as braces on most layouts.
    Shortcut { sequences: ["Ctrl+Shift+]", "Ctrl+}", "Ctrl+Shift+}"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.order("front") }
    Shortcut { sequences: ["Ctrl+Shift+[", "Ctrl+{", "Ctrl+Shift+{"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.order("back") }

    Shortcut { sequence: "Alt+A"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("left") }
    Shortcut { sequence: "Alt+H"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("hcenter") }
    Shortcut { sequence: "Alt+D"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("right") }
    Shortcut { sequence: "Alt+W"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("top") }
    Shortcut { sequence: "Alt+V"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("vcenter") }
    Shortcut { sequence: "Alt+S"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.align("bottom") }
    Shortcut { sequence: "Alt+Shift+H"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.distribute("h") }
    Shortcut { sequence: "Alt+Shift+V"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.distribute("v") }

    // Bold and italic work while typing too, on the text being typed.
    Shortcut { sequence: "Ctrl+B"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: backend.setStyle("bold", !backend.style.bold) }
    Shortcut { sequence: "Ctrl+I"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: backend.setStyle("italic", !backend.style.italic) }

    Shortcut { sequences: ["+", "=", "Ctrl++", "Ctrl+="]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: board.view.zoomBy(1.25) }
    Shortcut { sequences: ["-", "Ctrl+-"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: board.view.zoomBy(0.8) }
    Shortcut { sequences: ["0", "Ctrl+0"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: board.view.resetZoom() }
    Shortcut { sequences: ["1", "Ctrl+1"]; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: board.view.fitContent() }

    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.guarded("new") }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.guarded("open") }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.saveAs() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.showExport() }
    Shortcut { sequence: "Ctrl+Shift+L"; context: Qt.ApplicationShortcut; enabled: !win.overlayOpen; onActivated: win.toggleMode() }
    Shortcut { sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut; enabled: win.live; onActivated: backend.copyImage() }

    // --- The window ---

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction. The board itself is never mirrored:
    // a drawing reads the same in every language.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        TopBar {
            id: bar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            view: board.view
            onNewRequested: win.guarded("new")
            onOpenRequested: win.guarded("open")
            onSaveRequested: win.save()
            onSaveAsRequested: win.saveAs()
            onExportRequested: win.showExport()
            onModeRequested: win.toggleMode()
            onHelpRequested: win.helpVisible = true
        }
        ToolRail {
            id: rail
            anchors.left: parent.left
            anchors.top: bar.bottom
            anchors.bottom: parent.bottom
        }
        StylePanel {
            id: panel
            anchors.right: parent.right
            anchors.top: bar.bottom
            anchors.bottom: parent.bottom
        }
        Board {
            id: board
            anchors.left: rail.right
            anchors.right: panel.left
            anchors.top: bar.bottom
            anchors.bottom: parent.bottom
            onExportRequested: win.showExport()
        }
    }

    HelpOverlay {
        visible: win.helpVisible
        onDismissed: win.helpVisible = false
    }

    ExportDialog {
        visible: win.exportVisible
        onDismissed: {
            win.exportVisible = false
            win.focusBoard()
        }
        onAccepted: (options) => {
            win.exportVisible = false
            win.focusBoard()
            backend.chooseExport(options)
        }
    }

    ConfirmDialog {
        visible: win.confirming !== ""
        title: qsTr("Save changes to %1?").arg(backend.fileName)
        message: qsTr("This board has changes that haven't been saved.")
        buttons: [
            { text: qsTr("Cancel"), mnemonic: qsTr("C", "the letter that presses Cancel") },
            { text: qsTr("Discard"), mnemonic: qsTr("D", "the letter that presses Discard") },
            { text: qsTr("Save"), mnemonic: qsTr("S", "the letter that presses Save"), primary: true }
        ]
        initialFocus: 2
        onDismissed: {
            win.confirming = ""
            win.focusBoard()
        }
        onChosen: (index) => {
            var action = win.confirming
            win.confirming = ""
            win.focusBoard()
            if (index === 1) {
                win.run(action)
            } else if (index === 2) {
                win.afterSave = action
                backend.save()
            }
        }
    }
}
