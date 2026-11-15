import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// One of the app's line icons, drawn at any size in any color.
Item {
    id: icon
    property string name: ""
    property color color: theme.text
    property real strokeWidth: 1.6
    implicitWidth: 20
    implicitHeight: 20

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(icon.width, icon.height) / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.paths[icon.name] || "" }
        }
    }
}
