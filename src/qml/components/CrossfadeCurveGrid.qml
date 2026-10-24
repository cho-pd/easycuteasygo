import QtQuick
import Drift

// VEDIT: Vegas's crossfade Fade Type grid — rows pick how the outgoing clip fades out, columns
// how the incoming clip fades in; each cell draws the pair crossing.
Column {
    id: root

    property string outCurve: "smooth"
    property string inCurve: "smooth"
    property real cellWidth: 52
    property real cellHeight: 40
    signal picked(string outCurve, string inCurve)

    readonly property var curves: ["equalPower", "linear", "slow", "smooth", "sharp"]
    readonly property var names: [qsTr("Fast"), qsTr("Linear"), qsTr("Slow"), qsTr("Smooth"), qsTr("Sharp")]

    spacing: cellWidth < 40 ? 2 : 3

    Repeater {
        model: root.curves.length
        delegate: Row {
            id: gridRow
            required property int index
            readonly property string rowOut: root.curves[index]
            spacing: root.cellWidth < 40 ? 2 : 3

            Repeater {
                model: root.curves.length
                delegate: FadeCurveGlyph {
                    id: cell
                    required property int index
                    width: root.cellWidth
                    height: root.cellHeight
                    edge: "cross"
                    outCurve: gridRow.rowOut
                    inCurve: root.curves[index]
                    selected: root.outCurve === gridRow.rowOut && root.inCurve === root.curves[index]
                    hovered: cellHover.hovered

                    HoverHandler {
                        id: cellHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.picked(gridRow.rowOut, root.curves[cell.index])
                    }
                    ThemedToolTip {
                        visible: cellHover.hovered
                        text: qsTr("Out: %1 · In: %2").arg(root.names[gridRow.index]).arg(root.names[cell.index])
                    }
                }
            }
        }
    }
}
