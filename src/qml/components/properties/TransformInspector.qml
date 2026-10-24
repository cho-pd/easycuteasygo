import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // A model clip is a full-canvas layer placed by its camera: x/y shift the model, the box
    // size and spin mean nothing (its own size and rotation live on the 3D Model tab).
    readonly property bool isModel3d: clipKind === "model3d"
    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectHeight())
    }

    readonly property var propOpacity: { "key": "opacity", "label": qsTr("Opacity"), "def": 1.0, "decimals": 2, "reset": 1.0 }
    readonly property var propX: { "key": "x", "label": "X", "def": 0.0, "decimals": 0 }
    readonly property var propY: { "key": "y", "label": "Y", "def": 0.0, "decimals": 0 }
    readonly property var propWidth: { "key": "width", "label": qsTr("Width"), "def": root.canvasW, "decimals": 0 }
    readonly property var propHeight: { "key": "height", "label": qsTr("Height"), "def": root.canvasH, "decimals": 0 }
    readonly property var propRotation: { "key": "rotation", "label": qsTr("Angle"), "def": 0.0, "decimals": 1, "reset": 0.0 }

    // One slider that scales width and height together about the box centre; the link button
    // swaps it for the separate Width/Height rows.
    property bool sizeLinked: true
    readonly property bool isText: clipKind === "text" || clipKind === "subtitle"

    // 100% is the box that fills the canvas on its tighter axis — what Reset and a fitted
    // import both produce — whatever the clip's own aspect ratio.
    property int liveRevision: 0
    readonly property real scaleAtPlayhead: {
        void clipDataRevision
        void liveRevision
        void EditorState.playheadSeconds
        if (!hasSelection)
            return 1
        const w = transformAt("width", canvasW)
        const h = transformAt("height", canvasH)
        return Math.max(w / canvasW, h / canvasH)
    }

    property var scaleDragStart: null

    function transformAt(key, def) {
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           key, EditorState.playheadSeconds, def)
    }

    function beginScaleDrag() {
        scaleDragStart = {
            "x": transformAt("x", 0),
            "y": transformAt("y", 0),
            "w": transformAt("width", canvasW),
            "h": transformAt("height", canvasH),
            "scale": scaleAtPlayhead,
            "pixelSize": (clipData.textStyle && clipData.textStyle.pixelSize) || 64
        }
    }

    function applyScale(scale) {
        const s = scaleDragStart
        if (!s || s.scale <= 0)
            return
        const k = scale / s.scale
        const w = Math.max(1, s.w * k)
        const h = Math.max(1, s.h * k)
        const x = s.x + (s.w - w) / 2
        const y = s.y + (s.h - h) / 2
        if (isText) {
            EditorState.previewSetTextRect(EditorState.selectedTrack, EditorState.selectedClip,
                                           x, y, w, h, Math.round(s.pixelSize * k))
        } else {
            EditorState.previewSetClipRect(EditorState.selectedTrack, EditorState.selectedClip,
                                           x, y, w, h)
        }
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            if (!scaleSlider.pressed && (keys.indexOf("width") >= 0 || keys.indexOf("height") >= 0))
                root.liveRevision++
        }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind === "audio"
            width: parent.width
            compact: true
            glyph: Theme.icons.film
            title: qsTr("Video only")
            hint: qsTr("This tab does not apply to audio clips.")
        }

        Column {
            width: root.width
            spacing: 10
            visible: root.clipKind !== "audio"

            // VEDIT (조박사님): 크기 first.
            Item {
                visible: !root.isModel3d
                width: parent.width
                height: sizeLinkButton.height

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.sizeLinked ? qsTr("Scale") : qsTr("Size (px)")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                // VEDIT (CapCut): ◇ for 크기 — keys width, height and position together so the
                // picture grows and shrinks about the same centre.
                KeyframeToggle {
                    id: scaleKey
                    visible: root.sizeLinked
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property var scaleProps: ["width", "height", "x", "y"]
                    readonly property var keyState: {
                        void root.clipDataRevision
                        void root.liveRevision
                        void EditorState.playheadSeconds
                        void EditorState.tracksRevision
                        return EditorState.clipPropsKeyState(EditorState.selectedTrack, EditorState.selectedClip, scaleProps)
                    }
                    animated: keyState.animated === true
                    atPlayhead: keyState.atPlayhead === true
                    prevSeconds: keyState.prev !== undefined ? keyState.prev : -1
                    nextSeconds: keyState.next !== undefined ? keyState.next : -1
                    onToggled: EditorState.toggleClipPropsKeyframe(EditorState.selectedTrack, EditorState.selectedClip, scaleProps)
                }

                IconButton {
                    id: sizeLinkButton
                    anchors.right: scaleKey.visible ? scaleKey.left : parent.right
                    anchors.rightMargin: scaleKey.visible ? 6 : 0 // VEDIT: ◇ stays at the far right
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: 24
                    iconSize: 14
                    glyph: root.sizeLinked ? Theme.icons.linkTwo : Theme.icons.unlink
                    active: root.sizeLinked
                    tooltip: root.sizeLinked ? qsTr("Edit width and height separately")
                                             : qsTr("Scale width and height together")
                    onClicked: root.sizeLinked = !root.sizeLinked
                }
            }

            Row {
                visible: !root.isModel3d && root.sizeLinked
                width: parent.width
                spacing: 8

                ThemedSlider {
                    id: scaleSlider
                    defaultValue: 1 // VEDIT: 100%
                    lockWhilePlaying: true
                    label: qsTr("Scale")
                    width: parent.width - scaleReadout.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.05
                    to: 4
                    valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                    Binding on value {
                        when: !scaleSlider.pressed
                        value: root.scaleAtPlayhead
                    }
                    onPressedChanged: {
                        if (pressed) {
                            root.beginScaleDrag()
                            EditorState.beginPreviewDrag(qsTr("Scale clip"))
                        } else {
                            EditorState.commitPreviewDrag()
                        }
                    }
                    onMoved: root.applyScale(value)
                }

                Text {
                    id: scaleReadout
                    width: 48
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: Math.round((scaleSlider.pressed ? scaleSlider.value : root.scaleAtPlayhead) * 100) + "%"
                    color: Theme.panelForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            PropertyKeyframeRow {
                visible: !root.isModel3d && !root.sizeLinked
                width: parent.width
                propDef: root.propWidth
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.width && root.clipData.keyframes.width.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasW * 2, 2)
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: !root.isModel3d && !root.sizeLinked
                width: parent.width
                propDef: root.propHeight
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.height && root.clipData.keyframes.height.points) || []
                useSlider: true
                sliderFrom: 1
                sliderTo: Math.max(root.canvasH * 2, 2)
                unit: "px"
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider or the preview also creates them.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedChip {
                text: qsTr("Auto keyframes")
                selected: EditorState.autoKeyEnabled
                onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
            }

            Text {
                text: root.isModel3d ? qsTr("Offset (px)") : qsTr("Position (px)")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propX
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.x && root.clipData.keyframes.x.points) || []
                useSlider: true
                sliderFrom: -root.canvasW
                sliderTo: root.canvasW * 2
                unit: "px"
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propY
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.y && root.clipData.keyframes.y.points) || []
                useSlider: true
                sliderFrom: -root.canvasH
                sliderTo: root.canvasH * 2
                unit: "px"
            }

            Text {
                text: root.isModel3d ? qsTr("Opacity") : qsTr("Opacity & rotation")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propOpacity
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.opacity && root.clipData.keyframes.opacity.points) || []
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
                percent: true
            }

            PropertyKeyframeRow {
                visible: !root.isModel3d
                width: parent.width
                propDef: root.propRotation
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotation && root.clipData.keyframes.rotation.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            Text {
                visible: !root.isModel3d
                text: qsTr("Rotate 90°")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                visible: !root.isModel3d
                width: parent.width
                spacing: 6
                Repeater {
                    // Plain ints avoid JS-object model role quirks (e.g. "value").
                    model: [0, 90, 180, -90]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            void root.clipDataRevision
                            void EditorState.playheadSeconds
                            const cur = Number(root.clipData.rotationAtPlayhead || 0)
                            return Math.abs(cur - modelData) < 0.5
                        }
                        onClicked: EditorState.setClipRotationSnap(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            Text {
                visible: root.clipKind === "video"
                text: qsTr("Fix orientation")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Text {
                visible: root.clipKind === "video"
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Corrects the source's own rotation losslessly — unlike Angle above, this changes decoding, not just the on-screen box.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                visible: root.clipKind === "video"
                width: parent.width
                spacing: 6
                Repeater {
                    model: [0, 90, 180, 270]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            void root.clipDataRevision
                            return Number(root.clipData.orientation) === modelData
                        }
                        onClicked: EditorState.setClipOrientation(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            Text {
                text: qsTr("Flip")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                width: parent.width
                spacing: 6
                ThemedChip {
                    text: qsTr("Flip H")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipH
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !root.clipData.flipH, !!root.clipData.flipV)
                }
                ThemedChip {
                    text: qsTr("Flip V")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipV
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !!root.clipData.flipH, !root.clipData.flipV)
                }
            }

            // VEDIT: CapCut's 화면 속 화면 — put the picture small in a corner (or back to full) in one press.
            Column {
                visible: root.clipKind === "video" || root.clipKind === "image"
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("Place on screen")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [
                            { label: qsTr("Top left"), where: "topLeft" },
                            { label: qsTr("Top right"), where: "topRight" },
                            { label: qsTr("Bottom left"), where: "bottomLeft" },
                            { label: qsTr("Bottom right"), where: "bottomRight" },
                            { label: qsTr("Small in the middle"), where: "center" },
                            { label: qsTr("Full screen"), where: "full" }
                        ]
                        delegate: ThemedChip {
                            required property var modelData
                            text: modelData.label
                            onClicked: EditorState.placeClipOnScreen(EditorState.selectedTrack,
                                                                     EditorState.selectedClip, modelData.where)
                        }
                    }
                }
            }

            // VEDIT: CapCut's 캔버스 — what fills the empty space around a photo or video that is not
            // the screen's shape. One setting for the whole video (the project background).
            Column {
                visible: root.clipKind === "video" || root.clipKind === "image"
                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("Empty space around the picture")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [
                            { label: qsTr("Black"), kind: "color", color: "#ff000000" },
                            { label: qsTr("Blurred picture"), kind: "blur", color: "" },
                            { label: qsTr("White"), kind: "color", color: "#ffffffff" }
                        ]
                        delegate: ThemedChip {
                            required property var modelData
                            readonly property var bg: EditorState.background
                            text: modelData.label
                            selected: modelData.kind === "blur"
                                      ? bg.kind === "blur"
                                      : (bg.kind === "color"
                                         && String(bg.color).toLowerCase() === modelData.color)
                            onClicked: EditorState.setBackground(modelData.kind === "blur"
                                                                 ? { kind: "blur", blurStrength: 40 }
                                                                 : { kind: "color", color: modelData.color })
                        }
                    }
                }
            }

            // VEDIT: CapCut-style crop. Insets per edge, as a share of the frame; the kept part stays
            // where it was on the canvas. Each slider drag is one undo step.
            Column {
                visible: root.clipKind === "video" || root.clipKind === "image"
                width: parent.width
                spacing: 6

                Text {
                    text: "자르기"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: [
                        { key: "cropL", label: "왼쪽" },
                        { key: "cropT", label: "위" },
                        { key: "cropR", label: "오른쪽" },
                        { key: "cropB", label: "아래" }
                    ]
                    // VEDIT: a visible name and value above each bar (the slider's `label` is
                    // for screen readers only).
                    delegate: Column {
                        id: cropRow
                        required property var modelData
                        width: parent.width
                        spacing: 2
                        Item {
                            width: parent.width
                            height: cropName.implicitHeight
                            Text {
                                id: cropName
                                text: cropRow.modelData.label
                                color: Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }
                            Text {
                                anchors.right: parent.right
                                text: Math.round(cropSlider.value * 100) + "%"
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }
                        }
                    ThemedSlider {
                        id: cropSlider
                        defaultValue: 0 // VEDIT
                        property var modelData: cropRow.modelData
                        width: parent.width
                        label: modelData.label
                        from: 0
                        to: 0.49
                        stepSize: 0.005
                        valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                        Binding on value {
                            when: !cropSlider.pressed
                            value: {
                                void root.clipDataRevision
                                return Number(root.clipData[cropSlider.modelData.key] || 0)
                            }
                        }
                        function insets(v) {
                            const c = root.clipData
                            const o = { cropL: Number(c.cropL || 0), cropT: Number(c.cropT || 0),
                                        cropR: Number(c.cropR || 0), cropB: Number(c.cropB || 0) }
                            o[modelData.key] = v
                            return o
                        }
                        onPressedChanged: {
                            if (pressed)
                                EditorState.beginPreviewDrag("자르기")
                            else
                                EditorState.commitPreviewDrag()
                        }
                        onMoved: {
                            const o = insets(value)
                            EditorState.previewSetClipCrop(EditorState.selectedTrack, EditorState.selectedClip,
                                                           o.cropL, o.cropT, o.cropR, o.cropB)
                        }
                    }
                    }
                }

                ThemedButton {
                    text: "자르기 없애기"
                    enabled: Number(root.clipData.cropL || 0) + Number(root.clipData.cropT || 0)
                             + Number(root.clipData.cropR || 0) + Number(root.clipData.cropB || 0) > 0
                    onClicked: EditorState.setClipCrop(EditorState.selectedTrack, EditorState.selectedClip, 0, 0, 0, 0)
                }
            }

            ThemedButton {
                text: root.isModel3d ? qsTr("Reset position") : qsTr("Reset position & size")
                onClicked: EditorState.resetClipTransform(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
