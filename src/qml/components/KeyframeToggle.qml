import QtQuick
import Drift

// VEDIT (CapCut): the ◇ beside a value. Hollow: no keyframes. Filled yellow: a keyframe at the
// playhead (click takes it out). Outlined yellow: keyframed, none here (click adds one). With
// keyframes, ‹ › jump the playhead to the previous / next one.
Row {
    id: root
    property bool animated: false
    property bool atPlayhead: false
    property real prevSeconds: -1
    property real nextSeconds: -1
    property string label: ""
    signal toggled()

    spacing: 0
    height: 22

    Item {
        width: 18
        height: parent.height
        visible: root.animated
        Text {
            anchors.centerIn: parent
            text: "‹"
            color: root.prevSeconds >= 0 ? Theme.panelForeground : Theme.mutedForeground
            opacity: root.prevSeconds >= 0 ? 1 : 0.4
            font.pixelSize: 18
            font.family: Theme.fontFamily
        }
        MouseArea {
            anchors.fill: parent
            enabled: root.prevSeconds >= 0
            cursorShape: Qt.PointingHandCursor
            onClicked: EditorState.playheadSeconds = root.prevSeconds
        }
    }

    Item {
        width: 24
        height: parent.height
        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 10
            rotation: 45
            color: root.atPlayhead ? Theme.primary : "transparent"
            border.width: 1.5
            border.color: root.animated || keyHover.hovered ? Theme.primary : Theme.mutedForeground
        }
        HoverHandler { id: keyHover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggled()
        }
        ThemedToolTip {
            visible: keyHover.hovered
            text: root.atPlayhead ? qsTr("Remove the keyframe here")
                                  : qsTr("Add a keyframe here")
        }
    }

    Item {
        width: 18
        height: parent.height
        visible: root.animated
        Text {
            anchors.centerIn: parent
            text: "›"
            color: root.nextSeconds >= 0 ? Theme.panelForeground : Theme.mutedForeground
            opacity: root.nextSeconds >= 0 ? 1 : 0.4
            font.pixelSize: 18
            font.family: Theme.fontFamily
        }
        MouseArea {
            anchors.fill: parent
            enabled: root.nextSeconds >= 0
            cursorShape: Qt.PointingHandCursor
            onClicked: EditorState.playheadSeconds = root.nextSeconds
        }
    }
}
