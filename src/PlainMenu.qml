import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A menu drawn the way the app's dialogs are: the window's corners, a quiet
// border, compact rows.
Menu {
    topPadding: 6
    bottomPadding: 6
    delegate: MenuRow {}
    background: DialogPanel {
        implicitWidth: 260
    }
}
