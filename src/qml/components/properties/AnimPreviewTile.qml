import QtQuick
import Drift

// VEDIT: one animation choice as CapCut shows it — a small picture that moves the way the clip
// will when the pointer is on it (and rests partway through otherwise), with its name below.
// The motion comes from the same code that moves the real clip (EditorState.clipAnimPreview).
Item {
    id: root

    property string slot: "in"   // in | out | combo
    property string animId: "none"
    property string label: ""
    property bool selected: false
    signal picked()

    // VEDIT (조박사님): the card grows with the panel (the inspector sets it), CapCut-sized.
    property real tileSize: 84
    width: tileSize
    height: tileSize + nameText.height + 4

    // 0..1 through a 2 s pretend clip. VEDIT (조박사님, CapCut): at rest the picture is whole and
    // full size — after an entrance, before an exit — and the pointer plays the move.
    readonly property real restT: slot === "in" ? 1.0 : 0.0
    property real t: restT
    readonly property var pose: EditorState.clipAnimPreview(root.slot, root.animId, root.t)

    NumberAnimation on t {
        running: hover.hovered && root.animId !== "none"
        from: 0
        to: 1
        duration: 2000
        loops: Animation.Infinite
    }
    Connections {
        target: hover
        function onHoveredChanged() {
            if (!hover.hovered)
                root.t = root.restT
        }
    }

    Rectangle {
        id: card
        width: root.tileSize
        height: root.tileSize
        radius: Theme.radiusSm
        color: root.animId === "none" ? (hover.hovered ? Theme.panelSecondaryBg : Theme.panelAccent)
                                      : "#000000" // VEDIT (조박사님, CapCut): black behind the picture
        border.width: root.selected ? 2 : 0
        border.color: Theme.primary
        clip: true

        // "None": a crossed circle, as in CapCut's first cell.
        Rectangle {
            visible: root.animId === "none"
            anchors.centerIn: parent
            width: 34
            height: 34
            radius: 17
            color: "transparent"
            border.width: 2
            border.color: Theme.mutedForeground
            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: 34
                color: Theme.mutedForeground
                rotation: 45
            }
        }

        // The picture that moves — the whole card, as the clip fills the frame (VEDIT, CapCut).
        Item {
            visible: root.animId !== "none"
            anchors.fill: parent
            opacity: root.pose.opacity
            transform: [
                Scale {
                    origin.x: root.tileSize / 2
                    origin.y: root.tileSize / 2
                    xScale: root.pose.scale * root.pose.scaleX
                    yScale: root.pose.scale * root.pose.scaleY
                },
                Rotation {
                    origin.x: root.tileSize / 2
                    origin.y: root.tileSize / 2
                    angle: root.pose.rotation
                },
                Rotation { // VEDIT: 3D turns, as the compositor draws them
                    origin.x: root.tileSize / 2
                    origin.y: root.tileSize / 2
                    axis { x: 0; y: 1; z: 0 }
                    angle: root.pose.yaw || 0
                },
                Rotation {
                    origin.x: root.tileSize / 2
                    origin.y: root.tileSize / 2
                    axis { x: 1; y: 0; z: 0 }
                    angle: root.pose.pitch || 0
                },
                Translate {
                    x: root.pose.dx * root.tileSize / 100
                    y: root.pose.dy * root.tileSize / 100
                }
            ]

            // VEDIT (조박사님): a real picture moves (from ../img/a2.png)
            Image {
                anchors.fill: parent
                // VEDIT (조박사님): one picture for entrances, another for exits
                source: root.slot === "out" ? "qrc:/app/brand/anim_out.jpg" : "qrc:/app/brand/anim_fg.jpg"
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
            }
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        ThemedToolTip {
            visible: hover.hovered && nameText.truncated
            text: root.label
        }
        TapHandler {
            onTapped: root.picked()
        }
    }

    Text {
        id: nameText
        anchors.top: card.bottom
        anchors.topMargin: 4
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: root.label
        color: root.selected ? Theme.primary : Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs // VEDIT: CapCut-sized
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label
}
