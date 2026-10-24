import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    property int transitionDataRevision: 0
    property bool suppressTransitionKindUpdate: false

    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    readonly property int transitionEditTrack: EditorState.selectedTransitionTrack >= 0
                                                 ? EditorState.selectedTransitionTrack
                                                 : EditorState.selectedTrack
    readonly property int transitionEditLeftClip: EditorState.selectedTransitionLeftClip >= 0
                                                    ? EditorState.selectedTransitionLeftClip
                                                    : EditorState.selectedClip
    readonly property var activeTransition: {
        void transitionDataRevision
        const selected = EditorState.selectedTransitionData
        if (selected && Object.keys(selected).length > 0)
            return selected
        return EditorState.transitionBetweenClips(
                   root.transitionEditTrack, root.transitionEditLeftClip)
    }
    readonly property bool hasActiveTransition: !!activeTransition && Object.keys(activeTransition).length > 0
    readonly property bool canAddOutgoingTransition: {
        if (!root.hasSelection)
            return false
        const tracks = EditorState.tracks
        const t = EditorState.selectedTrack
        const c = EditorState.selectedClip
        if (t < 0 || !tracks || t >= tracks.length)
            return false
        const track = tracks[t]
        if (track.type !== "video" && track.type !== "shape" && track.type !== "text")
            return false
        if (c < 0 || c >= track.clips.length)
            return false
        const left = track.clips[c]
        for (let i = 0; i < track.clips.length; i++) {
            if (i === c)
                continue
            const right = track.clips[i]
            if (right.start < left.start)
                continue
            const gap = right.start - (left.start + left.duration)
            if (gap <= 0.001)
                return true
        }
        return false
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {
        if (!root.hasActiveTransition)
            return
        if (transitionDurationField && !transitionDurationField.activeFocus)
            transitionDurationField.value = root.activeTransition.duration || 0.5
        if (transitionKindBox) {
            const kinds = EditorState.transitionKindsForTrack(root.transitionEditTrack)
            const active = root.activeTransition.kind || "crossfade"
            let idx = 0
            for (let i = 0; i < kinds.length; ++i) {
                if (kinds[i].kind === active) {
                    idx = i
                    break
                }
            }
            root.suppressTransitionKindUpdate = true
            transitionKindBox.currentIndex = idx
            root.suppressTransitionKindUpdate = false
        }
    }

    function commitEdits() {
        if (!root.hasActiveTransition)
            return
        const transitionId = root.activeTransition.id
        if (!transitionId)
            return
        if (transitionDurationField) {
            const v = transitionDurationField.value
            const current = Number(root.activeTransition.duration || 0.5)
            if (Math.abs(v - current) > 0.0001)
                EditorState.setTransitionDuration(
                    root.transitionEditTrack, transitionId, v)
        }
        if (transitionKindBox && transitionKindBox.currentIndex >= 0) {
            const kinds = EditorState.transitionKindsForTrack(root.transitionEditTrack)
            const item = kinds[transitionKindBox.currentIndex]
            if (item && item.kind !== (root.activeTransition.kind || "crossfade"))
                EditorState.setTransitionKind(
                    root.transitionEditTrack, transitionId, item.kind)
        }
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            root.transitionDataRevision++
            root.refreshFields()
        }
        function onSelectedTransitionDataChanged() {
            root.transitionDataRevision++
            root.refreshFields()
        }
        function onTracksChanged() {
            root.clipDataRevision++
            root.transitionDataRevision++
            root.refreshFields()
        }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: !root.hasActiveTransition && !root.canAddOutgoingTransition
            text: root.clipKind === "video" || root.clipKind === "shape" || root.clipKind === "text"
                  ? qsTr("Select where two clips overlap (shown in purple), or drag a clip so it overlaps the next one.")
                  : qsTr("Transitions work between two clips on a video, shape, or text track.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        Column {
            width: parent.width
            spacing: 8
            visible: !root.hasActiveTransition && root.canAddOutgoingTransition

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No transition after this clip. Add one at the cut to the next clip.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            // VEDIT (조박사님, Vegas): no transition without an overlap — the button says how instead.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Drag one clip over the next: where they overlap becomes a crossfade, as long as the overlap.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }

            // VEDIT (Vegas): every overlap on the track gets the crossfade, as long as the overlap.
            ThemedButton {
                text: qsTr("Crossfade at every overlap")
                onClicked: EditorState.applyTransitionToAllOverlaps(EditorState.selectedTrack, "crossfade")
            }
        }

        Column {
            width: parent.width
            spacing: 12
            visible: root.hasActiveTransition

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    const t = root.activeTransition
                    let line = t.overlapping
                        ? qsTr("Overlap transition. Drag another kind from Transitions to replace it.")
                        : qsTr("Transition to the next clip. Move across the cut to preview it.")
                    const h = t.handles || {}
                    if (!t.overlapping && (h.out === false || h.in === false))
                        line += " " + qsTr("One side has no media past the cut, so its sound fades through silence instead of crossfading.")
                    return line
                }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Type")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm // VEDIT: one size for every section label
                    font.weight: Font.Medium
                }
                ThemedComboBox {
                    id: transitionKindBox
                    width: parent.width
                    textRole: "label"
                    valueRole: "kind"
                    model: EditorState.transitionKindsForTrack(root.transitionEditTrack)
                    onActivated: transitionKindBox.commitTransitionKind()
                    onCurrentIndexChanged: {
                        if (root.suppressTransitionKindUpdate || !root.hasActiveTransition)
                            return
                        transitionKindBox.commitTransitionKind()
                    }

                    function commitTransitionKind() {
                        const item = model[currentIndex]
                        if (!item || !root.hasActiveTransition)
                            return
                        EditorState.setTransitionKind(
                            root.transitionEditTrack,
                            root.activeTransition.id, item.kind)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Row {
                    spacing: 10
                    Text {
                        text: qsTr("Duration")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm // VEDIT: one size for every section label
                        font.weight: Font.Medium
                    }
                    // VEDIT: the same length the timeline box shows ("1.15" = 1 s 15 frames), spelled
                    // out, so the seconds typed below and the box on the clip read as one number.
                    Text {
                        anchors.baseline: parent.children[0].baseline
                        visible: root.hasActiveTransition
                        readonly property int fps: [24, 25, 30, 60].indexOf(durationRulerSettings.rulerFps) >= 0
                                                   ? durationRulerSettings.rulerFps : 30
                        readonly property real secs: Number(root.activeTransition.duration) || 0
                        text: {
                            let whole = Math.floor(secs + 1e-6)
                            let f = Math.round((secs - whole) * fps)
                            if (f >= fps) { whole += 1; f = 0 }
                            return qsTr("= %1 s %2 frames (timeline %3)").arg(whole).arg(f)
                                .arg(whole + "." + String(f).padStart(String(fps - 1).length, "0"))
                        }
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }
                Settings {
                    id: durationRulerSettings
                    category: "timeline"
                    property int rulerFps: 30
                }
                // VEDIT: CapCut's length slider, up to half the shorter clip.
                ThemedSlider {
                    id: transitionDurationSlider
                    defaultValue: 0.5 // VEDIT: Transition's default
                    width: parent.width
                    label: qsTr("Duration")
                    lockWhilePlaying: true
                    from: 0.1
                    to: Math.max(0.2, root.hasActiveTransition ? (Number(root.activeTransition.maxDuration) || 5) : 5)
                    stepSize: 0.05
                    valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
                    Binding on value {
                        when: !transitionDurationSlider.pressed
                        value: root.hasActiveTransition ? (Number(root.activeTransition.duration) || 0.5) : 0.5
                    }
                    onPressedChanged: {
                        if (!pressed && root.hasActiveTransition)
                            EditorState.setTransitionDuration(root.transitionEditTrack, root.activeTransition.id, value)
                    }
                }
                ThemedNumberField {
                    id: transitionDurationField
                    to: root.hasActiveTransition ? (Number(root.activeTransition.maxDuration) || 60) : 60
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.05
                    from: 0.05
                    onEdited: v => {
                        if (!root.hasActiveTransition)
                            return
                        EditorState.setTransitionDuration(
                            root.transitionEditTrack, root.activeTransition.id, v)
                    }
                }
            }

            // VEDIT: Vegas's Fade Type grid — rows: how the outgoing clip fades out, columns: how
            // the incoming one fades in.
            Column {
                id: fadeTypeSection
                width: parent.width
                spacing: 6
                // VEDIT (조박사님): folded away until asked for.
                property bool open: false
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: fadeTypeSection.open ? Theme.icons.chevronDown : Theme.icons.chevronRight
                            iconSize: 14
                            iconColor: Theme.mutedForeground
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Fade type")
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: Font.Medium
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fadeTypeSection.open = !fadeTypeSection.open
                    }
                }
                CrossfadeCurveGrid {
                    visible: fadeTypeSection.open
                    cellWidth: Math.max(40, Math.min(56, (parent.width - 12) / 5))
                    cellHeight: 40
                    outCurve: root.activeTransition.outCurve || "smooth"
                    inCurve: root.activeTransition.inCurve || "smooth"
                    onPicked: function(o, i) {
                        if (root.hasActiveTransition)
                            EditorState.setTransitionFadeTypes(root.transitionEditTrack, root.activeTransition.id, o, i)
                    }
                }
            }

            // Remaps progress across the transition window. "Custom" hands off to the same
            // curve editor the clip fades use, scoped to this transition.
            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Curve")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm // VEDIT
                    font.weight: Font.Medium
                }
                ThemedComboBox {
                    id: transitionCurveBox
                    width: parent.width
                    readonly property var curveIds: ["linear", "smooth", "equalPower", "custom", "bezier"]
                    model: [qsTr("Linear"), qsTr("Smooth"), qsTr("Natural"), qsTr("Custom"), qsTr("Bezier")]
                    currentIndex: Math.max(0, curveIds.indexOf(
                                               root.activeTransition.easingCurve || "linear"))
                    onActivated: (index) => {
                        if (!root.hasActiveTransition)
                            return
                        const id = transitionCurveBox.curveIds[index]
                        if (id === "custom" || id === "bezier") {
                            root.Window.window.openTransitionCurve(
                                        root.transitionEditTrack, root.activeTransition.id)
                            return
                        }
                        EditorState.setTransitionEasing(
                                    root.transitionEditTrack, root.activeTransition.id, id)
                    }
                }
            }

            // Shader parameters declared by the active transition package.
            // Integer model: preview ticks replace params as a new list; a count
            // model keeps the pressed slider alive across those updates.
            // An audio track only hears the transition, so its picture parameters don't apply.
            Repeater {
                model: root.activeTransition.audioOnly ? 0 : (root.activeTransition.params || []).length
                delegate: Column {
                    id: trParamRow
                    required property int index
                    readonly property var paramData: (root.activeTransition.params || [])[index] || ({})
                    width: root.width
                    spacing: 4

                    Row {
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width - 48
                            elide: Text.ElideRight
                            text: trParamRow.paramData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm // VEDIT: same as the section labels
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: 40
                            horizontalAlignment: Text.AlignRight
                            text: trParamRow.paramData.isBoolean
                                  ? (trParamRow.paramData.value ? qsTr("On") : qsTr("Off"))
                                  : Number(trParamSlider.value).toFixed(
                                        Math.abs(trParamRow.paramData.max - trParamRow.paramData.min) >= 10 ? 1 : 2)
                            color: Theme.panelForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeSm // VEDIT
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    ThemedSwitch {
                        visible: !!trParamRow.paramData.isBoolean
                        checked: !!trParamRow.paramData.value
                        onToggled: EditorState.setTransitionParam(
                                       root.transitionEditTrack, root.activeTransition.id,
                                       trParamRow.paramData.key, checked ? 1 : 0)
                    }

                    ThemedSlider {
                        id: trParamSlider
                        defaultValue: trParamRow.paramData["default"] // VEDIT
                        lockWhilePlaying: true
                        label: trParamRow.paramData.label
                        visible: !trParamRow.paramData.isBoolean
                        width: parent.width
                        from: trParamRow.paramData.min
                        to: trParamRow.paramData.max
                        Binding on value {
                            when: !trParamSlider.pressed
                            value: trParamRow.paramData.value
                        }
                        onMoved: EditorState.previewSetTransitionParam(
                                     root.transitionEditTrack, root.activeTransition.id,
                                     trParamRow.paramData.key, value)
                        onPressedChanged: {
                            if (pressed)
                                EditorState.beginPreviewDrag(qsTr("Edit transition"))
                            else
                                EditorState.commitPreviewDrag()
                        }
                    }
                }
            }

            // VEDIT (조박사님): CapCut's 모두에 적용 — at the bottom, after the settings it copies.
            ThemedButton {
                text: qsTr("Apply to every overlap") // VEDIT (Vegas)
                onClicked: EditorState.applyTransitionToAllOverlaps(root.transitionEditTrack,
                                                                    root.activeTransition.kind)
            }

            ThemedButton {
                text: qsTr("Remove transition")
                variant: "destructive"
                onClicked: EditorState.removeTransition(
                               root.transitionEditTrack, root.activeTransition.id)
            }
        }
    }
}
