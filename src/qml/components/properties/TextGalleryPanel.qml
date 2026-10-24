// VEDIT: CapCut 글자 › 말풍선 / 편집효과 — a grid of pictures; one press puts the bubble or the
// word art on the selected text, the first tile takes it off again.
import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Column {
    id: root
    spacing: 12

    property string kind: "bubble" // "bubble" | "art"
    readonly property var items: kind === "bubble" ? EditorState.textBubbles() : EditorState.textWordArts()
    readonly property int columns: Math.max(2, Math.floor((width + 8) / 128))
    readonly property real tileWidth: (width - (columns - 1) * 8) / columns

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: root.kind === "bubble"
              ? qsTr("Press a bubble to put the words in it. The words keep their font and size.")
              : qsTr("Press a look to dress the words. The words keep their font and size.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: 16
    }

    Flow {
        width: parent.width
        spacing: 8
        Repeater {
            model: root.items
            delegate: Rectangle {
                id: tile
                required property var modelData
                readonly property bool isNone: modelData.id === "bubble-none" || modelData.id === "art-none"
                width: root.tileWidth
                height: 104
                radius: 8
                color: tileArea.containsMouse ? Theme.panelAccent : Theme.panelSecondaryBg
                border.width: tileArea.containsMouse ? 2 : 1
                border.color: tileArea.containsMouse ? Theme.focusRing : Theme.panelBorder

                Image {
                    visible: !tile.isNone
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 12
                    height: 64
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(Math.round(width * 2), Math.round(height * 2))
                    source: tile.isNone ? "" : "image://textstyle/" + tile.modelData.id
                }
                Text {
                    visible: tile.isNone
                    anchors.top: parent.top
                    anchors.topMargin: 14
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "⊘"
                    color: Theme.mutedForeground
                    font.pixelSize: 36
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: tile.modelData.label
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.kind === "bubble")
                            EditorState.applyTextBubble(EditorState.selectedTrack, EditorState.selectedClip, tile.modelData.id)
                        else
                            EditorState.applyTextWordArt(EditorState.selectedTrack, EditorState.selectedClip, tile.modelData.id)
                    }
                }
            }
        }
    }
}
