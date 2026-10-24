import QtQuick
import Drift

// VEDIT: Vegas's five fade types in a row of pictures — fast, linear, slow, smooth, sharp.
Row {
    id: root

    property string edge: "in"        // in | out
    property string current: "smooth"
    signal picked(string curve)

    readonly property var curves: ["equalPower", "linear", "slow", "smooth", "sharp"]
    readonly property var names: [qsTr("Fast"), qsTr("Linear"), qsTr("Slow"), qsTr("Smooth"), qsTr("Sharp")]

    property real cellWidth: 52
    property real cellHeight: 48
    spacing: cellWidth < 40 ? 2 : 4

    Repeater {
        model: root.curves.length
        delegate: FadeCurveGlyph {
            id: glyph
            required property int index
            width: root.cellWidth
            height: root.cellHeight
            edge: root.edge
            curve: root.curves[index]
            selected: root.current === root.curves[index]
            hovered: pickHover.hovered

            HoverHandler {
                id: pickHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: root.picked(root.curves[glyph.index])
            }
            ThemedToolTip {
                visible: pickHover.hovered
                text: root.names[glyph.index]
            }
            Accessible.role: Accessible.RadioButton
            Accessible.name: root.names[glyph.index]
            Accessible.checked: glyph.selected
        }
    }
}
