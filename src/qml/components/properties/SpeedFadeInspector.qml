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
    // Composites (and audio separated from one) do not retime in this version.
    readonly property bool speedKind: (clipKind === "video" || clipKind === "audio") && !clipData.sequenceId

    height: speedColumn.height
    implicitHeight: speedColumn.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: speedColumn
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: !root.speedKind
            width: parent.width
            compact: true
            glyph: Theme.icons.gauge
            title: qsTr("Not available")
            hint: qsTr("Speed applies to video and audio clips.")
        }

        Text {
            visible: root.speedKind
            text: qsTr("Playback speed")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // A ramp supersedes the constant rate outright, so the controls below
        // are meaningless while one is attached.
        property bool hasSpeedCurve: {
            void root.clipDataRevision
            return !!root.clipData.hasSpeedCurve
        }

        Row {
            width: parent.width
            spacing: 6
            visible: root.speedKind

            ThemedButton {
                text: qsTr("Custom speed…")
                variant: "secondary"
                onClicked: root.Window.window.openSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: speedColumn.hasSpeedCurve
                text: qsTr("Custom speed active — remove")
                selected: true
                onClicked: EditorState.clearClipSpeedCurve(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

            // VEDIT: CapCut's curve presets, applied to the clip in one click.
            Column {
                width: parent.width
                spacing: 6
                visible: root.speedKind

                Text {
                    text: "속도 곡선"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 6
                    readonly property string current: {
                        void root.clipDataRevision
                        return EditorState.clipSpeedCurvePreset(EditorState.selectedTrack,
                                                                EditorState.selectedClip)
                    }
                    Repeater {
                        model: [
                            { id: "none", label: "없음" },
                            { id: "montage", label: "몽타주" },
                            { id: "hero", label: "히어로" },
                            { id: "bullet", label: "총알" },
                            { id: "jumpcut", label: "점프 컷" },
                            { id: "flashin", label: "플래시 인" },
                            { id: "flashout", label: "플래시 아웃" }
                        ]
                        delegate: ThemedChip {
                            required property var modelData
                            text: modelData.label
                            selected: parent.current === modelData.id
                            onClicked: EditorState.applySpeedCurvePreset(
                                           EditorState.selectedTrack, EditorState.selectedClip,
                                           modelData.id)
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 6
                visible: root.speedKind
                Repeater {
                    model: [
                        { label: "0.25×", value: 0.25 },
                        { label: "0.5×", value: 0.5 },
                        { label: "1×", value: 1.0 },
                        { label: "2×", value: 2.0 },
                        { label: "4×", value: 4.0 },
                        // VEDIT (조박사님): faster steps
                        { label: "10×", value: 10.0 },
                        { label: "25×", value: 25.0 },
                        { label: "100×", value: 100.0 }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        enabled: !speedColumn.hasSpeedCurve
                        selected: !speedColumn.hasSpeedCurve
                                  && Math.abs((root.clipData.speed || 1) - modelData.value) < 0.01
                        onClicked: EditorState.setClipSpeed(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData.value)
                    }
                }
            }

        ThemedSlider {
            id: speedSlider
            label: qsTr("Speed")
            visible: root.speedKind
            enabled: !speedColumn.hasSpeedCurve && !EditorState.playing
            width: parent.width
            // VEDIT (조박사님): CapCut's 0.1x-100x in marked steps, evenly spaced —
            // 0.1 · 0.5 · 1 · 2 · 5 · 10 · 15 · 20 · 25 · 100 (그 이상) — with a smooth ramp between.
            readonly property var stops: [0.1, 0.5, 1, 2, 5, 10, 15, 20, 25, 100]
            function speedAt(v) {
                const i = Math.max(0, Math.min(stops.length - 2, Math.floor(v)))
                const t = Math.max(0, Math.min(1, v - i))
                return stops[i] * Math.pow(stops[i + 1] / stops[i], t)
            }
            function positionOf(sp) {
                sp = Math.max(stops[0], Math.min(stops[stops.length - 1], sp))
                for (let i = 0; i < stops.length - 1; ++i)
                    if (sp <= stops[i + 1])
                        return i + Math.log(sp / stops[i]) / Math.log(stops[i + 1] / stops[i])
                return stops.length - 1
            }
            defaultValue: 2 // VEDIT: 1x
            from: 0
            to: stops.length - 1
            stepSize: 0.01
            valueFormatter: function (v) { return speedAt(v).toFixed(2) + "×" }
            Binding on value {
                when: !speedSlider.pressed
                value: speedSlider.positionOf(root.clipData.speed || 1.0)
            }
            onMoved: EditorState.previewSetClipSpeed(
                         EditorState.selectedTrack, EditorState.selectedClip, speedSlider.speedAt(value))
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Speed changed"))
                else
                    EditorState.commitPreviewDrag()
            }
        }

        // VEDIT (조박사님): the steps marked under the speed slider.
        Item {
            visible: root.speedKind
            width: speedSlider.width
            height: 26
            Repeater {
                model: speedSlider.stops
                delegate: Item {
                    required property var modelData
                    required property int index
                    readonly property real handleW: Theme.iconSizeMd
                    x: speedSlider.leftPadding + handleW / 2
                       + index / (speedSlider.stops.length - 1) * (speedSlider.availableWidth - handleW)
                    Rectangle {
                        x: -0.5
                        y: 0
                        width: 1
                        height: 6
                        color: Theme.mutedForeground
                    }
                    Text {
                        anchors.horizontalCenter: parent.left
                        y: 8
                        text: index === speedSlider.stops.length - 1 ? qsTr("More") : modelData
                        color: Math.abs((root.clipData.speed || 1) - modelData) < 0.01
                               ? Theme.primary : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTiny
                    }
                    MouseArea {
                        x: -12
                        width: 24
                        height: 26
                        enabled: speedSlider.enabled
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setClipSpeed(EditorState.selectedTrack,
                                                            EditorState.selectedClip, modelData)
                    }
                }
            }
        }

        Text {
            visible: root.speedKind
            text: (speedColumn.hasSpeedCurve
                   ? qsTr("Custom speed")
                   : (root.clipData.speed || 1).toFixed(2) + "×")
                    + (root.clipData.reverse ? qsTr(" (reversed)") : "")
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        ThemedChip {
            visible: root.speedKind
            text: qsTr("Reverse")
            selected: {
                void root.clipDataRevision
                return !!root.clipData.reverse
            }
            // Turning reverse off is free. Turning it on may need a rendered copy first, which
            // requestClipReverse decides and confirms through ReverseProgressDialog.
            onClicked: {
                if (root.clipData.reverse)
                    EditorState.setClipReverse(EditorState.selectedTrack,
                                               EditorState.selectedClip, false)
                else
                    EditorState.requestClipReverse(EditorState.selectedTrack,
                                                   EditorState.selectedClip)
            }
        }

        // Shown when a reversed video clip has no rendered copy covering it — after a cancelled
        // render, or after a trim pushed the clip past the range that was rendered.
        Row {
            width: parent.width
            spacing: Theme.spacingLg
            visible: {
                void root.clipDataRevision
                void EditorState.reverseRendering
                return root.clipKind === "video" && !!root.clipData.reverse
                       && !EditorState.clipHasReverseProxy(EditorState.selectedTrack,
                                                           EditorState.selectedClip)
            }

            ThemedLabel {
                width: parent.width - renderReversedButton.width - Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Not rendered — playback may stutter")
            }

            ThemedButton {
                id: renderReversedButton
                text: qsTr("Render")
                onClicked: EditorState.requestClipReverse(EditorState.selectedTrack,
                                                          EditorState.selectedClip)
            }
        }
    }
}
