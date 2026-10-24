import QtQuick
import QtQuick.Controls.Basic
import Drift

// VEDIT: the Fade Type menu Vegas opens on a right-click — for a clip, its fade-in and fade-out
// curves; for a transition, the out × in grid. Picking applies at once (one undo step each).
Popup {
    id: root

    property string mode: "clip"   // clip | transition
    property int trackIndex: -1
    property int clipIndex: -1
    property var transition: ({})  // a transition map (id, outCurve, inCurve)
    property var clipMap: ({})     // a clip map (fadeCurve, fadeOutCurve); "clip" is Item's own

    padding: 6 // 조박사님: half the first size
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
    }

    contentItem: Column {
        spacing: 4

        Text {
            text: qsTr("Fade type")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }

        CrossfadeCurveGrid {
            visible: root.mode === "transition"
            cellWidth: 26
            cellHeight: 20
            outCurve: root.transition.outCurve || "smooth"
            inCurve: root.transition.inCurve || "smooth"
            onPicked: function(o, i) {
                EditorState.setTransitionFadeTypes(root.trackIndex, root.transition.id || "", o, i)
                root.close()
            }
        }

        Text {
            visible: root.mode === "clip"
            text: qsTr("Fade in")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
        FadeCurvePicker {
            visible: root.mode === "clip"
            cellWidth: 26
            cellHeight: 24
            edge: "in"
            current: root.clipMap.fadeCurve || "smooth"
            onPicked: function(c) {
                EditorState.setClipFadeEdgeCurve(root.trackIndex, root.clipIndex, "in", c)
                root.close()
            }
        }
        Text {
            visible: root.mode === "clip"
            text: qsTr("Fade out")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
        FadeCurvePicker {
            visible: root.mode === "clip"
            cellWidth: 26
            cellHeight: 24
            edge: "out"
            current: root.clipMap.fadeOutCurve || root.clipMap.fadeCurve || "smooth"
            onPicked: function(c) {
                EditorState.setClipFadeEdgeCurve(root.trackIndex, root.clipIndex, "out", c)
                root.close()
            }
        }
    }
}
