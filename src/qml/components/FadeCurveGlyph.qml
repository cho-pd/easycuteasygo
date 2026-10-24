import QtQuick
import Drift

// VEDIT: one fade type drawn as Vegas draws it in its Fade Type menu — a small box with the gain
// curve inside. `edge` "in" rises left to right, "out" falls; "cross" draws the outgoing fade-out
// (outCurve) and the incoming fade-in (inCurve) together, crossing like an X.
Rectangle {
    id: root

    property string curve: "smooth"
    property string outCurve: "smooth"
    property string inCurve: "smooth"
    property string edge: "in"   // in | out | cross
    property bool selected: false
    property bool hovered: false

    implicitWidth: 52
    implicitHeight: 38
    radius: 3
    color: selected ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.22)
                    : (hovered ? Theme.panelSecondaryBg : "#141414")
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.primary : "#8a8a8a"

    // Mirrors drift::shapedProgress for the five fade types.
    function gain(c, t) {
        t = Math.max(0, Math.min(1, t))
        switch (c) {
        case "linear": return t
        case "equalPower": return Math.sin(t * Math.PI / 2)
        case "slow": return 1 - Math.cos(t * Math.PI / 2)
        case "sharp": return 0.5 - Math.sin(Math.asin(1 - 2 * t) / 3)
        default: return t * t * (3 - 2 * t)
        }
    }

    Canvas {
        anchors.fill: parent
        anchors.margins: Math.max(2, Math.min(5, root.height / 8))
        property string key: root.edge + root.curve + root.outCurve + root.inCurve + root.selected
        onKeyChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            ctx.strokeStyle = root.selected ? "#ffffff" : "#d8d8d8"
            ctx.lineWidth = root.height < 30 ? 1.1 : 1.4
            const line = (g) => {
                ctx.beginPath()
                for (let i = 0; i <= 32; ++i) {
                    const t = i / 32
                    const y = h * (1 - g(t))
                    if (i === 0)
                        ctx.moveTo(t * w, y)
                    else
                        ctx.lineTo(t * w, y)
                }
                ctx.stroke()
            }
            if (root.edge === "cross") {
                line((t) => root.gain(root.outCurve, 1 - t))
                line((t) => root.gain(root.inCurve, t))
            } else if (root.edge === "out") {
                line((t) => root.gain(root.curve, 1 - t))
            } else {
                line((t) => root.gain(root.curve, t))
            }
        }
    }
}
