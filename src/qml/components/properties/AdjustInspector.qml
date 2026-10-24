import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// VEDIT: CapCut-style "Adjust" tab — the everyday colour controls as plain sliders right on the
// selected video/photo clip. Each slider drives one bundled effect (adjust.*, stylize.sharpen) on
// the clip's effect host, so the pro Effects view shows the same values. A drag is one undo step.
Item {
    id: root

    property int revision: 0
    readonly property var clipData: {
        void revision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool adjustable: clipKind === "video" || clipKind === "image"
    // VEDIT: 필터 — the clip's current look, and the looks on offer (tile colour hints at each).
    readonly property var filter: {
        void revision
        return adjustable ? EditorState.clipFilter(EditorState.selectedTrack, EditorState.selectedClip)
                          : ({ id: "none", strength: 0.8 })
    }
    readonly property var filters: [
        { id: "none", label: "원본", tint: "#3a3a3a" },
        { id: "warm", label: "따뜻하게", tint: "#c9874a" },
        { id: "cool", label: "시원하게", tint: "#4a7fc9" },
        { id: "vivid", label: "선명하게", tint: "#d6493f" },
        { id: "bright", label: "화사하게", tint: "#e8c96b" },
        { id: "bw", label: "흑백", tint: "#9a9a9a" },
        { id: "old", label: "옛날 사진", tint: "#8a6a45" },
        { id: "movie", label: "영화처럼", tint: "#2f6f73" },
        { id: "film", label: "필름", tint: "#6b5a3a" },
        { id: "dream", label: "몽글몽글", tint: "#c98aa8" }
    ]

    readonly property var values: {
        void revision
        void EditorState.playheadSeconds // VEDIT: keyframed values read as at the playhead
        void EditorState.tracksRevision
        return adjustable ? EditorState.clipAdjustValues(EditorState.selectedTrack, EditorState.selectedClip)
                          : ({})
    }

    // effectId, parameter key, label, min, max, neutral value, display scale (shown = value * scale)
    // VEDIT: grouped as CapCut's 조정 > 기본 — 색상, 광도 (light), 편집효과.
    readonly property var controlGroups: [
        { title: "색상", items: [
            { effect: "adjust.temperature", key: "temperature", label: "색온도", min: 3000, max: 7000, neutral: 6500, scale: 1 },
            { effect: "adjust.tone", key: "tint", label: "색조 (초록 ↔ 자주)", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.saturation", key: "saturation", label: "채도", min: 0, max: 2, neutral: 1, scale: 100 },
            { effect: "adjust.hue", key: "h", label: "색상 돌리기", min: -180, max: 180, neutral: 0, scale: 1 }
        ] },
        { title: "광도", items: [
            { effect: "adjust.brightness", key: "brightness", label: "밝기", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "exposure", label: "노출", min: -2, max: 2, neutral: 0, scale: 50 },
            { effect: "adjust.contrast", key: "contrast", label: "대비", min: 0, max: 2, neutral: 1, scale: 100 },
            { effect: "adjust.tone", key: "highlights", label: "밝은 부분", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "shadows", label: "어두운 부분", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "whites", label: "화이트", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "blacks", label: "블랙", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "brilliance", label: "휘도", min: -1, max: 1, neutral: 0, scale: 100 }
        ] },
        { title: "편집효과", items: [
            { effect: "stylize.sharpen", key: "luma_amount", label: "선명하게", min: 0, max: 3, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "clarity", label: "선명도", min: -1, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "grain", label: "입자", min: 0, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "fade", label: "페이드", min: 0, max: 1, neutral: 0, scale: 100 },
            { effect: "adjust.tone", key: "vignette", label: "가장자리 어둡게", min: 0, max: 1, neutral: 0, scale: 100 }
        ] }
    ]
    // VEDIT: HSL — which colour the three sliders below work on
    property string hslColor: "red"
    readonly property var hslColors: [
        { id: "red", label: "빨강", swatch: "#e5484d" }, { id: "orange", label: "주황", swatch: "#f1873a" },
        { id: "yellow", label: "노랑", swatch: "#f2cf3a" }, { id: "green", label: "초록", swatch: "#46b45a" },
        { id: "cyan", label: "청록", swatch: "#3cc3c8" }, { id: "blue", label: "파랑", swatch: "#3a73e0" },
        { id: "purple", label: "보라", swatch: "#8a5ad8" }, { id: "magenta", label: "자홍", swatch: "#d84fb0" }
    ]
    function hslTouched(id) {
        for (const k of ["h_", "s_", "l_"]) {
            const v = root.values["adjust.hsl." + k + id]
            if (v !== undefined && Math.abs(Number(v)) > 0.001)
                return true
        }
        return false
    }
    property string adjustTab: "basic" // VEDIT: 기본 | hsl | curves | wheels
    readonly property var controls: {
        let out = []
        for (let g = 0; g < controlGroups.length; ++g)
            out = out.concat(controlGroups[g].items)
        return out
    }

    height: column.height
    implicitHeight: column.height

    function refreshFields() {}

    function valueOf(c) {
        const v = root.values[c.effect + "." + c.key]
        return v === undefined ? c.neutral : Number(v)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.revision++ }
        function onSelectedClipDataChanged() { root.revision++ }
        function onTracksChanged() { root.revision++ }
    }

    Column {
        id: column
        width: root.width
        spacing: Theme.spacingLg

        EmptyState {
            visible: !root.adjustable
            width: parent.width
            compact: true
            glyph: Theme.icons.sliders
            title: "영상이나 사진 장면을 고르세요"
            hint: "필터와 밝기·대비·채도·색온도를 바로 고칠 수 있어요."
        }

        // VEDIT: CapCut's 조정 tabs
        Row {
            visible: root.adjustable
            spacing: 6
            Repeater {
                model: [ { id: "basic", label: "기본" }, { id: "hsl", label: "HSL" },
                         { id: "curves", label: "곡선" }, { id: "wheels", label: "컬러 휠" } ]
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    labelSize: 16
                    chipHeight: 40
                    selected: root.adjustTab === modelData.id
                    onClicked: root.adjustTab = modelData.id
                }
            }
        }

        // VEDIT: CapCut's 조정 > 컬러 휠
        Grid {
            visible: root.adjustable && root.adjustTab === "wheels"
            columns: 2
            spacing: 14
            Repeater {
                model: [ { w: "lift", t: "어두운 부분" }, { w: "gamma", t: "중간 톤" },
                         { w: "gain", t: "밝은 부분" }, { w: "offset", t: "전체" } ]
                delegate: ColorWheel {
                    required property var modelData
                    wheel: modelData.w
                    title: modelData.t
                    values: root.values
                    size: Math.min(130, (column.width - 14) / 2)
                }
            }
        }
        Text {
            visible: root.adjustable && root.adjustTab === "wheels"
            width: column.width
            wrapMode: Text.WordWrap
            text: "원 안의 점을 원하는 색 쪽으로 끌어요(멀수록 강하게). 아래 막대는 밝기예요. 원을 두 번 누르면 처음대로."
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }

        // ----- 필터 ------------------------------------------------------------------------
        // VEDIT: CapCut's 자동 보정 — one press for a dark or dull shot.
        ThemedButton {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            variant: "primary"
            glyph: Theme.icons.wand
            text: "자동 보정"
            tooltip: "어둡거나 흐릿한 장면을 밝고 또렷하게 맞춥니다"
            onClicked: EditorState.autoAdjustClip(EditorState.selectedTrack, EditorState.selectedClip)
        }
        // VEDIT (CapCut ◇ at the top of 조정): every value on this tab keyed at the playhead at once.
        ThemedButton {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            variant: "secondary"
            text: "◇  지금 위치에 모든 값 키프레임"
            tooltip: "재생 위치에서 이 탭의 모든 조정 값을 키프레임으로 저장합니다. 다른 위치로 옮겨 값을 바꾸면 그 사이가 서서히 바뀝니다"
            onClicked: EditorState.keyframeAllClipAdjust(EditorState.selectedTrack, EditorState.selectedClip,
                                                         root.controls)
        }

        // VEDIT: CapCut's 색상 맞춤
        ThemedButton {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            variant: "secondary"
            glyph: Theme.icons.wand
            text: "앞 장면 색에 맞추기"
            tooltip: "카메라가 달라 색이 튀는 장면을, 바로 앞 장면의 밝기·대비·색감에 맞춥니다"
            onClicked: EditorState.matchClipColor(EditorState.selectedTrack, EditorState.selectedClip)
        }

        Text {
            visible: root.adjustable && root.adjustTab === "basic"
            text: "필터"
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }

        Flow {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            spacing: Theme.spacingSm

            Repeater {
                model: root.filters
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    readonly property bool current: root.filter.id === modelData.id
                    width: 72
                    height: 64
                    radius: Theme.radiusMd
                    color: Theme.panelSecondaryBg
                    border.width: current ? 2 : Theme.borderWidth
                    border.color: current ? Theme.primary : Theme.panelBorder

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 6
                        width: parent.width - 12
                        height: 28
                        radius: Theme.radiusSm
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.darker(tile.modelData.tint, 1.4) }
                            GradientStop { position: 1.0; color: Qt.lighter(tile.modelData.tint, 1.3) }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        text: tile.modelData.label
                        color: tile.current ? Theme.primary : Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.applyClipFilter(EditorState.selectedTrack, EditorState.selectedClip,
                                                               tile.modelData.id, root.filter.strength)
                    }
                }
            }
        }

        // VEDIT: named, like the bars below.
        Item {
            visible: filterStrength.visible
            width: column.width
            height: strengthName.implicitHeight
            Text {
                id: strengthName
                text: "필터 강도"
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Text {
                anchors.right: parent.right
                text: Math.round(filterStrength.value * 100).toString()
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }

        ThemedSlider {
            id: filterStrength
            defaultValue: 0.8 // VEDIT
            visible: root.adjustable && root.adjustTab === "basic" && root.filter.id !== "none"
            width: column.width
            label: "필터 강도"
            from: 0
            to: 1
            stepSize: 0.01
            valueFormatter: function (v) { return Math.round(v * 100).toString() }
            Binding on value {
                when: !filterStrength.pressed
                value: Number(root.filter.strength)
            }
            // One undo step per drag: the look is rebuilt when the handle is let go.
            onPressedChanged: {
                if (!pressed)
                    EditorState.applyClipFilter(EditorState.selectedTrack, EditorState.selectedClip,
                                                root.filter.id, value)
            }
        }

        // VEDIT: CapCut's 모두 적용 — the same look on every photo and video.
        ThemedButton {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            variant: "secondary"
            text: "모든 사진·영상에 이 필터"
            tooltip: "이 클립의 필터(와 강도)를 다른 모든 사진과 영상에 똑같이 겁니다"
            onClicked: EditorState.applyFilterToAllPictures(EditorState.selectedTrack, EditorState.selectedClip)
        }

        Rectangle {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            height: Theme.borderWidth
            color: Theme.panelBorder
        }

        Repeater {
            model: root.adjustable && root.adjustTab === "basic" ? root.controlGroups : []
            delegate: Column {
                id: group
                required property var modelData
                width: column.width
                spacing: Theme.spacingLg
                Text {
                    text: group.modelData.title
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }
        Repeater {
            model: group.modelData.items
            // VEDIT: the name and the number sit above each bar — ThemedSlider's `label` is only
            // read out by screen readers, so on its own the bars were unnamed (조박사님).
            delegate: Column {
                id: row
                required property var modelData
                width: column.width
                spacing: 2

                Item {
                    width: parent.width
                    height: nameText.implicitHeight
                    Text {
                        id: nameText
                        text: row.modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    Text {
                        anchors.right: rowKey.left
                        anchors.rightMargin: 4
                        text: Math.round(slider.value * row.modelData.scale).toString()
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    // VEDIT (CapCut): ◇ — keyframe this value at the playhead.
                    KeyframeToggle {
                        id: rowKey
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property var keyState: {
                            void root.revision
                            void EditorState.playheadSeconds
                            void EditorState.tracksRevision
                            return EditorState.clipAdjustKeyState(EditorState.selectedTrack, EditorState.selectedClip,
                                                                  row.modelData.effect, row.modelData.key)
                        }
                        animated: keyState.animated === true
                        atPlayhead: keyState.atPlayhead === true
                        prevSeconds: keyState.prev !== undefined ? keyState.prev : -1
                        nextSeconds: keyState.next !== undefined ? keyState.next : -1
                        onToggled: EditorState.toggleClipAdjustKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                                       row.modelData.effect, row.modelData.key)
                    }
                }

                ThemedSlider {
                    id: slider
                    defaultValue: row.modelData.neutral // VEDIT
                    width: parent.width
                    label: row.modelData.label
                    from: row.modelData.min
                    to: row.modelData.max
                    stepSize: (row.modelData.max - row.modelData.min) / 200
                    valueFormatter: function (v) { return Math.round(v * row.modelData.scale).toString() }
                    Binding on value {
                        when: !slider.pressed
                        value: root.valueOf(row.modelData)
                    }
                    onPressedChanged: {
                        if (pressed)
                            EditorState.beginPreviewDrag("조정")
                        else
                            EditorState.commitPreviewDrag()
                    }
                    onMoved: EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip,
                                                       row.modelData.effect, row.modelData.key, value)
                }
            }
        }
            }
        }

        // VEDIT: CapCut's 조정 > HSL — pick a colour, then its 색상 / 채도 / 밝기.
        Text {
            visible: root.adjustable && root.adjustTab === "hsl"
            text: "HSL (색깔별로)"
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.weight: Font.Medium
        }
        Row {
            visible: root.adjustable && root.adjustTab === "hsl"
            spacing: 6
            Repeater {
                model: root.hslColors
                delegate: Rectangle {
                    required property var modelData
                    width: 28
                    height: 28
                    radius: 14
                    color: modelData.swatch
                    border.width: root.hslColor === modelData.id ? 3 : 1
                    border.color: root.hslColor === modelData.id ? "#ffffff" : Qt.rgba(1, 1, 1, 0.3)
                    // a dot on colours already changed
                    Rectangle {
                        visible: root.hslTouched(modelData.id)
                        anchors.right: parent.right
                        anchors.top: parent.top
                        width: 8; height: 8; radius: 4
                        color: Theme.primary
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.hslColor = parent.modelData.id
                    }
                    ThemedToolTip {
                        visible: hslHover.hovered
                        text: parent.modelData.label
                    }
                    HoverHandler { id: hslHover }
                }
            }
        }
        Repeater {
            model: root.adjustable && root.adjustTab === "hsl" ? [
                { effect: "adjust.hsl", key: "h_" + root.hslColor, label: "색상", min: -1, max: 1, neutral: 0, scale: 100 },
                { effect: "adjust.hsl", key: "s_" + root.hslColor, label: "채도", min: -1, max: 1, neutral: 0, scale: 100 },
                { effect: "adjust.hsl", key: "l_" + root.hslColor, label: "밝기", min: -1, max: 1, neutral: 0, scale: 100 }
            ] : []
            delegate: Column {
                id: hslRow
                required property var modelData
                width: column.width
                spacing: 2
                Item {
                    width: parent.width
                    height: hslName.implicitHeight
                    Text {
                        id: hslName
                        text: hslRow.modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    Text {
                        anchors.right: hslKey.left
                        anchors.rightMargin: 4
                        text: Math.round(hslSlider.value * 100).toString()
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    KeyframeToggle { // VEDIT (CapCut) ◇
                        id: hslKey
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property var keyState: {
                            void root.revision
                            void EditorState.playheadSeconds
                            void EditorState.tracksRevision
                            return EditorState.clipAdjustKeyState(EditorState.selectedTrack, EditorState.selectedClip,
                                                                  hslRow.modelData.effect, hslRow.modelData.key)
                        }
                        animated: keyState.animated === true
                        atPlayhead: keyState.atPlayhead === true
                        prevSeconds: keyState.prev !== undefined ? keyState.prev : -1
                        nextSeconds: keyState.next !== undefined ? keyState.next : -1
                        onToggled: EditorState.toggleClipAdjustKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                                       hslRow.modelData.effect, hslRow.modelData.key)
                    }
                }
                ThemedSlider {
                    id: hslSlider
                    defaultValue: 0 // VEDIT
                    width: parent.width
                    label: hslRow.modelData.label
                    from: -1
                    to: 1
                    stepSize: 0.01
                    valueFormatter: function (v) { return Math.round(v * 100).toString() }
                    Binding on value {
                        when: !hslSlider.pressed
                        value: root.valueOf(hslRow.modelData)
                    }
                    onPressedChanged: {
                        if (pressed)
                            EditorState.beginPreviewDrag("HSL")
                        else
                            EditorState.commitPreviewDrag()
                    }
                    onMoved: EditorState.setClipAdjust(EditorState.selectedTrack, EditorState.selectedClip,
                                                       hslRow.modelData.effect, hslRow.modelData.key, value)
                }
            }
        }

        // VEDIT: CapCut's 조정 > 곡선
        Text {
            visible: root.adjustable && root.adjustTab === "curves"
            text: "곡선"
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.weight: Font.Medium
        }
        CurvesEditor {
            visible: root.adjustable && root.adjustTab === "curves"
            width: column.width
            values: root.values
        }

        ThemedButton {
            visible: root.adjustable
            text: "조정 처음대로"
            // VEDIT: takes the adjustments off rather than writing eight neutral effects.
            onClicked: EditorState.resetClipAdjust(EditorState.selectedTrack, EditorState.selectedClip)
        }

        // VEDIT: CapCut's 모두 적용 for the sliders above.
        ThemedButton {
            visible: root.adjustable
            width: column.width
            variant: "secondary"
            text: "모든 사진·영상에 이 조정"
            tooltip: "이 클립의 밝기·대비·노출 같은 조정을 다른 모든 사진과 영상에 똑같이 겁니다(필터는 그대로)"
            onClicked: EditorState.applyAdjustToAllPictures(EditorState.selectedTrack, EditorState.selectedClip)
        }

        // VEDIT: 초록 배경 지우기 — key out a green screen, or pick the background colour off the preview.
        Rectangle {
            visible: root.adjustable && root.adjustTab === "basic"
            width: column.width
            height: Theme.borderWidth
            color: Theme.panelBorder
        }
        Text {
            visible: root.adjustable && root.adjustTab === "basic"
            text: "배경 지우기 (크로마키)"
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }
        Row {
            visible: root.adjustable && root.adjustTab === "basic"
            spacing: Theme.spacingSm
            ThemedButton {
                text: "초록 배경 지우기"
                onClicked: EditorState.applyChromaKeyColor(EditorState.selectedTrack, EditorState.selectedClip, "#ff00ff00")
            }
            ThemedButton {
                text: EditorState.chromaPickActive ? "미리보기에서 배경을 누르세요" : "지울 색 찍기"
                onClicked: EditorState.chromaPickActive ? EditorState.cancelChromaPick() : EditorState.beginChromaPick()
            }
        }
        // VEDIT: CapCut's 자동 배경 제거 — any background, no green screen: the bundled model keeps
        // the people. Pressing again while it runs stops it.
        ThemedButton {
            visible: root.adjustable && root.adjustTab === "basic" && root.clipKind === "video"
            width: column.width
            glyph: Theme.icons.wand
            text: EditorState.segmenting
                  ? "사람 오려내는 중… " + Math.round(EditorState.segmentProgress * 100) + "% (누르면 멈춤)"
                  : "사람만 남기기 (배경 자동 지우기)"
            tooltip: "영상 속 사람만 남기고 나머지 배경을 투명하게 합니다. 아래 줄에 다른 영상이나 사진을 두면 배경이 바뀝니다"
            onClicked: EditorState.keepOnlyPeople(EditorState.selectedTrack, EditorState.selectedClip)
        }
    }
}
