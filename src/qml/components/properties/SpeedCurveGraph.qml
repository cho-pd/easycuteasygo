import QtQuick
import Drift

// VEDIT (조박사님: "속도 곡선을 시간 그래프로 보여줘야지"): CapCut's curve-speed graph, in the Speed
// tab. Across: the clip from start to end. Up: how fast it plays (0.1× to 8×, 1× in the middle band).
// The yellow line is the playhead. Points drag up and down (and sideways between their
// neighbours); a double-click on the line adds a point, on a point removes it.
Item {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property int revision: 0

    implicitHeight: 200

    readonly property var graph: {
        void root.revision
        void EditorState.playheadSeconds
        return root.trackIndex >= 0 && root.clipIndex >= 0
               ? EditorState.clipSpeedCurveGraph(root.trackIndex, root.clipIndex, 96) : ({})
    }
    readonly property real minSpeed: graph.minSpeed || 0.1
    readonly property real maxSpeed: graph.maxSpeed || 8
    readonly property bool hasCurve: graph.hasCurve === true

    // Points being dragged: the graph's own copy, so the line follows the pointer at once.
    property var livePoints: null
    readonly property var points: livePoints !== null ? livePoints : (graph.points || [])

    readonly property real padLeft: 40
    readonly property real padRight: 12
    readonly property real padTop: 10
    readonly property real padBottom: 22
    readonly property real plotW: Math.max(1, width - padLeft - padRight)
    readonly property real plotH: Math.max(1, height - padTop - padBottom)

    function xFor(pos) { return padLeft + pos * plotW }
    function posFor(x) { return Math.max(0, Math.min(1, (x - padLeft) / plotW)) }
    function yFor(speed) {
        const s = Math.max(minSpeed, Math.min(maxSpeed, speed))
        const t = Math.log(s / minSpeed) / Math.log(maxSpeed / minSpeed)
        return padTop + (1 - t) * plotH
    }
    function speedFor(y) {
        const t = 1 - Math.max(0, Math.min(1, (y - padTop) / plotH))
        return minSpeed * Math.pow(maxSpeed / minSpeed, t)
    }
    function copyPoints() {
        const out = []
        for (let i = 0; i < points.length; ++i)
            out.push({ pos: points[i].pos, speed: points[i].speed })
        return out
    }
    function speedAtPos(pos) {
        const s = graph.samples || []
        if (s.length === 0)
            return 1
        return s[Math.round(pos * (s.length - 1))]
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.panelMuted
        border.width: 1
        border.color: Theme.panelBorder
    }

    // Speed grid with labels.
    Repeater {
        model: [0.1, 0.5, 1, 2, 4, 8]
        delegate: Item {
            required property var modelData
            Rectangle {
                x: root.padLeft
                y: root.yFor(modelData)
                width: root.plotW
                height: 1
                color: modelData === 1 ? Theme.mutedForeground : Theme.panelBorder
                opacity: modelData === 1 ? 0.7 : 0.5
            }
            Text {
                x: 4
                y: root.yFor(modelData) - height / 2
                text: modelData + "×"
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    Text {
        x: root.padLeft
        y: root.height - height - 3
        text: "시작"
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
    Text {
        x: root.padLeft + root.plotW - width
        y: root.height - height - 3
        text: (root.graph.seconds || 0).toFixed(1) + "초"
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Canvas {
        id: curve
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.lineWidth = 2
            ctx.strokeStyle = Theme.primary
            ctx.beginPath()
            if (root.livePoints !== null) {
                // While dragging: straight runs between the points (the real curve redraws on release).
                const p = root.livePoints
                for (let i = 0; i < p.length; ++i) {
                    const x = root.xFor(p[i].pos), y = root.yFor(p[i].speed)
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
            } else {
                const s = root.graph.samples || []
                for (let i = 0; i < s.length; ++i) {
                    const x = root.xFor(i / Math.max(1, s.length - 1)), y = root.yFor(s[i])
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
            }
            ctx.stroke()
        }
        Connections {
            target: root
            function onGraphChanged() { curve.requestPaint() }
            function onLivePointsChanged() { curve.requestPaint() }
            function onWidthChanged() { curve.requestPaint() }
            function onHeightChanged() { curve.requestPaint() }
        }
    }

    // Playhead.
    Rectangle {
        visible: (root.graph.playhead || -1) >= 0
        x: root.xFor(root.graph.playhead || 0) - 1
        y: root.padTop
        width: 2
        height: root.plotH
        color: Theme.primary
        opacity: 0.85
    }

    // Double-click on the line: add a point there (the first one makes the clip a curve).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: (mouse) => {
            const pos = root.posFor(mouse.x)
            let pts = root.copyPoints()
            if (pts.length < 2) {
                const s = root.speedAtPos(0.5)
                pts = [{ pos: 0, speed: s }, { pos: 1, speed: s }]
            }
            pts.push({ pos: pos, speed: root.speedFor(mouse.y) })
            pts.sort((a, b) => a.pos - b.pos)
            EditorState.setClipSpeedCurvePoints(root.trackIndex, root.clipIndex, pts)
        }
    }

    Repeater {
        model: root.points.length
        delegate: Item {
            id: knot
            required property int index
            readonly property var p: root.points[index] || { pos: 0, speed: 1 }
            readonly property bool isEnd: index === 0 || index === root.points.length - 1
            width: 28
            height: 28
            x: root.xFor(p.pos) - width / 2
            y: root.yFor(p.speed) - height / 2

            Rectangle {
                anchors.centerIn: parent
                width: knotMouse.pressed || knotMouse.containsMouse ? 16 : 13
                height: width
                radius: width / 2
                color: knotMouse.pressed ? Theme.primary : "#ffffff"
                border.width: 2
                border.color: Theme.primary
            }
            Text {
                visible: knotMouse.pressed || knotMouse.containsMouse
                anchors.bottom: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                text: knot.p.speed.toFixed(knot.p.speed < 1 ? 2 : 1) + "×"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
            }

            MouseArea {
                id: knotMouse
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                cursorShape: knot.isEnd ? Qt.SizeVerCursor : Qt.SizeAllCursor
                onPressed: root.livePoints = root.copyPoints()
                onPositionChanged: (mouse) => {
                    if (!pressed || root.livePoints === null)
                        return
                    const at = mapToItem(root, mouse.x, mouse.y)
                    const pts = root.livePoints.slice()
                    const i = knot.index
                    const pt = { pos: pts[i].pos, speed: root.speedFor(at.y) }
                    if (!knot.isEnd) {
                        const lo = pts[i - 1].pos + 0.02, hi = pts[i + 1].pos - 0.02
                        pt.pos = Math.max(lo, Math.min(hi, root.posFor(at.x)))
                    }
                    pts[i] = pt
                    root.livePoints = pts
                    EditorState.previewSetClipSpeedCurve(root.trackIndex, root.clipIndex, pts)
                }
                onReleased: {
                    root.livePoints = null
                    EditorState.commitPreviewDrag()
                }
                onCanceled: {
                    root.livePoints = null
                    EditorState.cancelPreviewDrag()
                }
                onDoubleClicked: {
                    if (knot.isEnd || root.points.length <= 2)
                        return
                    const pts = root.copyPoints()
                    pts.splice(knot.index, 1)
                    EditorState.setClipSpeedCurvePoints(root.trackIndex, root.clipIndex, pts)
                }
            }
        }
    }
}
