import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// CapCut-style Animation: In / Out / Combo (VEDIT), plus 계속 움직이기 for photos.
// Fade is one kind among slide/zoom/pop — same duration + style (Linear/Smooth/Natural/Custom).
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool supportsBodyAnim: clipKind === "video" || clipKind === "image"
                                             || clipKind === "shape" || clipKind === "text"
    readonly property bool supportsFade: supportsBodyAnim || clipKind === "audio"

    readonly property var animKinds: [
        "none", "fade", "slideUp", "slideDown", "slideLeft", "slideRight",
        "zoomIn", "zoomOut", "pop", "spinCW", "spinCCW", "bounce",
        "swing", "shake", "drop", "zoomSpin", "flyBig", // VEDIT
        "flipH", "flipV", "jelly", "stretchH", "stretchV", "riseUp", "whirl", "elastic", "diagonal", "whooshLeft", "whooshRight", "blink",
        "turn3d", "tilt3d", // VEDIT: 3D
        "glowIn", "glitchIn", "wipeIn", "blurIn", // VEDIT: special entrances
        "fadeRise", "squeeze", "rollIn", "heartbeat", "spin3d", "flipUp3d", "zoomBlurIn", "flashIn" // VEDIT: stage 7
    ]
    readonly property var animKindLabels: [
        qsTr("None"), qsTr("Fade"), qsTr("Slide up"), qsTr("Slide down"),
        qsTr("Slide left"), qsTr("Slide right"), qsTr("Zoom in"), qsTr("Zoom out"),
        qsTr("Pop"), qsTr("Spin CW"), qsTr("Spin CCW"), qsTr("Bounce"),
        qsTr("Swing"), qsTr("Shake"), qsTr("Drop"), qsTr("Spin and grow"), qsTr("Fly in big"),
        qsTr("Flip sideways"), qsTr("Flip over"), qsTr("Jelly"), qsTr("Open sideways"), qsTr("Open up and down"), qsTr("Shoot up"), qsTr("Whirl"), qsTr("Spring"), qsTr("Diagonal"), qsTr("Whoosh from left"), qsTr("Whoosh from right"), qsTr("Blink"),
        qsTr("3D turn in"), qsTr("3D tilt in"),
        qsTr("Glow in"), qsTr("Glitch in"), qsTr("Wipe in"), qsTr("Blur in"),
        qsTr("Fade up"), qsTr("Squeeze"), qsTr("Roll in"), qsTr("Heartbeat"), qsTr("3D spin in"), qsTr("3D flip up"), qsTr("Zoom blur in"), qsTr("Flash")
    ]
    readonly property var styleIds: ["linear", "smooth", "equalPower", "custom", "bezier"]
    readonly property var styleLabels: [qsTr("Linear"), qsTr("Smooth"), qsTr("Natural"), qsTr("Custom"), qsTr("Bezier")]

    readonly property var animIn: {
        void clipDataRevision
        return (clipData && clipData.animIn)
               ? clipData.animIn
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }
    readonly property var animOut: {
        void clipDataRevision
        return (clipData && clipData.animOut)
               ? clipData.animOut
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }

    // VEDIT: which tab of 들어오기 / 나가기 / 콤보 is showing; it opens on what the clip has.
    property string slot: "in"
    readonly property var currentAnim: slot === "out" ? animOut : animIn
    // VEDIT (조박사님, CapCut): the movements sorted into groups, a chip row to pick one.
    property string animCategory: "all"
    readonly property var animCategories: [
        { id: "all", label: qsTr("All") }, { id: "basic", label: qsTr("Basic") },
        { id: "dynamic", label: qsTr("Dynamic") }, { id: "3d", label: qsTr("3D") },
        { id: "special", label: qsTr("Light & glitch") }
    ]
    function animCategoryOf(kind) {
        if (["fade", "slideUp", "slideDown", "slideLeft", "slideRight", "zoomIn", "zoomOut", "pop",
             "stretchH", "stretchV", "diagonal", "fadeRise"].indexOf(kind) >= 0)
            return "basic"
        if (["flipH", "flipV", "turn3d", "tilt3d", "spin3d", "flipUp3d"].indexOf(kind) >= 0)
            return "3d"
        if (["glowIn", "glitchIn", "wipeIn", "blurIn", "blink", "zoomBlurIn", "flashIn"].indexOf(kind) >= 0)
            return "special"
        return "dynamic"
    }
    readonly property var motionTiles: {
        const out = []
        for (let i = 0; i < animKinds.length; ++i)
            if (animKinds[i] === "none" || animCategory === "all" || animCategoryOf(animKinds[i]) === animCategory)
                out.push({ id: animKinds[i], label: animKindLabels[i] })
        return out
    }
    // Three or more cards a row, as wide as the panel lets them be.
    readonly property int tileColumns: Math.max(3, Math.floor((width + 8) / 118))
    readonly property real tileSize: Math.floor((width - 8 * (tileColumns - 1)) / tileColumns)
    readonly property var comboTiles: [
        { id: "none", label: qsTr("None") },
        { id: "rock", label: qsTr("Rock") },
        { id: "zoomInOut", label: qsTr("Zoom in and out") },
        { id: "rotateDown", label: qsTr("Rotate down") },
        { id: "slideThrough", label: qsTr("Slide through") },
        { id: "bounceInOut", label: qsTr("Bounce in and out") },
        { id: "spinZoom", label: qsTr("Spin zoom") },
        { id: "pendulum", label: qsTr("Pendulum") },
        { id: "squash", label: qsTr("Squash and stretch") },
        { id: "flipThrough", label: qsTr("Flip through") },
        { id: "floatThrough", label: qsTr("Float through") }
    ]

    function currentIdFor(which) {
        if (which === "combo")
            return (root.clipData && root.clipData.animCombo) ? root.clipData.animCombo : "none"
        const a = which === "out" ? root.animOut : root.animIn
        return (a && a.kind) ? a.kind : "none"
    }

    function pick(which, id) {
        const t = EditorState.selectedTrack, c = EditorState.selectedClip
        if (which === "combo")
            EditorState.setClipAnimCombo(t, c, id === "none" ? "" : id)
        else
            root.setAnim(which === "out" ? "animOut" : "animIn", { kind: id })
        if (id !== "none")
            EditorState.previewClipAnimation(t, c, which) // CapCut plays the choice at once
    }

    // A clip with a combo opens on the 콤보 tab — only when another clip is picked, so an edit
    // elsewhere does not flip the tab under the user.
    property string _shownClipId: ""
    onClipDataChanged: {
        if (!root.hasSelection)
            return
        const id = root.clipData.id || ""
        if (id === root._shownClipId)
            return
        root._shownClipId = id
        if (root.clipData.animCombo)
            root.slot = "combo"
        else if (root.slot === "combo")
            root.slot = "in"
    }

    height: column.height
    implicitHeight: column.height

    function setAnim(which, patch) {
        EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip, which, patch)
    }

    function applyBoth(kind) {
        const curve = kind === "none" ? "smooth" : (root.animIn.curve || "smooth")
        const duration = 0.5
        root.setAnim("animIn", { kind: kind, duration: duration, curve: curve })
        root.setAnim("animOut", { kind: kind, duration: duration, curve: curve })
    }

    function styleIndexFor(anim) {
        const id = (anim && anim.curve) ? anim.curve : (root.clipData.fadeCurve || "smooth")
        return Math.max(0, root.styleIds.indexOf(id))
    }

    function setStyle(which, index) {
        const id = root.styleIds[index]
        if (id === "custom" || id === "bezier") {
            root.setAnim(which, { curve: id })
            root.Window.window.openFadeCurve(EditorState.selectedTrack, EditorState.selectedClip)
            return
        }
        root.setAnim(which, { curve: id })
    }

    function refreshFields() {
        if (root.supportsBodyAnim) {
            if (animDurationField && !animDurationField.activeFocus)
                animDurationField.value = root.currentAnim.duration || 0.5
        }
        if (root.clipKind === "audio") {
            if (audioFadeInField && !audioFadeInField.activeFocus)
                audioFadeInField.value = root.clipData.fadeIn || 0
            if (audioFadeOutField && !audioFadeOutField.activeFocus)
                audioFadeOutField.value = root.clipData.fadeOut || 0
        }
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()
    onSlotChanged: refreshFields()

    Column {
        id: column
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: !root.supportsFade
            width: parent.width
            compact: true
            glyph: Theme.icons.sparkles
            title: qsTr("Not available")
            hint: qsTr("Animation applies to video, image, shape, text, and audio.")
        }

        // ----- Audio: volume fades only ----------------------------------------------------
        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Fade in / out (volume)")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            visible: root.clipKind === "audio"
            width: parent.width
            spacing: 6

            ThemedNumberField {
                id: audioFadeInField
                to: Math.max(0.1, root.clipData.duration || 1)
                unit: "s"
                width: (parent.width - parent.spacing) / 2
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               v, root.clipData.fadeOut || 0)
            }
            ThemedNumberField {
                id: audioFadeOutField
                to: Math.max(0.1, root.clipData.duration || 1)
                unit: "s"
                width: (parent.width - parent.spacing) / 2
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               root.clipData.fadeIn || 0, v)
            }
        }

        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Style")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // VEDIT: Vegas fade types per edge, as pictures
        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Fade in")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
        }
        FadeCurvePicker {
            visible: root.clipKind === "audio"
            edge: "in"
            current: root.clipData.fadeCurve || "smooth"
            onPicked: function(c) {
                EditorState.setClipFadeEdgeCurve(EditorState.selectedTrack, EditorState.selectedClip, "in", c)
            }
        }
        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Fade out")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
        }
        FadeCurvePicker {
            visible: root.clipKind === "audio"
            edge: "out"
            current: root.clipData.fadeOutCurve || root.clipData.fadeCurve || "smooth"
            onPicked: function(c) {
                EditorState.setClipFadeEdgeCurve(EditorState.selectedTrack, EditorState.selectedClip, "out", c)
            }
        }

        ThemedComboBox {
            visible: false // VEDIT: replaced by the pictures above
            width: parent.width
            model: root.styleLabels
            currentIndex: Math.max(0, root.styleIds.indexOf(root.clipData.fadeCurve || "smooth"))
            onActivated: (index) => {
                const id = root.styleIds[index]
                if (id === "custom" || id === "bezier") {
                    EditorState.setClipFadeCurve(
                                EditorState.selectedTrack, EditorState.selectedClip, id)
                    root.Window.window.openFadeCurve(
                                EditorState.selectedTrack, EditorState.selectedClip)
                    return
                }
                EditorState.setClipFadeCurve(
                            EditorState.selectedTrack, EditorState.selectedClip, id)
            }
        }

        // ----- VEDIT: CapCut 애니메이션 — 들어오기 / 나가기 / 콤보 tabs over a grid of moving tiles -----
        Flow {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { label: qsTr("In"), slot: "in" },
                    { label: qsTr("Out"), slot: "out" },
                    { label: qsTr("Combo"), slot: "combo" }
                ]
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    labelSize: Theme.fontSizeSm // VEDIT: CapCut-sized
                    chipHeight: 28
                    selected: root.slot === modelData.slot
                    onClicked: root.slot = modelData.slot
                }
            }
        }

        Flow { // VEDIT (CapCut): group chips over the cards
            visible: root.supportsBodyAnim && root.slot !== "combo"
            width: parent.width
            spacing: 6
            Repeater {
                model: root.animCategories
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    labelSize: Theme.fontSizeXs
                    chipHeight: 24
                    selected: root.animCategory === modelData.id
                    onClicked: root.animCategory = modelData.id
                }
            }
        }

        Text {
            visible: root.supportsBodyAnim
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.slot === "combo"
                  ? qsTr("Combo moves the clip from its start to its end — it takes the place of In and Out.")
                  : root.slot === "in" ? qsTr("How the clip comes in. Point at a picture to see it move.")
                                       : qsTr("How the clip goes out. Point at a picture to see it move.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs // VEDIT: CapCut-sized
        }

        Flow {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 8

            Repeater {
                model: root.slot === "combo" ? root.comboTiles : root.motionTiles
                delegate: AnimPreviewTile {
                    required property var modelData
                    tileSize: root.tileSize // VEDIT
                    slot: root.slot
                    animId: modelData.id
                    label: modelData.label
                    selected: root.currentIdFor(root.slot) === modelData.id
                    onPicked: root.pick(root.slot, modelData.id)
                }
            }
        }

        // Length and style of the 들어오기 / 나가기 now shown (CapCut's 길이 slider).
        Row {
            visible: root.supportsBodyAnim && root.slot !== "combo" && root.currentIdFor(root.slot) !== "none"
            width: parent.width
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Length")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }
            ThemedSlider {
                id: animDurationSlider
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(60, parent.width - 70 - 64 - parent.spacing * 2)
                label: qsTr("Animation length")
                from: 0.1
                to: Math.max(0.2, root.clipData.duration || 1)
                stepSize: 0.05
                defaultValue: 0.5 // VEDIT
                Binding on value { value: root.currentAnim.duration || 0.5 } // VEDIT: survives a reset
                valueFormatter: function (v) { return Number(v).toFixed(1) + qsTr(" s") }
                onMoved: root.setAnim(root.slot === "in" ? "animIn" : "animOut", { duration: value })
            }
            ThemedNumberField {
                id: animDurationField
                anchors.verticalCenter: parent.verticalCenter
                to: 10
                unit: "s"
                width: 64
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => root.setAnim(root.slot === "in" ? "animIn" : "animOut", { duration: v })
            }
        }

        Row {
            visible: root.supportsBodyAnim && root.slot !== "combo" && root.currentIdFor(root.slot) !== "none"
            width: parent.width
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Style")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }
            // VEDIT: Vegas's fade types as pictures
            FadeCurvePicker {
                anchors.verticalCenter: parent.verticalCenter
                edge: root.slot === "out" ? "out" : "in"
                current: root.currentAnim.curve || "smooth"
                onPicked: function(c) { root.setAnim(root.slot === "in" ? "animIn" : "animOut", { curve: c }) }
            }
        }

        // ----- VEDIT: CapCut 콤보 — slow motion across the whole clip (photo slideshows) -----
        Text {
            visible: root.supportsBodyAnim
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Keep moving — slow motion for the whole clip, lovely on photos")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }

        Flow {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { label: qsTr("Slowly closer"), motion: "zoomIn" },
                    { label: qsTr("Slowly farther"), motion: "zoomOut" },
                    { label: qsTr("Left to right"), motion: "panRight" },
                    { label: qsTr("Right to left"), motion: "panLeft" },
                    { label: qsTr("Heartbeat"), motion: "pulse" },
                    { label: qsTr("Sway"), motion: "sway" },
                    { label: qsTr("Float"), motion: "float" },
                    { label: qsTr("Slow spin"), motion: "spinLoop" },
                    { label: qsTr("Shiver"), motion: "jitter" },
                    { label: qsTr("Hold still"), motion: "none" }
                ]
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: modelData.motion !== "none"
                              && (EditorState.selectedClipData.motionPreset || "") === modelData.motion
                    onClicked: EditorState.applyClipMotion(EditorState.selectedTrack, EditorState.selectedClip,
                                                           modelData.motion)
                }
            }
        }

        // VEDIT: the same movement on every photo at once (slideshow feel without remaking it).
        ThemedButton {
            visible: root.supportsBodyAnim && root.clipKind === "image"
            width: parent.width
            variant: "secondary"
            text: qsTr("Give every photo this movement")
            onClicked: EditorState.applyMotionToAllPhotos(EditorState.selectedTrack, EditorState.selectedClip)
        }

        ThemedButton {
            visible: root.supportsBodyAnim
                     && ((root.animIn.curve || "") === "custom"
                         || (root.animOut.curve || "") === "custom"
                         || (root.clipData.fadeCurve || "") === "custom")
            width: parent.width
            text: qsTr("Edit custom curve…")
            variant: "secondary"
            onClicked: root.Window.window.openFadeCurve(
                           EditorState.selectedTrack, EditorState.selectedClip)
        }

        Text {
            visible: root.supportsBodyAnim && root.clipKind === "text"
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Letter and word animations live in the Text tab, under Animate. This moves the whole clip.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
