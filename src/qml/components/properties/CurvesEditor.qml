import QtQuick
import Drift
import ".."

// VEDIT: CapCut's 조정 > 곡선 — pick RGB / 빨강 / 초록 / 파랑, then drag any of the five points up or
// down (0, 25, 50, 75, 100 % input). One drag is one undo step; a double-click puts that curve
// back to the straight line.
Column {
    id: root

    property var values: ({})     // the clip's adjust values ("adjust.curves.m2" → 0.6 ...)
    property string channel: "m"
    readonly property var channels: [
        { id: "m", label: "RGB", color: "#e8e8e8" }, { id: "r", label: "빨강", color: "#ff5a5a" },
        { id: "g", label: "초록", color: "#4ed36a" }, { id: "b", label: "파랑", color: "#5a8cff" }
    ]
    property var dragYs: null      // live points while dragging

    function pointsOf(ch) {
        const ys = []
        for (let i = 0; i < 5; ++i) {
            const v = root.values["adjust.curves." + ch + i]
            ys.push(v === undefined ? i * 0.25 : Number(v))
        }
        return ys
    }
    readonly property var ys: dragYs ? dragYs : pointsOf(channel)
    function seg(p0, p1, p2, p3, t) {
        return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                      + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t)
    }
    function curveAt(ys, x) {
        const f = Math.max(0, Math.min(1, x)) * 4
        const i = Math.min(Math.floor(f), 3)
        const t = f - i
        return Math.max(0, Math.min(1, seg(ys[Math.max(i - 1, 0)], ys[i], ys[i + 1], ys[Math.min(i + 2, 4)], t)))
    }

    spacing: 8

    Row {
        spacing: 6
        Repeater {
            model: root.channels
            delegate: ThemedChip {
                required property var modelData
                text: modelData.label
                labelSize: 16
                selected: root.channel === modelData.id
                onClicked: root.channel = modelData.id
            }
        }
    }

    Item {
        id: pad
        width: Math.min(260, root.width)
        height: width
        Rectangle {
            anchors.fill: parent
            color: "#141414"
            border.width: 1
            border.color: Theme.panelBorder
        }
        Canvas {
            id: plot
            anchors.fill: parent
            property var key: JSON.stringify(root.ys) + root.channel
            onKeyChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width, h = height
                ctx.strokeStyle = "rgba(255,255,255,0.12)"
                ctx.lineWidth = 1
                for (let k = 1; k < 4; ++k) {
                    ctx.beginPath(); ctx.moveTo(w * k / 4, 0); ctx.lineTo(w * k / 4, h); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(0, h * k / 4); ctx.lineTo(w, h * k / 4); ctx.stroke()
                }
                ctx.strokeStyle = "rgba(255,255,255,0.25)"
                ctx.beginPath(); ctx.moveTo(0, h); ctx.lineTo(w, 0); ctx.stroke()
                const col = root.channels.find(c => c.id === root.channel).color
                ctx.strokeStyle = col
                ctx.lineWidth = 2
                ctx.beginPath()
                for (let i = 0; i <= 64; ++i) {
                    const x = i / 64
                    const y = root.curveAt(root.ys, x)
                    if (i === 0) ctx.moveTo(x * w, h * (1 - y)); else ctx.lineTo(x * w, h * (1 - y))
                }
                ctx.stroke()
            }
        }
        Repeater {
            model: 5
            delegate: Rectangle {
                id: knob
                required property int index
                width: 14
                height: 14
                radius: 7
                color: knobMouse.pressed || knobMouse.containsMouse ? "#ffffff" : "#cfcfcf"
                border.width: 2
                border.color: "#1a1a1a"
                x: pad.width * index / 4 - width / 2
                y: pad.height * (1 - root.ys[index]) - height / 2
                MouseArea {
                    id: knobMouse
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    preventStealing: true
                    onPressed: {
                        root.dragYs = root.pointsOf(root.channel)
                        EditorState.beginPreviewDrag("곡선")
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed || !root.dragYs)
                            return
                        const yy = mapToItem(pad, mouse.x, mouse.y).y
                        const v = Math.max(0, Math.min(1, 1 - yy / pad.height))
                        const next = root.dragYs.slice()
                        next[knob.index] = v
                        root.dragYs = next
                        EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip,
                                                  "adjust.curves", root.channel + knob.index, v)
                    }
                    onReleased: {
                        EditorState.commitPreviewDrag()
                        root.dragYs = null
                    }
                    onCanceled: {
                        EditorState.cancelPreviewDrag()
                        root.dragYs = null
                    }
                    onDoubleClicked: {
                        EditorState.beginPreviewDrag("곡선")
                        for (let i = 0; i < 5; ++i)
                            EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip,
                                                      "adjust.curves", root.channel + i, i * 0.25)
                        EditorState.commitPreviewDrag()
                    }
                }
            }
        }
    }

    Text {
        width: root.width
        wrapMode: Text.WordWrap
        text: "점을 위아래로 끌어요. 점을 두 번 누르면 이 곡선이 처음(직선)으로 돌아가요."
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: 14
    }
}
