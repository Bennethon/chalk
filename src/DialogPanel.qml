import QtQuick

// Every dialog's panel: dark, a quiet border, and the corners Hyprland gives
// windows (sharp on Omarchy's default). From Omacut (MIT, David Heinemeier
// Hansson).
Rectangle {
    color: theme.dialog
    border.width: 1
    border.color: theme.dialogBorder
    radius: theme.radius
}
