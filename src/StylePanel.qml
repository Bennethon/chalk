import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Everything about what's selected: where it is, how it lines up with its
// neighbours, and how it looks. With nothing selected it sets the look of
// the next thing drawn, and holds the board's own settings.
Rectangle {
    id: panel
    width: 256
    color: theme.panel

    readonly property var s: backend.style
    readonly property bool has: backend.selectionCount > 0

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: theme.border
    }

    component Section: Column {
        property string title: ""
        // An inline component can't see the ids around it, so it sizes
        // itself to whatever column it's put in.
        width: parent ? parent.width : 0
        spacing: 8
        Label {
            text: parent.title
            color: theme.muted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.8
        }
    }
    component Divider: Rectangle {
        width: 1
        height: 20
        anchors.verticalCenter: parent.verticalCenter
        color: theme.border
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 14
        contentHeight: column.height
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: 18

            Label {
                width: parent.width
                text: panel.has ? qsTr("%n selected", "", backend.selectionCount) : qsTr("Next shape")
                color: theme.strong
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Section {
                title: qsTr("Position")
                visible: panel.has
                Grid {
                    columns: 2
                    spacing: 6
                    NumberField { width: 111; label: "X"; value: panel.s.x; onEdited: (v) => backend.setGeometry("x", v) }
                    NumberField { width: 111; label: "Y"; value: panel.s.y; onEdited: (v) => backend.setGeometry("y", v) }
                    NumberField { width: 111; label: "W"; value: panel.s.w; from: 1; onEdited: (v) => backend.setGeometry("w", v) }
                    NumberField { width: 111; label: "H"; value: panel.s.h; from: 1; onEdited: (v) => backend.setGeometry("h", v) }
                }
            }

            Section {
                title: qsTr("Align")
                visible: panel.s.units >= 2
                Row {
                    spacing: 2
                    IconButton { glyph: "alignLeft"; tip: qsTr("Align left"); keys: "Alt+A"; onClicked: backend.align("left") }
                    IconButton { glyph: "alignHCenter"; tip: qsTr("Align center"); keys: "Alt+H"; onClicked: backend.align("hcenter") }
                    IconButton { glyph: "alignRight"; tip: qsTr("Align right"); keys: "Alt+D"; onClicked: backend.align("right") }
                    Item { width: 8; height: 1 }
                    IconButton { glyph: "alignTop"; tip: qsTr("Align top"); keys: "Alt+W"; onClicked: backend.align("top") }
                    IconButton { glyph: "alignVCenter"; tip: qsTr("Align middle"); keys: "Alt+V"; onClicked: backend.align("vcenter") }
                    IconButton { glyph: "alignBottom"; tip: qsTr("Align bottom"); keys: "Alt+S"; onClicked: backend.align("bottom") }
                }
                Row {
                    spacing: 6
                    IconButton {
                        glyph: "distributeH"
                        tip: qsTr("Space out evenly across")
                        keys: "Alt+Shift+H"
                        enabled: panel.s.units >= 3
                        onClicked: backend.distribute("h")
                    }
                    NumberField {
                        width: 72
                        label: "↔"
                        blank: panel.s.gapH === undefined
                        value: panel.s.gapH === undefined ? 0 : panel.s.gapH
                        onEdited: (v) => backend.setGap("h", v)
                    }
                    IconButton {
                        glyph: "distributeV"
                        tip: qsTr("Space out evenly down")
                        keys: "Alt+Shift+V"
                        enabled: panel.s.units >= 3
                        onClicked: backend.distribute("v")
                    }
                    NumberField {
                        width: 72
                        label: "↕"
                        blank: panel.s.gapV === undefined
                        value: panel.s.gapV === undefined ? 0 : panel.s.gapV
                        onEdited: (v) => backend.setGap("v", v)
                    }
                }
            }

            Section {
                title: qsTr("Arrange")
                visible: panel.has
                Row {
                    spacing: 2
                    IconButton { glyph: "toFront"; tip: qsTr("Bring to front"); keys: "Ctrl+Shift+]"; onClicked: backend.order("front") }
                    IconButton { glyph: "forward"; tip: qsTr("Bring forward"); keys: "Ctrl+]"; onClicked: backend.order("forward") }
                    IconButton { glyph: "backward"; tip: qsTr("Send backward"); keys: "Ctrl+["; onClicked: backend.order("backward") }
                    IconButton { glyph: "toBack"; tip: qsTr("Send to back"); keys: "Ctrl+Shift+["; onClicked: backend.order("back") }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    IconButton { raised: true; text: qsTr("Group"); keys: "Ctrl+G"; tip: qsTr("Make the selection move as one"); enabled: panel.s.canGroup; onClicked: backend.group() }
                    IconButton { raised: true; text: qsTr("Ungroup"); keys: "Ctrl+Shift+G"; tip: qsTr("Split the group back up"); enabled: panel.s.canUngroup; onClicked: backend.ungroup() }
                    IconButton { raised: true; text: qsTr("Duplicate"); keys: "Ctrl+D"; tip: qsTr("Make a copy beside it"); onClicked: backend.duplicate() }
                    IconButton { raised: true; text: qsTr("Delete"); keys: "Delete"; tip: qsTr("Remove it from the board"); onClicked: backend.deleteSelection() }
                }
            }

            Section {
                title: qsTr("Shape")
                Row {
                    spacing: 6
                    visible: panel.s.showFill
                    Label { width: 52; height: 28; verticalAlignment: Text.AlignVCenter; text: qsTr("Fill"); color: theme.label; font.pixelSize: 12 }
                    ColorField { value: panel.s.fill; tints: true; tip: qsTr("Fill color"); onPicked: (c) => backend.setStyle("fill", c) }
                    NumberField {
                        visible: panel.s.showRadius
                        width: 76
                        label: "◜"
                        value: panel.s.radius
                        from: 0
                        to: 500
                        onEdited: (v) => backend.setStyle("radius", v)
                        ToolTip.visible: cornerHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Corner radius")
                        HoverHandler { id: cornerHover }
                    }
                }
                Row {
                    spacing: 6
                    Label { width: 52; height: 28; verticalAlignment: Text.AlignVCenter; text: qsTr("Line"); color: theme.label; font.pixelSize: 12 }
                    ColorField { value: panel.s.stroke; tip: qsTr("Line color"); onPicked: (c) => backend.setStyle("stroke", c) }
                    NumberField {
                        width: 76
                        label: "≡"
                        value: panel.s.strokeWidth
                        from: 0
                        to: 40
                        decimals: 1
                        onEdited: (v) => backend.setStyle("strokeWidth", v)
                        ToolTip.visible: widthHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Line thickness")
                        HoverHandler { id: widthHover }
                    }
                }
                Row {
                    spacing: 2
                    Item { width: 56; height: 1 }
                    IconButton { glyph: "solid"; tip: qsTr("Solid"); active: panel.s.dash === 0; onClicked: backend.setStyle("dash", 0) }
                    IconButton { glyph: "dashed"; tip: qsTr("Dashed"); active: panel.s.dash === 1; onClicked: backend.setStyle("dash", 1) }
                    IconButton { glyph: "dotted"; tip: qsTr("Dotted"); active: panel.s.dash === 2; onClicked: backend.setStyle("dash", 2) }
                }
                Row {
                    spacing: 6
                    Label { width: 52; height: 28; verticalAlignment: Text.AlignVCenter; text: qsTr("Opacity"); color: theme.label; font.pixelSize: 12; elide: Text.ElideRight }
                    NumberField {
                        width: 76
                        label: "%"
                        value: Math.round(panel.s.opacity * 100)
                        from: 5
                        to: 100
                        step: 5
                        onEdited: (v) => backend.setStyle("opacity", v / 100)
                    }
                }
            }

            Section {
                title: qsTr("Connector")
                visible: panel.s.showLine
                Row {
                    spacing: 2
                    IconButton { glyph: "straight"; tip: qsTr("Straight"); active: panel.s.routing === 0; onClicked: backend.setStyle("routing", 0) }
                    IconButton { glyph: "elbow"; tip: qsTr("Elbow"); active: panel.s.routing === 1; onClicked: backend.setStyle("routing", 1) }
                    IconButton { glyph: "curved"; tip: qsTr("Curved"); active: panel.s.routing === 2; onClicked: backend.setStyle("routing", 2) }
                    Item { width: 8; height: 1; visible: panel.s.showArrows }
                    IconButton {
                        visible: panel.s.showArrows
                        glyph: "arrowStart"
                        tip: qsTr("Arrowhead at the start")
                        active: panel.s.arrowStart
                        onClicked: backend.setStyle("arrowStart", !panel.s.arrowStart)
                    }
                    IconButton {
                        visible: panel.s.showArrows
                        glyph: "arrowEnd"
                        tip: qsTr("Arrowhead at the end")
                        active: panel.s.arrowEnd
                        onClicked: backend.setStyle("arrowEnd", !panel.s.arrowEnd)
                    }
                }
            }

            Section {
                title: qsTr("Text")
                visible: panel.s.showText
                Row {
                    spacing: 2
                    IconButton { raised: true; text: qsTr("Sans"); active: panel.s.font === "sans"; onClicked: backend.setStyle("font", "sans") }
                    IconButton { raised: true; text: qsTr("Serif"); active: panel.s.font === "serif"; onClicked: backend.setStyle("font", "serif") }
                    IconButton { raised: true; text: qsTr("Mono"); active: panel.s.font === "mono"; onClicked: backend.setStyle("font", "mono") }
                }
                Row {
                    spacing: 6
                    NumberField {
                        width: 76
                        label: "Aa"
                        value: panel.s.fontSize
                        from: 6
                        to: 400
                        onEdited: (v) => backend.setStyle("fontSize", v)
                        ToolTip.visible: sizeHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Text size")
                        HoverHandler { id: sizeHover }
                    }
                    ColorField { value: panel.s.textColor; tip: qsTr("Text color"); onPicked: (c) => backend.setStyle("textColor", c) }
                    Row {
                        spacing: 2
                        IconButton {
                            text: "B"
                            implicitWidth: 32
                            tip: qsTr("Bold")
                            keys: "Ctrl+B"
                            active: panel.s.bold
                            onClicked: backend.setStyle("bold", !panel.s.bold)
                        }
                        IconButton {
                            text: "I"
                            implicitWidth: 32
                            tip: qsTr("Italic")
                            keys: "Ctrl+I"
                            active: panel.s.italic
                            onClicked: backend.setStyle("italic", !panel.s.italic)
                        }
                    }
                }
                Row {
                    spacing: 2
                    IconButton { glyph: "textLeft"; tip: qsTr("Text left"); active: panel.s.hAlign === 0; onClicked: backend.setStyle("hAlign", 0) }
                    IconButton { glyph: "textCenter"; tip: qsTr("Text centered"); active: panel.s.hAlign === 1; onClicked: backend.setStyle("hAlign", 1) }
                    IconButton { glyph: "textRight"; tip: qsTr("Text right"); active: panel.s.hAlign === 2; onClicked: backend.setStyle("hAlign", 2) }
                    Item { width: 8; height: 1 }
                    IconButton { glyph: "textTop"; tip: qsTr("Text at the top"); active: panel.s.vAlign === 0; onClicked: backend.setStyle("vAlign", 0) }
                    IconButton { glyph: "textMiddle"; tip: qsTr("Text in the middle"); active: panel.s.vAlign === 1; onClicked: backend.setStyle("vAlign", 1) }
                    IconButton { glyph: "textBottom"; tip: qsTr("Text at the bottom"); active: panel.s.vAlign === 2; onClicked: backend.setStyle("vAlign", 2) }
                }
            }

            Section {
                title: qsTr("Board")
                visible: !panel.has
                Row {
                    spacing: 2
                    Label { width: 56; height: 32; verticalAlignment: Text.AlignVCenter; text: qsTr("Paper"); color: theme.label; font.pixelSize: 12 }
                    IconButton { raised: true; text: qsTr("Dark"); active: backend.paper === "dark"; onClicked: backend.paper = "dark" }
                    IconButton { raised: true; text: qsTr("Light"); active: backend.paper === "light"; onClicked: backend.paper = "light" }
                }
            }
        }
    }
}
