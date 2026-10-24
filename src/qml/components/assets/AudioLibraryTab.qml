import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift

// VEDIT: CapCut-style Music / Sound effects tab. Lists the royalty-free audio shipped in
// bundled-addons (kind "music" or "sfx"), grouped by category chips, with a play/stop preview
// and a big [+ 넣기] that drops the file on the timeline at the playhead.
Item {
    id: root

    property string kind: "music"   // "music" | "sfx"
    property string category: ""    // "" = all
    readonly property var items: EditorState.bundledAudioLibrary(kind)
    readonly property var categories: {
        const seen = []
        for (const it of items) {
            if (it.category && seen.indexOf(it.category) < 0)
                seen.push(it.category)
        }
        return seen
    }
    readonly property var visibleItems: category === "" ? items
                                        : items.filter(function (it) { return it.category === category })
    property string playingPath: ""

    signal added()

    function formatSeconds(s) {
        const total = Math.round(Number(s) || 0)
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {}
        onPlaybackStateChanged: if (playbackState === MediaPlayer.StoppedState) root.playingPath = ""
    }
    onVisibleChanged: if (!visible) { player.stop(); playingPath = "" }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingMd

        EmptyState {
            visible: root.items.length === 0
            width: parent.width
            glyph: root.kind === "music" ? Theme.icons.music : Theme.icons.audioLines
            title: root.kind === "music" ? "음악이 아직 없어요" : "효과음이 아직 없어요"
            hint: "내 음악 파일은 [내 사진·영상] 탭에서 가져올 수 있어요."
        }

        Flow {
            visible: root.categories.length > 1
            width: parent.width
            spacing: 6
            ThemedChip {
                text: "전체"
                selected: root.category === ""
                onClicked: root.category = ""
            }
            Repeater {
                model: root.categories
                delegate: ThemedChip {
                    required property string modelData
                    text: modelData
                    selected: root.category === modelData
                    onClicked: root.category = modelData
                }
            }
        }

        ListView {
            width: parent.width
            height: parent.height - y
            clip: true
            spacing: 4
            model: root.visibleItems
            ScrollBar.vertical: AppScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 56
                radius: Theme.radiusSm
                color: rowHover.hovered ? Theme.popoverHover : "transparent"
                HoverHandler { id: rowHover }

                IconButton {
                    id: playButton
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: root.playingPath === modelData.path ? Theme.icons.pause : Theme.icons.play
                    tooltip: root.playingPath === modelData.path ? "멈추기" : "들어 보기"
                    onClicked: {
                        if (root.playingPath === modelData.path) {
                            player.stop()
                            root.playingPath = ""
                        } else {
                            player.source = modelData.url
                            player.play()
                            root.playingPath = modelData.path
                        }
                    }
                }

                Column {
                    anchors.left: playButton.right
                    anchors.leftMargin: Theme.spacingSm
                    anchors.right: addButton.left
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: modelData.title || ""
                        elide: Text.ElideRight
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBase
                    }
                    Text {
                        width: parent.width
                        text: root.formatSeconds(modelData.seconds)
                              + (modelData.author ? "  ·  " + modelData.author : "")
                        elide: Text.ElideRight
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                ThemedButton {
                    id: addButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+ 넣기"
                    onClicked: {
                        if (EditorState.addBundledAudio(modelData.path))
                            root.added()
                    }
                }
            }
        }
    }
}
