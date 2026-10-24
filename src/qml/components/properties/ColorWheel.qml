import QtQuick
import Drift
import ".."

// VEDIT: one of CapCut's 컬러 휠 — drag the dot toward a colour to tint that part of the picture
// (the further out, the stronger); the bar below makes it brighter or darker. Double-click the
// wheel to put it back in the middle.
Column {
    id: root

    property string wheel: "lift"   // lift | gamma | gain | offset
    property string title: ""
    property var values: ({})
    property real size: 120
    property real liveX: NaN
    property real liveY: NaN

    function val(k) {
        const v = root.values["adjust.wheels." + root.wheel + "_" + k]
        return v === undefined ? 0 : Number(v)
    }
    readonly property real px: isNaN(liveX) ? val("x") : liveX
    readonly property real py: isNaN(liveY) ? val("y") : liveY

    spacing: 6

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.title
        color: Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: 14
    }
    Item {
        id: disc
        width: root.size
        height: root.size
        anchors.horizontalCenter: parent.horizontalCenter
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = width / 2
                for (let a = 0; a < 360; a += 3) {
                    ctx.beginPath()
                    ctx.moveTo(r, r)
                    ctx.arc(r, r, r, (a - 1.6) * Math.PI / 180, (a + 1.6) * Math.PI / 180)
                    ctx.closePath()
                    // hue at screen angle a; y points up on the wheel
                    ctx.fillStyle = Qt.hsla(((360 - a) % 360) / 360, 0.75, 0.5, 1)
                    ctx.fill()
                }
                const g = ctx.createRadialGradient(r, r, 0, r, r, r)
                g.addColorStop(0, "rgba(40,40,40,1)")
                g.addColorStop(1, "rgba(40,40,40,0)")
                ctx.fillStyle = g
                ctx.beginPath(); ctx.arc(r, r, r, 0, Math.PI * 2); ctx.fill()
            }
        }
        Rectangle {
            width: 14; height: 14; radius: 7
            color: "transparent"
            border.width: 2
            border.color: "#ffffff"
            x: disc.width / 2 + root.px * disc.width / 2 - width / 2
            y: disc.height / 2 - root.py * disc.height / 2 - height / 2
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            function place(mouse) {
                let x = (mouse.x - disc.width / 2) / (disc.width / 2)
                let y = -(mouse.y - disc.height / 2) / (disc.height / 2)
                const len = Math.sqrt(x * x + y * y)
                if (len > 1) { x /= len; y /= len }
                root.liveX = x
                root.liveY = y
                EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip, "adjust.wheels", root.wheel + "_x", x)
                EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip, "adjust.wheels", root.wheel + "_y", y)
            }
            onPressed: (mouse) => { EditorState.beginPreviewDrag("컬러 휠"); place(mouse) }
            onPositionChanged: (mouse) => { if (pressed) place(mouse) }
            onReleased: { EditorState.commitPreviewDrag(); root.liveX = NaN; root.liveY = NaN }
            onCanceled: { EditorState.cancelPreviewDrag(); root.liveX = NaN; root.liveY = NaN }
            onDoubleClicked: {
                EditorState.beginPreviewDrag("컬러 휠")
                EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip, "adjust.wheels", root.wheel + "_x", 0)
                EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip, "adjust.wheels", root.wheel + "_y", 0)
                EditorState.commitPreviewDrag()
            }
        }
    }
    ThemedSlider {
        id: lum
        defaultValue: 0 // VEDIT
        width: root.size
        anchors.horizontalCenter: parent.horizontalCenter
        label: root.title
        from: -1
        to: 1
        stepSize: 0.01
        valueFormatter: function (v) { return Math.round(v * 100).toString() }
        Binding on value {
            when: !lum.pressed
            value: root.val("l")
        }
        onPressedChanged: pressed ? EditorState.beginPreviewDrag("컬러 휠") : EditorState.commitPreviewDrag()
        onMoved: EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip, "adjust.wheels", root.wheel + "_l", value)
    }
}
