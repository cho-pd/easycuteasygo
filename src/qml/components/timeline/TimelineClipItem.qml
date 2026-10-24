import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// A single clip on a timeline track: background/fade canvas, filmstrip or
// waveform, name/effects band, drag-to-move, trim handles and fade dots, plus
// the clip context menu. Positioning and edits are delegated back to the panel
// (pxPerSecond, snap/landing helpers, trackIndexAtY) and to EditorState.
Item {
    id: clipItem

    // Owning TimelinePanel (pxPerSecond, clipColor, trackIndexAtY, landing
    // preview + effect-drop state, tracks) and the enclosing column.
    //
    // trackRow is whatever Item this delegate is parented to — the track row itself, or the
    // clip-area / adjustment-lane wrapper inside it. Sizing off `parent` is what lets a clip in
    // a nested lane fit its strip without knowing lanes exist.
    //
    // Because this property exists, `trackRow` inside a binding AT THE CALL SITE resolves to it
    // and NOT to an enclosing `id: trackRow` — so a call site must never write
    // `trackIndex: trackRow.trackIndex`. It reads as the row's index and silently yields 0 when
    // the immediate parent is a wrapper, which renders every track's clips as track 0's. Bind
    // through the wrapper's own id instead (see trackClipArea / laneStrip in TimelinePanel).
    property var panel
    readonly property var trackRow: parent
    property var timelineColumn
    // Filled by Repeater when used as a delegate (AOT-safe).
    required property int index
    property int trackIndex
    property int clipIndex: index
    // Wider trim/move hit areas for phones; desktop leaves this false.
    property bool touchMode: false

    // Desktop timeline deletion is intentionally scoped to the clip that owns
    // keyboard focus. Clicking a clip already calls forceActiveFocus(), so this
    // does not steal Delete from the Media Bin, dialogs or other editor surfaces.
    //
    // macOS sends the key labelled Delete on MacBook keyboards as Backspace;
    // extended keyboards can also send the forward-delete Key_Delete.
    //
    // deleteSelectedClip() owns the A/V semantics:
    //   linked pair   -> delete the whole linked set
    //   unlinked pair -> delete only the current selection
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Delete
                || event.key === Qt.Key_Backspace)
                && clipItem.selected) {

            EditorState.deleteSelectedClip()
            event.accepted = true
        }
    }

    // The row object from EditorState.clipsModel(trackIndex), whose properties are that model's
    // roles. Required rather than passed in, so this can only ever be a delegate of that model.
    //
    // Roles, not a QVariantMap out of panel.tracks: that rebuilt and re-converted every clip in
    // the project on every edit, and handed each delegate a brand-new JS object, so every
    // binding through it re-ran even for a clip nothing had touched.
    required property var model
    readonly property var clipData: model
    // The revision, not the selection itself: reading EditorState.selection rebuilt a list of
    // maps for the whole selection, and this binding exists once per clip in the project.
    // VEDIT (조박사님): with a transition selected only the transition reads as selected — the
    // clip it starts from is held internally (for the inspector) but not drawn as picked.
    property bool selected: (EditorState.selectionRevision,
                             EditorState.selectedTransitionTrack < 0
                             && EditorState.selectionContains(trackIndex, clipIndex))
    // Live trim geometry, held here instead of being written to the project on every pointer
    // sample. Applying the edit per sample meant rebuilding the whole timeline model each time —
    // ~33 ms on a heavy project, several frames — for a change to one clip. The drag now previews
    // against these and commits once on release; EditorState.previewTrim* runs the same
    // computation the commit will, so the clip does not move when it lands.
    property bool trimPreviewActive: false
    // VEDIT (조박사님): an end dragged out over the next clip shows the crossfade (and its
    // seconds) while still dragging — the panel draws it from this live length.
    function reportLiveTrim() {
        if (trimPreviewActive)
            panel.setLiveTrim(trackIndex, clipData.id, trimPreviewStart, trimPreviewDuration)
        else
            panel.clearLiveTrim(clipData.id)
    }
    onTrimPreviewActiveChanged: reportLiveTrim()
    onTrimPreviewStartChanged: reportLiveTrim()
    onTrimPreviewDurationChanged: reportLiveTrim()
    property real trimPreviewStart: 0
    property real trimPreviewDuration: 0
    property real trimPreviewIn: 0
    property real trimPreviewOut: 0

    // The A/V companion of the clip being trimmed. The commit hands it identical timing, so it
    // follows the same preview rather than sitting still until release.
    readonly property bool trimFollowFollower: panel.trimFollowActive
                                               && !!clipData.linkId
                                               && clipData.linkId === panel.trimFollowLinkId
                                               && clipData.id !== panel.trimFollowClipId

    readonly property real effectiveStart: trimPreviewActive ? trimPreviewStart
                                           : trimFollowFollower ? panel.trimFollowStart
                                           : (clipData.start || 0)
    readonly property real effectiveDuration: trimPreviewActive ? trimPreviewDuration
                                              : trimFollowFollower ? panel.trimFollowDuration
                                              : (clipData.duration || 0)

    // The four clipData fields the waveform reads, hoisted to typed scalars. clipData is a
    // fresh JS object on every model change, so anything bound through it re-evaluated on every
    // edit anywhere in the project — re-querying up to 4096 peak buckets per audio clip and
    // repainting its Canvas. These compare by value, so they only propagate on a real change.
    readonly property string wfPath: clipData.path || ""
    readonly property real wfIn: trimPreviewActive ? trimPreviewIn
                                 : trimFollowFollower ? panel.trimFollowIn
                                 : (clipData.inPoint || 0)
    readonly property real wfOut: trimPreviewActive ? trimPreviewOut
                                  : trimFollowFollower ? panel.trimFollowOut
                                  : (clipData.outPoint || 0)
    readonly property int wfStream: clipData.audioStreamIndex || 0

    // Preview is playing this clip from a proxy (export still reads wfPath). The revision and the
    // two settings are named so the binding re-reads when any of them moves.
    readonly property bool previewUsesProxy: (AssetLibrary.badgeRevision,
                                              EditorState.playback.proxySize,
                                              EditorState.playback.useProxies
                                              && clipData.kind === "video"
                                              && AssetLibrary.hasProxyForPath(wfPath))
    // The bin asset behind this clip, by path: the clip model carries no asset id.
    readonly property string mediaAssetId: (AssetLibrary.badgeRevision,
                                            clipData.kind === "video"
                                            ? AssetLibrary.assetIdForPath(wfPath) : "")
    readonly property bool mediaEditFriendly: (AssetLibrary.badgeRevision,
                                               mediaAssetId.length > 0
                                               && AssetLibrary.isEditFriendly(mediaAssetId))
    readonly property bool mediaVariableFrameRate: (AssetLibrary.badgeRevision,
                                                    mediaAssetId.length > 0 && !mediaEditFriendly
                                                    && AssetLibrary.isVariableFrameRate(mediaAssetId))

    property string trackType: panel.tracks[trackIndex].type
    // Track::ClipDisplay: 0 filmstrip, 1 filmstrip + waveform bar, 2 waveform only.
    readonly property int clipDisplay: panel.tracks[trackIndex].clipDisplay !== undefined
                                       ? panel.tracks[trackIndex].clipDisplay : 1
    // VEDIT: a photo on the video track has no sound — it always shows its picture, full height.
    readonly property bool isStill: clipData.kind === "image"
    readonly property bool showWaveform: clipDisplay === 2 && !isStill
    // CapCut-style strip under the filmstrip. Dropped once the audio has been separated
    // (its waveform is on the audio track then) and when the body is too short to split.
    readonly property bool showWaveformBar: trackType === "video" && clipDisplay === 1 && !isStill
                                            && clipData.hasEmbeddedAudio === true
                                            && height - headerBandHeight >= 30
    // VEDIT (조박사님): a video whose sound was separated (or that has none) keeps the sound strip
    // as an empty black band, so it reads at a glance that its sound lives elsewhere.
    readonly property bool showEmptyAudioBar: trackType === "video" && clipDisplay === 1 && !isStill
                                              && clipData.kind === "video"
                                              && clipData.hasEmbeddedAudio !== true
                                              && height - headerBandHeight >= 30
    // VEDIT: a taller strip so stereo can split into L/R lanes under the picture.
    readonly property real waveformBarHeight:
        Math.max(16, Math.round((height - headerBandHeight) * 0.45))
    property bool showChannelWaveforms: panel.tracks[trackIndex].showChannelWaveforms === true
    property var clipEffects: clipData.effects || []
    property var clipAudioEffects: clipData.audioEffects || []
    readonly property bool hasAnyEffects: clipEffects.length > 0 || clipAudioEffects.length > 0
    readonly property string effectsLabelText: {
        const names = []
        for (var i = 0; i < clipEffects.length; i++) {
            const fx = clipEffects[i]
            const label = fx.label || qsTr("Effect")
            names.push(fx.enabled === false ? qsTr("%1 (off)").arg(label) : label)
        }
        for (var j = 0; j < clipAudioEffects.length; j++) {
            const afx = clipAudioEffects[j]
            const alabel = afx.label || qsTr("Effect")
            names.push(afx.enabled === false ? qsTr("%1 (off)").arg(alabel) : alabel)
        }
        return names.join(" · ")
    }
    // An adjustment is its stack — the name ("Adjustment Layer") says nothing the colour and
    // lane do not already. So it shows the effects it carries, and falls back to naming its kind
    // only while it is still empty.
    readonly property string adjustmentLabelText: {
        if (clipItem.effectsLabelText.length > 0)
            return clipItem.effectsLabelText
        // Nothing in it yet: a name the user gave it, else what kind of layer it is.
        if (clipItem.clipData.name && clipItem.clipData.name.length > 0)
            return clipItem.clipData.name
        const kind = clipItem.clipData.adjustmentKind
        if (kind === "audioEffects") return qsTr("Audio adjustment")
        if (kind === "mask") return qsTr("Mask")
        return qsTr("Adjustment")
    }

    // Named so tooling (and a screen reader) can address a clip by what the user sees on it
    // rather than by pixel position. The timeline is the app's main interaction surface and had
    // no accessible identity at all.
    Accessible.role: Accessible.Button
    Accessible.name: {
        const label = clipItem.clipData.name && clipItem.clipData.name.length > 0
                    ? clipItem.clipData.name
                    : clipItem.adjustmentLabelText
        return qsTr("%1, track %2").arg(label).arg(clipItem.trackIndex + 1)
    }
    Accessible.selected: clipItem.selected
    Accessible.onPressAction: EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)

    property bool effectDropTarget: panel.effectDropTrackIndex === trackIndex
                                    && panel.effectDropClipIndex === clipIndex
    // Subtitles keep cue-owned timing; text clips use the same edge fades as video.
    readonly property bool timelineFadeHandles: trackType !== "subtitle"

    // Gain along a fade ramp (progress 0..1). Mirrors Clip::shapeFade / FadeShape.
    function fadeGainAt(progress, curveOverride) {
        const t = Math.max(0, Math.min(1, progress))
        const curve = curveOverride || clipItem.clipData.fadeCurve || "smooth"
        if (curve === "linear")
            return t
        if (curve === "equalPower")
            return Math.sin(t * Math.PI * 0.5)
        if (curve === "slow") // VEDIT
            return 1 - Math.cos(t * Math.PI * 0.5)
        if (curve === "sharp") // VEDIT
            return 0.5 - Math.sin(Math.asin(1 - 2 * t) / 3)
        if (curve === "bezier") {
            // Mirrors FadeShape::bezierAt — anchors pinned at (0,0)/(1,1), solve x for t.
            const h = clipItem.clipData.fadeHandles || [0.42, 0.0, 0.58, 1.0]
            let lo = 0, hi = 1
            for (let i = 0; i < 24; ++i) {
                const mid = (lo + hi) / 2
                const mt = 1 - mid
                const x = 3 * mt * mt * mid * h[0] + 3 * mt * mid * mid * h[2] + mid * mid * mid
                if (x < t) lo = mid; else hi = mid
            }
            const u = (lo + hi) / 2
            const mu = 1 - u
            return 3 * mu * mu * u * h[1] + 3 * mu * u * u * h[3] + u * u * u
        }
        if (curve === "custom") {
            const pts = clipItem.clipData.fadeShape || []
            if (pts.length < 2)
                return t
            if (t <= pts[0].t)
                return pts[0].g
            if (t >= pts[pts.length - 1].t)
                return pts[pts.length - 1].g
            for (let i = 0; i + 1 < pts.length; ++i) {
                const a = pts[i]
                const b = pts[i + 1]
                if (t < a.t || t > b.t)
                    continue
                const span = b.t - a.t
                if (Math.abs(span) < 1e-9)
                    return b.g
                const u = (t - a.t) / span
                return a.g + (b.g - a.g) * u
            }
            return pts[pts.length - 1].g
        }
        // smooth (smoothstep)
        return t * t * (3.0 - 2.0 * t)
    }

    // Four ToolTips — each a Popup — existed on every clip in the project for the sake of the
    // one clip the pointer is actually over. Gated on the clip being touched at all; each
    // tooltip keeps its own visible binding inside, so the reveal delay and the conditions are
    // exactly what they were.
    readonly property bool tooltipsActive:
        clipMouse.containsMouse || clipMouse.pressed
        || leftTrimMouse.containsMouse || leftTrimMouse.pressed || leftTrimHover.hovered
        || rightTrimMouse.containsMouse || rightTrimMouse.pressed || rightTrimHover.hovered
        || fadeInMouse.containsMouse || fadeInMouse.pressed
        || fadeOutMouse.containsMouse || fadeOutMouse.pressed

    // Arms the Loader holding the context menu and pops it. Re-pops an already-loaded menu
    // rather than reloading it, for the second right-click that lands while the first is still
    // closing.
    function openContextMenu() {
        if (clipContextMenuLoader.active && clipContextMenuLoader.item) {
            clipContextMenuLoader.item.popup()
            return
        }
        clipContextMenuLoader.active = true
    }

    // Premiere-style trim pointer (vertical bar + arrow), sized to this clip.
    //
    // A press owns the cursor for the whole gesture. containsMouse goes false the moment the
    // pointer leaves the strip, and during a trim the handle geometry trails the pointer by a
    // frame — so it flapped, and every flap was a restoreOverrideCursor()/setOverrideCursor()
    // pair with a freshly rasterised pixmap behind it.
    readonly property int trimCursorSide: leftTrimMouse.pressed ? -1
                                          : rightTrimMouse.pressed ? 1
                                          : leftTrimMouse.containsMouse ? -1
                                          : rightTrimMouse.containsMouse ? 1 : 0
    readonly property int trimCursorHeight: Math.round(height)
    // Identifies this clip to the shared override cursor. Not derived from trackIndex/clipIndex:
    // those shift when a clip is inserted or removed, which would orphan a held cursor.
    property int trimCursorToken: 0
    function applyTrimCursor() {
        if (trimCursorSide !== 0 && trimCursorToken === 0)
            trimCursorToken = EditorState.acquireTrimCursorToken()
        EditorState.setTimelineTrimCursor(trimCursorSide, trimCursorHeight, trimCursorToken)
        if (trimCursorSide === 0)
            trimCursorToken = 0
    }
    onTrimCursorSideChanged: applyTrimCursor()
    onTrimCursorHeightChanged: if (trimCursorSide !== 0) applyTrimCursor()
    Component.onDestruction: {
        if (trimCursorSide !== 0)
            EditorState.setTimelineTrimCursor(0, 0, trimCursorToken)
        if (lifted && typeof panel.setScrollLocked === "function")
            panel.setScrollLocked(false)
    }

    // An adjustment pinned to a clip takes its extent from that clip, so its edges are not the
    // user's to drag — unlink it first and they become live.
    readonly property bool pinnedToClip: clipData.kind === "adjustment"
                                         && !!clipData.linkedClipId

    // Trim handles stay on whenever selected.
    // Width is floored so the clip never becomes
    // an unusable sliver; at that floor both
    // edges stay trimmable and the middle moves.
    readonly property bool showTrimHandles: selected && !pinnedToClip
    readonly property real minDurationSeconds: Math.max(
        Theme.clipMinDurationSeconds,
        Theme.clipMinWidth / panel.pxPerSecond)
    readonly property real trimHandleWidth: touchMode
                                            ? Theme.androidClipTrimHandleWidth
                                            : Theme.clipTrimHandleWidth
    // Clamped against the clip's own width: a minimum-width (56dp) clip inset by a
    // fixed 22dp each side left a 12dp movable core, and the edge bands are dead
    // until the clip is selected — so short clips could not be tapped at all.
    readonly property real edgeMargin: touchMode
                                       ? Math.min(Theme.androidClipEdgeMargin, width / 4)
                                       : 14
    // VEDIT (조박사님): on desktop the trim grab starts at the clip's own edge — no trim bar
    // appears with the pointer outside the clip.
    readonly property real trimHotspotExtra: touchMode
                                             ? Theme.androidTrimHotspotExtra
                                             : 0

    // --- Trim strips vs. scrolling the layers ---------------------------------------------
    // On touch each trim strip is ~38dp wide and runs the full height of the clip, at both
    // edges, and it holds the grab: MouseArea copies preventStealing into stealMouse once, in
    // mousePressEvent, so nothing that starts on a strip can ever be handed back to the
    // Flickable. It has to hold it, or the Flickable would steal a horizontal trim mid-drag.
    //
    // The cost is that those two bands cover the clip you are working on, which is exactly
    // where a finger lands when it goes to scroll to another layer — and with the timeline
    // pane only a couple of rows tall, scrolling is not a rare thing to want. So the strips
    // arbitrate instead of refusing: the first movement past the drag threshold decides
    // whether the gesture is a trim or a scroll, and a scroll drives the timeline for the rest
    // of the gesture. Whichever it picks, it keeps for the whole press, so a trim cannot turn
    // into a scroll halfway through a frame-accurate drag.
    //
    // Scene coordinates throughout. Scrolling moves this item under a finger that has not
    // moved, so in local coordinates the pointer would appear to travel and feed the scroll
    // back into itself.
    property int edgeAxis: 0 // 0 undecided, 1 trimming, 2 scrolling
    property real edgePressSceneX: 0
    property real edgePressSceneY: 0
    property real edgeLastSceneY: 0

    function beginEdgeGesture(area, mouse) {
        const p = area.mapToItem(null, mouse.x, mouse.y)
        // Desktop has a pointer and a scroll wheel and never needed this; starting it already
        // decided keeps every branch below dead there.
        edgeAxis = touchMode ? 0 : 1
        edgePressSceneX = p.x
        edgePressSceneY = p.y
        edgeLastSceneY = p.y
    }

    // True when this move belongs to the timeline rather than to the strip, in which case it
    // has already been applied and the caller must not also trim.
    function edgeGestureScrolled(area, mouse) {
        if (edgeAxis === 1)
            return false
        const p = area.mapToItem(null, mouse.x, mouse.y)
        if (edgeAxis === 0) {
            const dx = Math.abs(p.x - edgePressSceneX)
            const dy = Math.abs(p.y - edgePressSceneY)
            const slop = Qt.styleHints.startDragDistance
            // Vertical has to both clear the threshold and beat horizontal: a trim is the
            // reason these strips exist, so anything ambiguous stays a trim.
            if (dy > slop && dy > dx)
                edgeAxis = 2
            else if (dx > slop)
                edgeAxis = 1
            if (edgeAxis !== 2)
                return false
            // Re-anchored at the moment it arms, so the view does not jump by the threshold
            // that was spent deciding.
            edgeLastSceneY = p.y
        }
        // Shares the drag autoscroll's mover, which already clamps to the content bounds --
        // writing contentY directly goes around boundsBehavior. Android-only, like it.
        if (typeof panel.dragEdgeScroll === "function")
            panel.dragEdgeScroll(0, edgeLastSceneY - p.y)
        edgeLastSceneY = p.y
        return true
    }

    // Fade dots. On touch they used to be switched off outright: at the clip corners
    // they landed inside the trim strips, which own the full height of both edges.
    // Instead they are sized to a fingertip and inset past those strips, and — like the
    // trim handles — only appear on the selected clip. Desktop keeps the 13px corner
    // dots and a zero inset, so nothing there moves.
    readonly property real fadeHandleSize: touchMode ? 22 : 13
    // Clears the trim strip's own hotspot (trimHandleWidth + 4) plus the dot's -6
    // hit margin, so the two never contend for the same press.
    readonly property real fadeHandleInset: touchMode ? trimHandleWidth + 12 : 0
    readonly property real fadeHandleMinWidth: touchMode
                                               ? 2 * fadeHandleInset + fadeHandleSize + 12
                                               : 26

    // Name band height, derived once instead of
    // being hardcoded at three separate sites,
    // and clamped so it can never swallow a
    // short (25px) text or subtitle row.
    // One line now: the effect list that used to need a taller band moved to the lane.
    // VEDIT (조박사님, as in Vegas): a sound clip has no name band — its waveform fills the whole
    // clip and the name sits on top of it (audioNameTag below).
    readonly property real headerBandHeight: trackType === "audio" ? 0
        : Math.min(Theme.clipHeaderBandHeight, Math.max(0, height * 0.5))

    // VEDIT: clips start exactly at their time (no 1.5 px shift), so a cut lines up with the
    // playhead and the ruler. Vertical inset stays for the selection ring.
    readonly property real xInset: 0
    y: Theme.clipSelectionRingWidth
    // Floored so short clips stay visible and
    // trimmable even at low zoom.
    // VEDIT: exactly the clip's length — no inset at either end, so two clips that touch in time
    // touch on screen. The 3 px it used to lose read as a missing frame (조박사님).
    width: Math.max(Theme.clipMinWidth, effectiveDuration * panel.pxPerSecond)
    height: Math.max(0, trackRow.height - 2 * Theme.clipSelectionRingWidth)

    // A clip scrolled off the side of the timeline still cost a full set of scene-graph nodes,
    // a hit-test entry and (for audio) a Canvas framebuffer. ClipFilmstrip already culls its own
    // tiles from exactly these inputs; this extends the rule to the clip body.
    readonly property real viewportPad: 256
    readonly property bool inViewport: panel.timelineViewW <= 0
        || ((x + width) >= panel.timelineViewX - viewportPad
            && x <= panel.timelineViewX + panel.timelineViewW + viewportPad)

    // The exceptions are not cosmetic. A drag that autoscrolls past the edge must not cull its
    // own subject; a partner following a multi-clip move is moved from outside itself; and a
    // selected clip scrolled out of view still owns the keyboard focus that Delete arrives on.
    visible: inViewport
             || clipMouse.drag.active || clipMouse.pressed
             || leftTrimMouse.pressed || rightTrimMouse.pressed
             || moveFollowFollower
             || activeFocus

    property real lastPreviewDesired: -1
    property int lastPreviewTrack: -1

    // While dragging, show the same snapped landing outline the
    // library drop uses, on whichever track the clip is over.
    //
    // Gated on `moving` rather than drag.active: Qt only clears drag.active
    // after onReleased returns, so resetting y on drop re-armed the preview and
    // left an orphaned outline on the old track.
    function updateMovePreview() {
        if (!clipMouse.moving)
            return
        // Keep linked/selected partners locked to this clip's live X and Y.
        panel.updateMoveFollow(x - clipMouse.moveOriginX,
                               y - Theme.clipSelectionRingWidth)
        const desired = Math.max(0, (x - clipItem.xInset) / panel.pxPerSecond)
        const pos = mapToItem(timelineColumn, width / 2, height / 2)
        const targetTrack = panel.trackIndexAtY(pos.y)
        const effTrack = targetTrack >= 0 ? targetTrack : trackIndex
        if (Math.abs(desired - lastPreviewDesired) * panel.pxPerSecond >= 1 || effTrack !== lastPreviewTrack) {
            lastPreviewDesired = desired
            lastPreviewTrack = effTrack
            panel.showLandingPreview(effTrack, desired, clipData.duration, clipData.id)
        }
    }
    onXChanged: updateMovePreview()
    onYChanged: updateMovePreview()

    // CapCut-style: selected/linked partners slide with the dragged clip.
    readonly property bool moveFollowFollower: panel.moveFollowActive
                                              && selected
                                              && !(panel.moveLeaderTrack === trackIndex
                                                   && panel.moveLeaderClip === clipIndex)
    readonly property real followOffsetX: moveFollowFollower ? panel.moveFollowDeltaX : 0
    readonly property real followOffsetY: (moveFollowFollower && trackIndex === panel.moveLeaderTrack)
                                          ? panel.moveFollowDeltaY : 0

    // VEDIT: stands aside while a dragged clip is about to be inserted before it (끼워 넣기).
    readonly property real insertShiftX: (panel.insertTrack === trackIndex && panel.insertTrack >= 0
                                          && clipData.id !== panel.insertExcludeId
                                          && clipItem.effectiveStart >= panel.insertSeamSec - 0.001)
                                         ? panel.insertDurSec * panel.pxPerSecond : 0

    Binding {
        target: clipItem
        property: "x"
        when: !clipMouse.drag.active
        value: Math.max(clipItem.xInset,
                        clipItem.effectiveStart * panel.pxPerSecond
                        + clipItem.xInset
                        + clipItem.followOffsetX
                        + clipItem.insertShiftX)
    }

    Binding {
        target: clipItem
        property: "y"
        when: !clipMouse.drag.active
        value: Theme.clipSelectionRingWidth + followOffsetY
    }

    // No `Behavior on y` here. A drop settle looks appealing but cannot work at this
    // seam: onReleased reads clipItem.y through mapToItem to decide the target track,
    // and MouseArea.drag writes y directly during the drag. Animating y makes that
    // read lag the pointer, so trackIndexAtY resolves back to the origin track and a
    // cross-track drag silently becomes a same-track move. Gating the Behavior on
    // drag.active does not save it — Qt clears drag.active only after onReleased
    // returns, so y is already reset by then and the settle never plays anyway.
    // Touch: the hold has to be visible or there is no way to know the clip is now
    // liftable rather than still waiting. Lifts the clip out of the row and above its
    // neighbours for as long as the finger owns it.
    readonly property bool lifted: clipItem.touchMode
                                  && (clipMouse.moveArmed || clipMouse.drag.active)
    z: (lifted || clipMouse.drag.active || (moveFollowFollower && Math.abs(followOffsetY) > 1)) ? 10 : 0
    scale: lifted ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    // VEDIT: 트랙 잠금.
    readonly property bool trackLocked: !!(panel.tracks && panel.tracks[trackIndex]
                                           && panel.tracks[trackIndex].locked === true)

    // VEDIT (조박사님): a video clip's effects ride in the upper half of its band, edge to edge,
    // drawn under the name (which keeps its place and size); the effect list sits at the right.
    // This item only takes the clicks on that list: click opens the effects, right-click offers
    // 효과 줄 분리 — the effects leave for a lane of their own and stop following the clip.
    // VEDIT (조박사님): what the yellow edge handles stand for — a movement's length when the
    // side has one (들어오기 / 나가기), else the fade.
    readonly property real edgeInLength: (clipData.animInLength || 0) > 0 ? clipData.animInLength : (clipData.fadeIn || 0)
    readonly property real edgeOutLength: (clipData.animOutLength || 0) > 0 ? clipData.animOutLength : (clipData.fadeOut || 0)
    readonly property bool fxShown: trackType === "video" && clipData.kind !== "adjustment"
                                    && clipEffects.length > 0 && headerBandHeight > 0
    // VEDIT (조박사님): a speed change counts as an effect too — the upper half of the band turns
    // red and says how fast (4×).
    readonly property real clipSpeed: Number(clipData.speed) || 1
    readonly property bool speedShown: (clipData.kind === "video" || clipData.kind === "audio")
                                       && (Math.abs(clipSpeed - 1) > 0.001 || clipData.hasSpeedCurve === true)
                                       && headerBandHeight > 0
    readonly property string speedText: !speedShown ? ""
                                        : (clipData.hasSpeedCurve === true ? qsTr("Speed curve")
                                                                           : Number(clipSpeed.toFixed(2)) + "×")
    Item {
        id: fxBand
        visible: clipItem.fxShown
        z: 3
        y: 0
        height: Math.round(clipItem.headerBandHeight / 2)
        width: Math.min(fxListText.implicitWidth + 8, parent.width / 2)
        x: parent.width - width - clipItem.edgeMargin

        MouseArea {
            id: fxMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                EditorState.noteTrackClicked(clipItem.trackIndex)
                if (mouse.button === Qt.RightButton)
                    fxMenu.popup()
                else
                    EditorState.selectEffectHost(clipItem.trackIndex, clipItem.clipIndex)
            }
        }

        ThemedContextMenu {
            id: fxMenu
            ThemedMenuItem {
                text: qsTr("Open effects")
                icon.name: Theme.icons.wand
                onTriggered: EditorState.selectEffectHost(clipItem.trackIndex, clipItem.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Separate effect lane")
                icon.name: Theme.icons.unlink
                onTriggered: EditorState.separateEffectLane(clipItem.trackIndex, clipItem.clipIndex)
            }
        }
    }

    Rectangle {
        id: clipBackground
        anchors.fill: parent
        radius: Theme.clipCornerRadius // VEDIT: rounded so the meeting point of two clips shows
        // Lightens on hover — previously nothing
        // in the clip reacted to the pointer.
        color: {
            if (clipItem.clipData.kind === "adjustment") {
                const base = panel.adjustmentColor(clipItem.clipData.adjustmentKind)
                const lit = clipMouse.containsMouse || clipItem.lifted
                return lit ? Qt.lighter(base, 1.15) : base
            }
            let base = panel.clipColor(
                clipItem.clipData.kind === "composite" ? "composite"
                : clipItem.trackType === "shape" ? "graphic" : clipItem.trackType)
            // VEDIT (조박사님): a sound split off a video that no longer lines up with it gets a
            // little red — no number, just the hint.
            if (clipItem.clipData.outOfSync === true)
                base = Qt.tint(base, Qt.rgba(0.95, 0.12, 0.12, 0.38))
            const lit = clipMouse.containsMouse || clipItem.lifted
            return lit ? Qt.lighter(base, 1.15) : base
        }
        border.width: clipItem.effectDropTarget
                      ? Theme.borderWidthFocus
                      : (clipItem.selected ? 2 : 0) // VEDIT (조박사님): a 2 px yellow line round a picked clip
        border.color: clipItem.effectDropTarget ? Theme.clipEffect : Theme.primary
        clip: true

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.width {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            anchors.fill: parent
            visible: clipItem.effectDropTarget
            color: Qt.rgba(Theme.clipEffect.r, Theme.clipEffect.g, Theme.clipEffect.b, 0.28)
            z: 4
        }

        // VEDIT: CapCut's animation marks along the clip's bottom edge — a band the length of the
        // 들어오기 from the left, of the 나가기 from the right, or across the whole clip for a 콤보,
        // each with a small arrow pointing the way the clip moves.
        Item {
            id: animMarks
            anchors.fill: parent
            z: 3
            readonly property real inW: Math.min(width, (clipItem.clipData.animInLength || 0) * panel.pxPerSecond)
            readonly property real outW: Math.min(width, (clipItem.clipData.animOutLength || 0) * panel.pxPerSecond)
            readonly property bool combo: (clipItem.clipData.animComboId || "").length > 0
            readonly property color markColor: Qt.rgba(1.0, 0.82, 0.35, 0.85)

            // VEDIT (조박사님): the 들어오기 / 나가기 stretch of the clip faintly tinted, fading toward
            // the middle, so where a movement plays reads at a glance.
            Rectangle {
                visible: !animMarks.combo && animMarks.inW > 1
                x: 0
                width: animMarks.inW
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(1.0, 0.85, 0.4, 0.22) }
                    GradientStop { position: 1.0; color: Qt.rgba(1.0, 0.85, 0.4, 0.06) }
                }
            }
            Rectangle {
                visible: !animMarks.combo && animMarks.outW > 1
                x: parent.width - width
                width: animMarks.outW
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(1.0, 0.85, 0.4, 0.06) }
                    GradientStop { position: 1.0; color: Qt.rgba(1.0, 0.85, 0.4, 0.22) }
                }
            }

            Rectangle {
                visible: animMarks.combo
                x: 0
                y: parent.height - height
                width: parent.width
                height: 4
                color: animMarks.markColor
                opacity: 0.8
            }
            Rectangle {
                visible: !animMarks.combo && animMarks.inW > 1
                x: 0
                y: parent.height - height
                width: animMarks.inW
                height: 4
                color: animMarks.markColor
                Text {
                    visible: parent.width >= 14
                    anchors.right: parent.right
                    anchors.bottom: parent.top
                    text: "\u203A" // ›
                    color: animMarks.markColor
                    font.pixelSize: 14
                    font.bold: true
                }
            }
            Rectangle {
                visible: !animMarks.combo && animMarks.outW > 1
                x: parent.width - width
                y: parent.height - height
                width: animMarks.outW
                height: 4
                color: animMarks.markColor
                Text {
                    visible: parent.width >= 14
                    anchors.left: parent.left
                    anchors.bottom: parent.top
                    text: "\u203A"
                    color: animMarks.markColor
                    font.pixelSize: 14
                    font.bold: true
                }
            }
        }

        // Fade ramp overlays — sized only to the fade spans so a multi-hour clip
        // does not allocate a hundreds-of-thousands-px Canvas framebuffer.
        Canvas {
            id: fadeInCanvas
            x: 0
            y: 0
            z: 2
            width: Math.max(0, Math.min(parent.width,
                                        (clipItem.clipData.fadeIn || 0) * panel.pxPerSecond))
            height: parent.height
            visible: width > 0.5
            // Curve / custom shape changes must redraw even when width is unchanged.
            property string curveKey: (clipItem.clipData.fadeCurve || "smooth")
                                      + "|" + JSON.stringify(clipItem.clipData.fadeShape || [])
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onCurveKeyChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (width < 0.5 || height < 0.5)
                    return
                ctx.fillStyle = "rgba(0,0,0,0.38)"
                ctx.strokeStyle = "rgba(255,255,255,0.9)"
                ctx.lineWidth = 1.5
                const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                for (let i = steps; i >= 0; --i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(t)
                    ctx.lineTo(t * width, height * (1.0 - g))
                }
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                for (let i = 0; i <= steps; ++i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(t)
                    const x = t * width
                    const y = height * (1.0 - g)
                    if (i === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }

        Canvas {
            id: fadeOutCanvas
            y: 0
            z: 2
            width: Math.max(0, Math.min(parent.width,
                                        (clipItem.clipData.fadeOut || 0) * panel.pxPerSecond))
            height: parent.height
            x: parent.width - width
            visible: width > 0.5
            // VEDIT: the fade-out has its own curve
            readonly property string outCurve: clipItem.clipData.fadeOutCurve || clipItem.clipData.fadeCurve || "smooth"
            property string curveKey: outCurve
                                      + "|" + JSON.stringify(clipItem.clipData.fadeShape || [])
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onXChanged: requestPaint()
            onCurveKeyChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (width < 0.5 || height < 0.5)
                    return
                ctx.fillStyle = "rgba(0,0,0,0.38)"
                ctx.strokeStyle = "rgba(255,255,255,0.9)"
                ctx.lineWidth = 1.5
                const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                // Fade-out: progress from full (left of wedge) to silent (right edge).
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                for (let i = steps; i >= 0; --i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(1.0 - t, fadeOutCanvas.outCurve)
                    ctx.lineTo(t * width, height * (1.0 - g))
                }
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                for (let i = 0; i <= steps; ++i) {
                    const t = i / steps
                    const g = clipItem.fadeGainAt(1.0 - t, fadeOutCanvas.outCurve)
                    const x = t * width
                    const y = height * (1.0 - g)
                    if (i === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }

        Rectangle { // VEDIT: the empty sound strip of a video whose sound was separated
            visible: clipItem.showEmptyAudioBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: clipItem.waveformBarHeight
            color: Qt.rgba(0, 0, 0, 0.35) // 조박사님: the same as the sound strip of a clip with sound
            z: 0
        }

        ClipFilmstrip {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: (clipItem.trackType === "video"
                                || clipItem.trackType === "audio"
                                || clipItem.trackType === "shape")
                               ? clipItem.headerBandHeight
                               : 0
            anchors.bottomMargin: (clipItem.showWaveformBar || clipItem.showEmptyAudioBar) ? clipItem.waveformBarHeight : 0
            visible: clipItem.clipData.filmstripPath
                     && clipItem.clipData.filmstripPath.length > 0
                     && !clipItem.showWaveform
                     && (clipItem.trackType === "video"
                         || clipItem.trackType === "shape"
                         || clipItem.clipData.kind === "image")
            filmstripPath: clipItem.clipData.filmstripPath
            // Video only: on-demand tiles need a decodable video stream, and images/shapes
            // have a single poster frame that the strip already covers exactly.
            sourcePath: clipItem.clipData.kind === "video" ? (clipItem.clipData.path || "") : ""
            rotationCorrection: clipItem.clipData.rotationCorrection || 0
            inPoint: clipItem.wfIn
            outPoint: clipItem.wfOut
            sourceDuration: clipItem.clipData.sourceDuration
            // Image, vector and model "strips" are a single poster frame.
            frameCount: (clipItem.clipData.kind === "image" || clipItem.clipData.kind === "vector"
                         || clipItem.clipData.kind === "model3d") ? 1 : 8
            // Viewport-cull tiles so multi-hour clips don't spawn thousands of Images.
            worldX: clipItem.x
            viewX: panel.timelineViewX
            viewW: panel.timelineViewW
            refreshEpoch: panel.filmstripRefreshEpoch !== undefined
                          ? panel.filmstripRefreshEpoch : 0
            z: 0
        }

        Rectangle {
            visible: clipItem.trackType === "video"
                     || clipItem.trackType === "audio"
                     || clipItem.trackType === "shape"
            width: parent.width
            height: clipItem.headerBandHeight
            // Green band marks a clip the preview plays from its proxy, readable at any zoom
            // even once the pill no longer fits.
            color: clipItem.previewUsesProxy ? Theme.clipProxyBand : Theme.scrimColor
            z: 1

            // Just the name. The effect stack used to be listed on a second line here, but it
            // now lives on the clip's adjustment lane, which shows it in the row above — two
            // copies of the same list, one of them detached from the thing you edit.
            //
            // A VFR "!" leads the name so it survives any zoom; the pills follow it. The name only
            // gives up width when everything doesn't fit, so a long name elides and pills stay whole.
            // VEDIT: the FX strip (see fxBand) — upper half, edge to edge, list at the right.
            Rectangle {
                visible: clipItem.fxShown || clipItem.speedShown
                x: 0
                y: 0
                width: parent.width
                // VEDIT (조박사님): a speed change takes the whole header, its "4×" as large as the name.
                height: clipItem.speedShown ? parent.height : Math.round(parent.height / 2)
                readonly property color base: clipItem.speedShown ? "#d62f2f" : Theme.clipEffect
                color: fxMouse.containsMouse ? Qt.lighter(base, 1.2) : base
                Text {
                    id: fxListText
                    anchors.right: parent.right
                    anchors.rightMargin: clipItem.edgeMargin + 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width / 2)
                    horizontalAlignment: Text.AlignRight
                    text: clipItem.speedText
                          + (clipItem.speedShown && clipItem.fxShown ? "   " : "")
                          + (clipItem.fxShown ? "FX  " + clipItem.effectsLabelText : "")
                    color: Theme.onMedia
                    font.pixelSize: clipItem.speedShown ? Theme.fontSizeTiny
                                                        : Theme.fontSizeTiny - 1.5 // VEDIT (조박사님): a little smaller
                    font.family: Theme.fontFamily
                    font.weight: Font.DemiBold
                    elide: Text.ElideLeft
                }
            }

            Row {
                id: clipNameRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 4

                readonly property real pillsWidth:
                    (proxyBadge.visible ? proxyBadge.width + spacing : 0)
                    + (editFriendlyBadge.visible ? editFriendlyBadge.width + spacing : 0)

                VfrWarning {
                    id: vfrBadge
                    anchors.verticalCenter: parent.verticalCenter
                    visible: clipItem.mediaVariableFrameRate
                    size: Math.min(Theme.iconSizeSm, clipItem.headerBandHeight - 2)
                }

                Text {
                    width: Math.max(0, Math.min(implicitWidth,
                                                parent.width - clipNameRow.pillsWidth
                                                - (vfrBadge.visible ? vfrBadge.width + parent.spacing : 0)))
                    anchors.verticalCenter: parent.verticalCenter
                    text: clipItem.clipData.name
                    color: Theme.onMedia
                    font.pixelSize: Theme.fontSizeTiny
                    font.family: Theme.fontFamily
                    elide: Text.ElideRight
                }

                MediaPill {
                    id: proxyBadge
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Proxy")
                    tooltip: qsTr("Previewing from a low-resolution proxy. Export uses the original.")
                    // Dropped on clips too narrow to still show some of their name beside it.
                    visible: clipItem.previewUsesProxy && parent.width > width * 2.5
                    height: Math.min(implicitHeight, clipItem.headerBandHeight - 2)
                    fontSize: Theme.fontSizeTiny
                }

                MediaPill {
                    id: editFriendlyBadge
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Edit-friendly")
                    tooltip: qsTr("Converted to a constant frame rate for smooth editing")
                    accent: Theme.clipEditFriendly
                    foreground: Theme.clipEditFriendlyForeground
                    visible: clipItem.mediaEditFriendly
                             && parent.width > width * 2.5 + (proxyBadge.visible ? proxyBadge.width : 0)
                    height: Math.min(implicitHeight, clipItem.headerBandHeight - 2)
                    fontSize: Theme.fontSizeTiny
                }
            }
        }

        // Adjustments get a single centred line rather than the scrim band above: a nested lane
        // is a ~20px strip, and a band plus two text rows has nowhere to be legible.
        Item {
            visible: clipItem.clipData.kind === "adjustment"
            anchors.fill: parent
            z: 1

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                text: clipItem.adjustmentLabelText
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }
        }

        Column {
            visible: clipItem.trackType === "text"
                     || clipItem.trackType === "subtitle"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // Clamped so the label width cannot
            // go negative on very narrow clips.
            anchors.leftMargin: Math.min(Theme.spacingLg, parent.width / 4)
            anchors.rightMargin: Math.min(Theme.spacingLg, parent.width / 4)
            spacing: 1

            Text {
                width: parent.width
                text: clipItem.trackType === "subtitle"
                      ? (clipItem.clipData.name
                         || qsTr("Subtitles"))
                      : (clipItem.clipData.textContent
                         || clipItem.clipData.name)
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }

        }

        // Waveform: only the slice of the clip that is on screen gets a Canvas, at 1:1 px,
        // so a multi-hour clip neither allocates a hundreds-of-thousands-px buffer nor
        // stretches a fixed peak list across it.
        Item {
            id: waveformHost
            visible: clipItem.trackType === "audio"
                     || (clipItem.trackType === "video"
                         && clipItem.showWaveform)
                     || clipItem.showWaveformBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: clipItem.showWaveformBar ? parent.height - clipItem.waveformBarHeight
                                                        : clipItem.headerBandHeight
            anchors.bottom: parent.bottom
            clip: true

            Rectangle {
                visible: clipItem.showWaveformBar
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.35)
            }

            // Bumped when an off-thread decode lands, to re-run the peaks and lane-count
            // bindings. Lives on the host because the labels below need it too.
            property int decodeRevision: 0

            // Channels the decoder found, 0 until the first block lands — so a multi-channel
            // clip draws the merged lane for one frame and then splits.
            // VEDIT: what the source actually carries — "mono", "stereo", "left" / "right" (a
            // stereo file with one silent side), "multi", or "" before the first block decodes.
            readonly property string channelLayout: {
                void decodeRevision
                if (!clipItem.wfPath)
                    return ""
                return EditorState.waveformChannelLayout(clipItem.wfPath, clipItem.wfStream)
            }
            // Stereo with one silent side draws just the side that has sound, in one lane.
            // VEDIT (조박사님, as Vegas shows it): a stereo file with one silent side is drawn as two
            // lanes too — the side with sound, and an empty lane for the silent side — so it is
            // plain which speaker it plays on. (It used to fill one full-height lane, which read as
            // mono.)
            readonly property bool oneSided: channelLayout === "left" || channelLayout === "right"
            readonly property int soloChannel: -1

            readonly property int sourceChannels: {
                void decodeRevision
                if (!clipItem.clipData.path)
                    return 0
                // Stereo always splits into L/R (VEDIT); other layouts keep the per-track
                // opt-in for every channel (5.1, 7.1...).
                if (channelLayout === "stereo" || waveformHost.oneSided)
                    return 2
                if (!clipItem.showChannelWaveforms || clipItem.showWaveformBar)
                    return 0
                return EditorState.waveformChannelCount(
                    clipItem.clipData.path, clipItem.clipData.audioStreamIndex || 0)
            }

            // Below this a lane is a grey smear rather than a waveform, so fall back to the
            // single merged envelope instead of drawing something unreadable.
            readonly property real minLaneHeight: 8

            readonly property int laneCount: {
                if (sourceChannels < 2)
                    return 1
                return (height / sourceChannels) >= minLaneHeight ? sourceChannels : 1
            }
            readonly property real laneHeight: height / Math.max(1, laneCount)

            Connections {
                target: EditorState
                function onWaveformRangeReady(path) {
                    if (path === clipItem.wfPath)
                        waveformHost.decodeRevision++
                }
            }

            Canvas {
                id: waveformCanvas
                // VEDIT: a sound clip's waveform shrinks with its event level, as in Vegas — each
                // lane about its own centre line (drawn smaller, not squeezed toward the middle).
                readonly property real gainScale: clipItem.trackType === "audio" ? clipItem.liveLevel : 1.0
                onGainScaleChanged: requestPaint()

                // Visible slice of the clip, snapped to a 256px grid so scrolling only
                // re-queries peaks every step instead of every frame.
                readonly property real windowStep: 256
                readonly property real visibleLeft: {
                    const left = panel.timelineViewX - clipItem.x - windowStep
                    return Math.max(0, Math.floor(left / windowStep) * windowStep)
                }
                readonly property real visibleRight: {
                    if (panel.timelineViewW <= 0)
                        return waveformHost.width
                    const right = panel.timelineViewX - clipItem.x + panel.timelineViewW + windowStep
                    return Math.min(waveformHost.width,
                                    Math.ceil(right / windowStep) * windowStep)
                }
                // Source seconds per px of clip body, so the strip maps to the trimmed
                // window rather than the whole file.
                readonly property real srcPerPx: {
                    const span = clipItem.wfOut - clipItem.wfIn
                    return waveformHost.width > 0 && span > 0 ? span / waveformHost.width : 0
                }

                x: visibleLeft
                width: Math.max(1, Math.min(4096, Math.floor(visibleRight - visibleLeft)))
                height: waveformHost.height

                property var peaks: {
                    void waveformHost.decodeRevision
                    if (!clipItem.wfPath || srcPerPx <= 0)
                        return []
                    // The per-channel query covers this window already; asking for the merged
                    // envelope as well would double the work for something nothing draws.
                    if (waveformHost.laneCount > 1 || waveformHost.soloChannel >= 0)
                        return []
                    return EditorState.waveformPeaksRange(
                        clipItem.wfPath,
                        clipItem.wfIn + x * srcPerPx,
                        width * srcPerPx,
                        Math.ceil(width),
                        clipItem.wfStream)
                }

                // { channels, buckets, names, peaks } with peaks channel-major and flat.
                property var channelData: {
                    void waveformHost.decodeRevision
                    if (!clipItem.wfPath || srcPerPx <= 0
                            || (waveformHost.laneCount <= 1 && waveformHost.soloChannel < 0))
                        return null
                    return EditorState.waveformChannelPeaksRange(
                        clipItem.wfPath,
                        clipItem.wfIn + x * srcPerPx,
                        width * srcPerPx,
                        Math.ceil(width),
                        clipItem.wfStream)
                }

                onPeaksChanged: requestPaint()
                onChannelDataChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                // One lane per source channel. Drawn as a single filled path per lane rather
                // than a rect per column: 8 lanes over a viewport-wide clip is tens of
                // thousands of fillRect calls, which visibly hitches on repaint.
                function paintLanes(ctx, data) {
                    const channels = data.channels
                    const buckets = data.buckets
                    const values = data.peaks
                    const w = Math.max(1, Math.floor(width))
                    const laneH = height / channels

                    ctx.fillStyle = Theme.waveformColor
                    for (var c = 0; c < channels; c++) {
                        const base = c * buckets
                        const mid = c * laneH + laneH / 2
                        const half = (laneH / 2) * 0.85
                        ctx.beginPath()
                        // Top edge left to right, then the mirrored bottom edge back, so the
                        // lane closes into one shape.
                        for (var x = 0; x < w; x++) {
                            var i0 = base + Math.floor(x * buckets / w)
                            var i1 = base + Math.floor((x + 1) * buckets / w)
                            if (i1 <= i0)
                                i1 = Math.min(base + buckets, i0 + 1)
                            var peak = 0
                            for (var i = i0; i < i1; i++) {
                                if (values[i] > peak)
                                    peak = values[i]
                            }
                            const amp = Math.max(0.5, peak * half * waveformCanvas.gainScale)
                            if (x === 0)
                                ctx.moveTo(x, mid - amp)
                            else
                                ctx.lineTo(x, mid - amp)
                            ctx.lineTo(x + 1, mid - amp)
                        }
                        for (var xb = w - 1; xb >= 0; xb--) {
                            var j0 = base + Math.floor(xb * buckets / w)
                            var j1 = base + Math.floor((xb + 1) * buckets / w)
                            if (j1 <= j0)
                                j1 = Math.min(base + buckets, j0 + 1)
                            var peakB = 0
                            for (var j = j0; j < j1; j++) {
                                if (values[j] > peakB)
                                    peakB = values[j]
                            }
                            const ampB = Math.max(0.5, peakB * half * waveformCanvas.gainScale)
                            ctx.lineTo(xb + 1, mid + ampB)
                            ctx.lineTo(xb, mid + ampB)
                        }
                        ctx.closePath()
                        ctx.fill()
                    }

                    // Without a divider eight lanes read as one grey block. Inner edges only.
                    ctx.fillStyle = Theme.panelBorder
                    ctx.globalAlpha = 0.5
                    for (var d = 1; d < channels; d++)
                        ctx.fillRect(0, Math.round(d * laneH), w, 1)
                    ctx.globalAlpha = 1.0
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);

                    if (waveformHost.laneCount > 1 && channelData
                            && channelData.channels > 1 && channelData.buckets > 0) {
                        // VEDIT: draw the sides as they will play (L on both, R on both, mixed)
                        const mode = clipItem.effectiveChannelMode
                        if (mode > 0 && channelData.channels === 2) {
                            const b = channelData.buckets
                            const src = channelData.peaks
                            if (mode === 3) {
                                // VEDIT (조박사님): L/R mixed is mono — one lane, full height
                                const mono = new Array(b)
                                for (let k = 0; k < b; ++k)
                                    mono[k] = (src[k] + src[b + k]) / 2
                                paintLanes(ctx, { channels: 1, buckets: b, peaks: mono });
                                return;
                            }
                            const out = new Array(2 * b)
                            for (let k = 0; k < b; ++k) {
                                if (mode === 4) { // VEDIT: swapped
                                    out[k] = src[b + k]
                                    out[b + k] = src[k]
                                    continue
                                }
                                const v1 = mode === 1 ? src[k] : src[b + k]
                                out[k] = v1
                                out[b + k] = v1
                            }
                            paintLanes(ctx, { channels: 2, buckets: b, peaks: out });
                            return;
                        }
                        paintLanes(ctx, channelData);
                        return;
                    }
                    // VEDIT: one-sided stereo — draw only the channel with sound, full height.
                    if (waveformHost.soloChannel >= 0 && channelData && channelData.buckets > 0
                            && channelData.channels > waveformHost.soloChannel) {
                        const b = channelData.buckets;
                        const start = waveformHost.soloChannel * b;
                        paintLanes(ctx, { channels: 1, buckets: b,
                                          peaks: channelData.peaks.slice(start, start + b) });
                        return;
                    }

                    if (!peaks || peaks.length === 0)
                        return;
                    ctx.fillStyle = Theme.waveformColor;
                    var mid = height / 2;
                    var w = Math.max(1, Math.floor(width));
                    var n = peaks.length;
                    for (var x = 0; x < w; x++) {
                        var i0 = Math.floor(x * n / w);
                        var i1 = Math.floor((x + 1) * n / w);
                        if (i1 <= i0)
                            i1 = Math.min(n, i0 + 1);
                        var peak = 0;
                        for (var i = i0; i < i1; i++) {
                            if (peaks[i] > peak)
                                peak = peaks[i];
                        }
                        var amp = peak * mid * 0.9 * gainScale;
                        if (amp > 0.5)
                            ctx.fillRect(x, mid - amp, 1, amp * 2);
                    }
                }
            }

            // Channel labels. Pinned to the host, not drawn into the canvas: the canvas x
            // tracks the viewport, so anything painted in its coordinates slides while you
            // scroll. Hidden when a lane is too short for the glyph to fit.
            Repeater {
                // VEDIT: a one-sided stereo lane is labelled too, so "only L" is visible.
                model: clipItem.effectiveChannelMode === 3 ? 0 // VEDIT: mono, no L / R
                       : waveformHost.laneCount > 1 ? waveformHost.laneCount
                                                    : (waveformHost.soloChannel >= 0 ? 1 : 0)

                Text {
                    readonly property var names: waveformCanvas.channelData
                                                 ? waveformCanvas.channelData.names : null
                    x: clipItem.trackType === "audio" ? waveformHost.width - width - 4 : 3 // VEDIT: clear of the name
                    y: index * waveformHost.laneHeight
                       + (waveformHost.laneHeight - height) / 2
                    visible: waveformHost.laneHeight >= 14
                    // VEDIT: plain L / R for stereo instead of FL / FR.
                    text: waveformHost.soloChannel >= 0 ? (waveformHost.soloChannel === 0 ? "L" : "R")
                          : (waveformHost.channelLayout === "stereo" || waveformHost.oneSided) ? (index === 0 ? "L" : "R")
                          : (names && names[index]) ? names[index] : (index + 1)
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }
        }
    }

    // VEDIT (조박사님, as in Vegas): a sound clip's name on top of its waveform, top-left.
    Rectangle {
        id: audioNameTag
        visible: clipItem.trackType === "audio" && clipItem.width > 40 && (clipItem.clipData.name || "").length > 0
        z: 3
        x: Math.min(clipItem.fadeHandleSize + 4, 16)
        y: 2
        width: Math.min(audioNameText.implicitWidth + 8, Math.max(0, clipItem.width - x - 6))
        height: audioNameText.implicitHeight + 2
        radius: 2
        color: Qt.rgba(0, 0, 0, 0.45)
        clip: true
        Text {
            id: audioNameText
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 8
            text: clipItem.clipData.name || ""
            elide: Text.ElideRight
            color: Theme.onMedia
            font.pixelSize: Theme.fontSizeTiny
            font.family: Theme.fontFamily
        }
    }

    // VEDIT (조박사님, Vegas's event level): the line along the clip's top. Drag it down: a sound
    // clip gets quieter (its waveform shrinks, the dB shows); a picture, text or shape gets more
    // transparent ("Opacity -10%") while the clip itself on the timeline stays as it is.
    readonly property bool levelIsSound: clipItem.trackType === "audio"
    // VEDIT: stereo sides — the clip's own choice, else its track's
    readonly property int effectiveChannelMode: (clipItem.clipData.channelMode || 0) > 0
                                                ? clipItem.clipData.channelMode
                                                : ((panel.tracks && panel.tracks[clipItem.trackIndex]
                                                    && panel.tracks[clipItem.trackIndex].channelMode) || 0)
    onEffectiveChannelModeChanged: if (typeof waveformCanvas !== "undefined") waveformCanvas.requestPaint()
    property real dragLevel: -1
    readonly property real liveLevel: dragLevel >= 0 ? dragLevel
                                                     : (clipItem.clipData.level !== undefined ? clipItem.clipData.level : 1.0)
    // VEDIT: the waveform is redrawn at every step of the drag, not only when it ends
    onLiveLevelChanged: if (typeof waveformCanvas !== "undefined" && waveformCanvas) waveformCanvas.requestPaint()
    // Line position (0 = top) ↔ level: sound on a squared scale (half way ≈ -12 dB), pictures linear.
    function levelFromPos(p) {
        p = Math.max(0, Math.min(1, p))
        return levelIsSound ? (1 - p) * (1 - p) : 1 - p
    }
    function posFromLevel(l) {
        l = Math.max(0, Math.min(1, l))
        return levelIsSound ? 1 - Math.sqrt(l) : 1 - l
    }
    function levelLabel(l) {
        if (levelIsSound) {
            if (l <= 0.0001)
                return "-∞ dB"
            const db = 20 * Math.log(l) / Math.LN10
            return (db > -0.05 ? "0.0" : db.toFixed(1)) + " dB"
        }
        return qsTr("Opacity %1%").arg(l >= 0.9995 ? "0" : "-" + Math.round((1 - l) * 100))
    }
    readonly property bool levelSupported: clipItem.trackType !== "subtitle" && clipItem.clipData.kind !== "adjustment"
    readonly property real levelLineY: posFromLevel(liveLevel) * (clipItem.height - 4) + 1

    Rectangle {
        id: levelLine
        visible: clipItem.levelSupported && (clipItem.liveLevel < 0.999 || levelMouse.containsMouse || levelMouse.pressed)
        z: 25
        x: 0
        width: clipItem.width
        y: clipItem.levelLineY
        height: levelMouse.containsMouse || levelMouse.pressed ? 2 : 1
        color: Qt.rgba(1, 1, 1, levelMouse.containsMouse || levelMouse.pressed ? 0.95 : 0.6)
    }
    Rectangle {
        visible: clipItem.levelSupported && (levelMouse.containsMouse || levelMouse.pressed)
        z: 26
        // VEDIT (조박사님): beside the pointer, not under it — to its right, or to its left near
        // the clip's right end.
        readonly property real pointerX: levelMouse.mouseX + levelMouse.x
        x: pointerX + 18 + width <= clipItem.width - 2 ? pointerX + 18
                                                       : Math.max(2, pointerX - 18 - width)
        y: Math.max(0, Math.min(clipItem.height - height - 1, clipItem.levelLineY - height / 2))
        width: levelText.implicitWidth + 8
        height: levelText.implicitHeight + 2
        radius: 3
        color: Qt.rgba(0.1, 0.1, 0.1, 0.9)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.7)
        Text {
            id: levelText
            anchors.centerIn: parent
            text: clipItem.levelLabel(clipItem.liveLevel)
            color: "#ffffff"
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
    }
    // VEDIT (조박사님): a handle in the middle of the level line, as in Vegas — shown while the
    // pointer is on the clip, or the clip is selected, or the level is down.
    Rectangle {
        id: levelHandle
        visible: clipItem.levelSupported && !clipItem.touchMode
                 && (clipMouse.containsMouse || levelMouse.containsMouse || levelMouse.pressed
                     || clipItem.selected || clipItem.liveLevel < 0.999)
                 && clipItem.width > 2 * (clipItem.fadeHandleSize + 12) + 24
        z: 26
        width: 24
        height: 6
        radius: 0 // 조박사님: square corners
        x: (clipItem.width - width) / 2
        y: Math.max(0, Math.min(clipItem.height - height, clipItem.levelLineY - height / 2 + 0.5))
        color: levelMouse.containsMouse || levelMouse.pressed ? "#3a3a3a" : "#000000" // 조박사님: black
        border.width: 1
        border.color: "#8a8a8a" // VEDIT (조박사님): grey, not white
    }
    MouseArea {
        id: levelMouse
        enabled: clipItem.levelSupported && !clipItem.touchMode
        z: 24
        // Between the fade dots, a strip around the line.
        x: clipItem.fadeHandleSize + 12
        width: Math.max(0, clipItem.width - 2 * (clipItem.fadeHandleSize + 12))
        y: Math.max(0, clipItem.levelLineY - 4)
        height: 9
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        preventStealing: true
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const yy = mapToItem(clipItem, mouse.x, mouse.y).y
            clipItem.dragLevel = clipItem.levelFromPos((yy - 1) / Math.max(1, clipItem.height - 4))
            // VEDIT (조박사님): the preview and the sound follow while the line moves
            EditorState.previewSetClipLevel(clipItem.trackIndex, clipItem.clipIndex, clipItem.dragLevel)
        }
        onReleased: {
            if (clipItem.dragLevel >= 0) {
                EditorState.commitPreviewDrag() // the whole drag is one undo step
                clipItem.dragLevel = -1
            }
        }
        onCanceled: {
            if (clipItem.dragLevel >= 0)
                EditorState.cancelPreviewDrag()
            clipItem.dragLevel = -1
        }
        onDoubleClicked: EditorState.setClipLevel(clipItem.trackIndex, clipItem.clipIndex, 1.0) // back to full
    }

    MouseArea {
        id: clipMouse
        z: 2
        anchors.fill: parent
        hoverEnabled: true
        // Always leave edge strips for CapCut-style trim cursor on approach.
        anchors.leftMargin: clipItem.edgeMargin
        anchors.rightMargin: clipItem.edgeMargin
        enabled: !leftTrimMouse.pressed && !rightTrimMouse.pressed
                     && !fadeInMouse.pressed && !fadeOutMouse.pressed
        // CapCut: open hand to move. Trim edges set SizeHorCursor via their own
        // HoverHandlers (subtitle-grip pattern); do not claim a cursor here while
        // those zones are hovered or Qt keeps the open-hand shape.
        cursorShape: {
            if (copyDrag)
                return Qt.DragCopyCursor // VEDIT
            if (drag.active)
                return Qt.ClosedHandCursor
            if (leftTrimMouse.containsMouse || rightTrimMouse.containsMouse
                    || fadeInMouse.containsMouse || fadeOutMouse.containsMouse)
                return Qt.BlankCursor
            return Qt.OpenHandCursor
        }
        // Right-click opens the clip menu; the app
        // previously had no context menus at all.
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // On touch a clip only becomes draggable after a deliberate press-and-hold.
        // Holding the grab from the first touch (preventStealing over a surface that is
        // mostly clips) meant the Flickable could never pan, so a timeline with any
        // content in it could not be scrolled at all.
        //
        // Arming goes through drag.target, not preventStealing: MouseArea copies
        // preventStealing into its private stealMouse flag once, in mousePressEvent, and
        // drag activation needs keepMouseGrab && stealMouse. Raising preventStealing
        // after the press sets only keepMouseGrab, which then blocks the branch that
        // would have set stealMouse — leaving the clip grabbed and permanently
        // undraggable. A null drag.target skips the drag block entirely instead, so the
        // Flickable steals at its own threshold and panning works.
        // VEDIT: a locked track's clips stay put.
        drag.target: clipItem.trackLocked ? null
                     : (clipItem.touchMode ? (moveArmed ? clipItem : null) : clipItem)
        drag.axis: Drag.XAndYAxis
        // Once armed the finger is already down and still, so any motion is the move.
        // On desktop, 2px threshold eliminates the deadzone while preventing accidental micro-drags.
        drag.threshold: clipItem.touchMode ? 0 : 2
        drag.minimumX: {
            if (clipItem.selected && EditorState.selectionCount > 1) {
                const earliest = EditorState.selectionEarliestStartSeconds()
                const minStart = Math.max(0, (clipItem.clipData.start || 0) - earliest)
                return minStart * panel.pxPerSecond + clipItem.xInset
            }
            return clipItem.xInset
        }
        // Allow dropping onto any track, not just the immediate neighbours.
        drag.minimumY: -Math.max(clipItem.trackRow.height, panel.totalTracksHeightCached)
        drag.maximumY: Math.max(clipItem.trackRow.height * 2, panel.totalTracksHeightCached)
        preventStealing: !clipItem.touchMode
        pressAndHoldInterval: clipItem.touchMode ? 450 : 800
        property int originTrack: clipItem.trackIndex
        property int originClip: clipItem.clipIndex
        // Model-space X at press — follow delta is measured from this so partners
        // stay locked to the leader even after the drag threshold has been crossed.
        property real moveOriginX: 0
        // drag.active is cleared after onReleased in some paths; remember we dragged.
        property bool didDrag: false
        // True only for the span we treat as a move, so the landing preview stops
        // updating the moment the clip is dropped.
        property bool moving: false
        // Touch only: the hold has been recognised and the clip is now draggable.
        // Releasing without moving opens the context menu instead.
        property bool moveArmed: false
        // VEDIT: Ctrl is held while dragging — the drop will copy.
        property bool copyDrag: false

        // Where inside the clip the finger landed, so the autoscroll driver below can
        // recompute the pointer's viewport position each tick instead of guessing at
        // the clip's centre (which for a clip wider than the viewport is never in it).
        property real grabX: 0
        property real grabY: 0

        onPressed: (mouse) => {
            Qt.callLater(function() { clipItem.forceActiveFocus() })
            // Read before anything selects: `selected` comes back through the model, so testing it
            // after the call would report the state this press is about to create.
            const wasSelected = clipItem.selected
            // A previous gesture that ended while snapped would otherwise leave the latch engaged
            // and swallow this one's first tick.
            Haptics.reset()
            originTrack = clipItem.trackIndex
            originClip = clipItem.clipIndex
            moveOriginX = clipItem.clipData.start * panel.pxPerSecond
                          + clipItem.xInset
            grabX = mouse.x + clipItem.edgeMargin
            grabY = mouse.y
            didDrag = false
            moving = false
            moveArmed = false
            if (mouse.button === Qt.RightButton) {
                // Right-click selects, then opens the menu.
                if (!clipItem.selected)
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                clipItem.openContextMenu()
                return
            }
            // Touch multi-select mode (see the panel's multiSelectActive): the press
            // does nothing so a pan still reaches the Flickable, and the tap toggles
            // on release. Undefined on desktop, which has shift-click and the marquee.
            if (panel.multiSelectActive === true)
                return
            if ((mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                EditorState.addToSelection(clipItem.trackIndex, clipItem.clipIndex)
            else if (!wasSelected)
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
            // Only when the press changed the selection. Tapping a clip that is already selected
            // does nothing, and onClicked is about to run this same branch again — ticking on both
            // would double up on any tap slow enough to outlast the rate limiter.
            if (!wasSelected)
                Haptics.select()
        }
        // Hold, then move to drag; hold and let go to get the menu. One gesture serves
        // both, which is what leaves the plain drag free for the Flickable to pan with.
        onPressAndHold: (mouse) => {
            if (!clipItem.touchMode || mouse.button === Qt.RightButton)
                return
            if (didDrag || drag.active || panel.multiSelectActive === true)
                return
            moveArmed = true
            // The moment the clip becomes draggable, and the only one in the gesture that nothing
            // on screen can announce ahead of the user's own movement: the lift animation plays
            // because of this, not before it.
            Haptics.pickUp()
            if (!clipItem.selected)
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton && clipItem.clipData.kind === "composite")
                EditorState.openCompositeClip(clipItem.trackIndex, clipItem.clipIndex)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton || didDrag)
                return
            if (panel.multiSelectActive === true) {
                // The press deliberately does nothing in this mode so a pan can still reach the
                // Flickable, which leaves the tick to the tap that lands here.
                panel.toggleInSelection(clipItem.trackIndex, clipItem.clipIndex)
                Haptics.select()
                return
            }
            if ((mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                EditorState.addToSelection(clipItem.trackIndex, clipItem.clipIndex)
            else
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
        }

        // Surfaces actions that were previously
        // reachable only by unlabelled shortcut,
        // plus cutSelection which had no UI at all.
        // Every clip in the project used to build this menu at load: eighteen entries, each an
        // icon, a label and a background, for a menu that only ever opens on a right-click.
        // Loaded on demand instead. Synchronous, and popped from onLoaded, so the very first
        // right-click still opens it rather than being spent on the load.
        Loader {
            id: clipContextMenuLoader
            // Stands in for the rect the Menu used to be parented to, so it resolves the same
            // parent item and clamps against the same bounds.
            anchors.fill: parent
            active: false
            asynchronous: false
            sourceComponent: clipContextMenuComponent
            onLoaded: item.popup()
        }

        FadeCurvePopup { // VEDIT
            id: fadeTypePopup
            mode: "clip"
            x: Math.min(Math.max(0, clipItem.width / 2 - width / 2), clipItem.width)
            y: -height - 6
        }

        Component {
            id: clipContextMenuComponent

            ThemedContextMenu {
                id: clipContextMenu

                property bool canPasteEffects: false
                property bool canPasteAttributes: false
                property bool canMergeTrackSubtitles: false
                onAboutToShow: {
                    canPasteAttributes = EditorState.canPasteAttributes()
                    canMergeTrackSubtitles = clipItem.trackType === "subtitle"
                            && EditorState.canMergeAllSubtitlesOnTrack(clipItem.trackIndex)
                }
                // Deferred: onClosed runs inside the Popup's own close path, and dropping the
                // Loader's item there would destroy an object still unwinding.
                onClosed: Qt.callLater(function() { clipContextMenuLoader.active = false })

                // VEDIT (조박사님): grouped by kind — attributes, sound, editing, split, move,
                // composite, the rest, then Delete.
                ThemedMenuItem {
                    text: qsTr("Properties")
                    icon.name: Theme.icons.sliders
                    onTriggered: {
                        if (!clipItem.selected)
                            EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                        if (typeof panel.openClipProperties === "function")
                            panel.openClipProperties()
                    }
                }
                ThemedMenuItem { // VEDIT (조박사님): the keyframe lane above the tracks, on request only
                    text: panel.keyframeLaneShown ? qsTr("Hide keyframe track") : qsTr("Show keyframe track")
                    icon.name: Theme.icons.diamondPlus
                    visible: clipItem.keyCapable
                    height: visible ? implicitHeight : 0
                    onTriggered: {
                        if (!clipItem.selected)
                            EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                        panel.keyframeLaneShown = !panel.keyframeLaneShown
                    }
                }
                ThemedMenuItem { // VEDIT (조박사님)
                    text: qsTr("Clear all attributes")
                    icon.name: Theme.icons.undo
                    onTriggered: EditorState.clearClipAttributes(clipItem.trackIndex, clipItem.clipIndex)
                }
                // VEDIT: CapCut's 특성 복사 / 특성 붙여넣기 — every attribute of this clip, pasted
                // onto others through a checklist.
                ThemedMenuItem {
                    text: qsTr("Copy attributes")
                    icon.name: Theme.icons.wand
                    onTriggered: EditorState.copyAttributes(clipItem.trackIndex, clipItem.clipIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Paste attributes…")
                    icon.name: Theme.icons.clipboardPaste
                    enabled: clipContextMenu.canPasteAttributes
                    onTriggered: EditorState.requestPasteAttributes()
                }
                // VEDIT: 효과 복사 / 효과 붙여넣기 removed — 특성 복사 / 특성 붙여넣기 covers them.
                ThemedMenuItem {
                    text: qsTr("Save effects as preset…")
                    icon.name: Theme.icons.save
                    visible: clipItem.hasAnyEffects
                    onTriggered: clipItem.panel.requestSaveEffectPreset(clipItem.trackIndex,
                                                                         clipItem.clipIndex)
                }
                ThemedMenuSeparator { }
                // VEDIT (조박사님): stereo sides for this sound clip
                ThemedContextMenu {
                    title: qsTr("Sound channels")
                    enabled: clipItem.trackType === "audio"
                    ThemedMenuItem {
                        text: ((clipItem.clipData.channelMode || 0) === 0 ? "✓  " : "     ") + qsTr("As the track")
                        onTriggered: EditorState.setClipChannelMode(clipItem.trackIndex, clipItem.clipIndex, 0)
                    }
                    ThemedMenuItem {
                        text: ((clipItem.clipData.channelMode || 0) === 1 ? "✓  " : "     ") + qsTr("Left on both sides (L → R)")
                        onTriggered: EditorState.setClipChannelMode(clipItem.trackIndex, clipItem.clipIndex, 1)
                    }
                    ThemedMenuItem {
                        text: ((clipItem.clipData.channelMode || 0) === 2 ? "✓  " : "     ") + qsTr("Right on both sides (R → L)")
                        onTriggered: EditorState.setClipChannelMode(clipItem.trackIndex, clipItem.clipIndex, 2)
                    }
                    ThemedMenuItem {
                        text: ((clipItem.clipData.channelMode || 0) === 3 ? "✓  " : "     ") + qsTr("Mix left and right (L/R mix)")
                        onTriggered: EditorState.setClipChannelMode(clipItem.trackIndex, clipItem.clipIndex, 3)
                    }
                    ThemedMenuItem {
                        text: ((clipItem.clipData.channelMode || 0) === 4 ? "✓  " : "     ") + qsTr("Swap left and right (L ↔ R)")
                        onTriggered: EditorState.setClipChannelMode(clipItem.trackIndex, clipItem.clipIndex, 4)
                    }
                }
                // VEDIT: Vegas's Fade Type menu for this clip's fades
                ThemedMenuItem {
                    text: qsTr("Fade type…")
                    icon.name: Theme.icons.sliders
                    visible: (clipItem.clipData.fadeIn || 0) > 0 || (clipItem.clipData.fadeOut || 0) > 0
                    height: visible ? implicitHeight : 0
                    onTriggered: {
                        EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                        fadeTypePopup.clipMap = EditorState.selectedClipData
                        fadeTypePopup.trackIndex = clipItem.trackIndex
                        fadeTypePopup.clipIndex = clipItem.clipIndex
                        fadeTypePopup.open()
                    }
                }
                ThemedMenuItem {
                    text: qsTr("Separate audio")
                    icon.name: Theme.icons.audioLines
                    // CapCut: only offer extract when the clip still has embedded audio.
                    visible: clipItem.trackType === "video" && EditorState.separateAudioAvailable
                    onTriggered: EditorState.separateAudioFromSelection()
                }
                ThemedMenuItem {
                    // VEDIT: CapCut's 오디오 복구 — offered on the silenced video or its split-off audio.
                    text: qsTr("Restore audio")
                    icon.name: Theme.icons.audioLines
                    visible: EditorState.restoreAudioAvailable
                    onTriggered: EditorState.restoreAudioFromSelection()
                }
                ThemedMenuItem {
                    text: qsTr("Separate all audio tracks")
                    icon.name: Theme.icons.audioLines
                    visible: clipItem.trackType === "video" && EditorState.separateAudioAvailable
                             && EditorState.clipAudioStreamCount(clipItem.trackIndex, clipItem.clipIndex) > 1
                    onTriggered: EditorState.separateAllAudioTracks(clipItem.trackIndex, clipItem.clipIndex)
                }
                // VEDIT: 소리로 맞추기 — the selected clips lined up by their sound (CapCut audio sync).
                ThemedMenuItem {
                    text: qsTr("Line up by sound")
                    icon.name: Theme.icons.layers
                    visible: EditorState.syncByAudioAvailable
                    onTriggered: EditorState.syncSelectionByAudio()
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Cut")
                    icon.name: Theme.icons.scissors
                    onTriggered: EditorState.cutSelection()
                }
                ThemedMenuItem {
                    text: qsTr("Copy")
                    icon.name: Theme.icons.copy
                    onTriggered: EditorState.copySelection()
                }
                ThemedMenuItem {
                    text: qsTr("Duplicate")
                    icon.name: Theme.icons.copyPlus
                    onTriggered: EditorState.duplicateSelectedClip()
                }
                ThemedMenuItem {
                    text: qsTr("Rename…")
                    icon.name: Theme.icons.pencil
                    onTriggered: clipItem.panel.requestRenameClip(clipItem.trackIndex, clipItem.clipIndex)
                }
                // VEDIT: CapCut's 바꾸기 — another photo or video in this clip's place, effects kept.
                ThemedMenuItem {
                    text: qsTr("Replace with another photo or video…")
                    icon.name: Theme.icons.image
                    visible: (clipItem.clipData.kind === "video" || clipItem.clipData.kind === "image")
                             && !clipItem.clipData.linked
                    onTriggered: {
                        const url = FileDialogs.openFile(qsTr("Replace with…"), [AssetLibrary.mediaNameFilter()])
                        if (url != "")
                            EditorState.replaceClipMedia(clipItem.trackIndex, clipItem.clipIndex, url)
                    }
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Split at current time")
                    icon.name: Theme.icons.scissors
                    onTriggered: EditorState.splitAtPlayhead()
                }
                ThemedMenuItem {
                    text: qsTr("Split item at current time")
                    icon.name: Theme.icons.scissors
                    // Scoped to just this clip (and its linked partner, e.g. companion
                    // audio) — splitAtPlayhead() above cuts every clip under the playhead
                    // across every track, which is surprising when picked from one clip's menu.
                    visible: EditorState.playheadSeconds > clipItem.clipData.start
                            && EditorState.playheadSeconds < clipItem.clipData.start + clipItem.clipData.duration
                    onTriggered: EditorState.splitClipAt(clipItem.trackIndex, clipItem.clipIndex,
                                                         EditorState.playheadSeconds)
                }
                ThemedMenuSeparator { }
                // VEDIT: 드래그 없이 순서 바꾸기 — swap with the neighbour before or after.
                ThemedMenuItem {
                    text: qsTr("Move earlier")
                    icon.name: Theme.icons.chevronLeft
                    onTriggered: EditorState.moveClipInOrder(clipItem.trackIndex, clipItem.clipIndex, -1)
                }
                ThemedMenuItem {
                    text: qsTr("Move later")
                    icon.name: Theme.icons.chevronRight
                    onTriggered: EditorState.moveClipInOrder(clipItem.trackIndex, clipItem.clipIndex, 1)
                }
                // VEDIT: 드래그 없이 줄 옮기기 — picture-in-picture without dragging.
                ThemedMenuItem {
                    text: qsTr("Move to the track above")
                    icon.name: Theme.icons.chevronUp
                    onTriggered: EditorState.moveClipToNeighbourTrack(clipItem.trackIndex, clipItem.clipIndex, -1)
                }
                ThemedMenuItem {
                    text: qsTr("Move to the track below")
                    icon.name: Theme.icons.chevronDown
                    onTriggered: EditorState.moveClipToNeighbourTrack(clipItem.trackIndex, clipItem.clipIndex, 1)
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Make composite")
                    icon.name: Theme.icons.layers
                    visible: EditorState.makeCompositeAvailable
                    onTriggered: EditorState.makeCompositeFromSelection()
                }
                ThemedMenuItem {
                    text: qsTr("Open composite")
                    icon.name: Theme.icons.layers
                    visible: clipItem.clipData.kind === "composite"
                    onTriggered: EditorState.openCompositeClip(clipItem.trackIndex, clipItem.clipIndex)
                }
                // VEDIT: 합성 해제 — back to the separate clips it was made of.
                ThemedMenuItem {
                    text: qsTr("Undo composite")
                    icon.name: Theme.icons.layers
                    visible: clipItem.clipData.kind === "composite"
                    onTriggered: EditorState.ungroupComposite(clipItem.trackIndex, clipItem.clipIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Flatten composite")
                    icon.name: Theme.icons.film
                    visible: clipItem.clipData.kind === "composite"
                    enabled: !EditorState.exportInProgress
                    onTriggered: EditorState.flattenComposite(clipItem.trackIndex, clipItem.clipIndex)
                }
                ThemedMenuItem {
                    // Touch route into multi-clip selection; the panel owns the mode and
                    // the desktop panel does not declare the property at all.
                    text: qsTr("Select multiple")
                    icon.name: Theme.icons.check
                    visible: panel.multiSelectActive === false
                    onTriggered: panel.multiSelectActive = true
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Convert to edit-friendly format")
                    icon.name: Theme.icons.rabbit
                    // Converts the bin media, so every clip using it follows along.
                    visible: clipItem.mediaAssetId.length > 0 && !clipItem.mediaEditFriendly
                    onTriggered: EditorState.convertAssetsToConstantFrameRate([clipItem.mediaAssetId])
                }
                ThemedMenuItem {
                    text: qsTr("Unlink")
                    icon.name: Theme.icons.unlink
                    visible: !!clipItem.clipData.linked && EditorState.unlinkAvailable
                    onTriggered: EditorState.unlinkSelectedClips()
                }
                ThemedMenuItem {
                    text: qsTr("Merge subtitle clips")
                    icon.name: Theme.icons.linkTwo
                    visible: clipItem.trackType === "subtitle"
                    enabled: EditorState.mergeAvailable
                    onTriggered: EditorState.mergeSelectedClips()
                }
                ThemedMenuItem {
                    text: qsTr("Merge all subtitles on this track")
                    icon.name: Theme.icons.linkTwo
                    visible: clipItem.trackType === "subtitle"
                    enabled: clipContextMenu.canMergeTrackSubtitles
                    onTriggered: EditorState.mergeAllSubtitlesOnTrack(clipItem.trackIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Unlink from clip")
                    icon.name: Theme.icons.unlink
                    visible: clipItem.pinnedToClip
                    onTriggered: EditorState.unlinkAdjustment(clipItem.trackIndex, clipItem.clipIndex)
                }
                ThemedMenuItem {
                    text: qsTr("Move to its own track")
                    icon.name: Theme.icons.layers
                    // Only meaningful for a nested one: a standalone adjustment already has one.
                    visible: clipItem.clipData.kind === "adjustment"
                             && clipItem.panel.tracks[clipItem.trackIndex].isAdjustmentLane === true
                    onTriggered: EditorState.moveAdjustmentToOwnTrack(clipItem.trackIndex,
                                                                      clipItem.clipIndex)
                }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Delete")
                    icon.name: Theme.icons.trash
                    onTriggered: EditorState.deleteSelectedClip()
                }
            }
        }
        onReleased: (mouse) => {
            panel.endEdgeDrag() // VEDIT
            panel.moveInsertIndex = -1 // VEDIT: the seam line never outlives the drag
            // VEDIT: dropped with its head on a seam → 끼워 넣기.
            const insertTo = panel.insertTrack
            const insertBefore = panel.insertBeforeId
            panel.clearInsertPreview()
            const moved = drag.active || didDrag
            // VEDIT: Ctrl held at the drop copies instead of moving (VEGAS / CapCut).
            const copyDrop = (mouse.modifiers & Qt.ControlModifier) !== 0
            copyDrag = false
            // Armed but never moved: the hold was a request for the menu.
            const wantsMenu = clipItem.touchMode && moveArmed && !moved
            // NOTE: Keep didDrag true if moved so onClicked (which runs immediately after
            // onReleased in Qt Quick) knows a drag happened and does not collapse the selection.
            // didDrag is reset on the next onPressed or onCanceled.
            if (!moved)
                didDrag = false
            moving = false
            moveArmed = false
            // Clear follow before committing so partners don't keep the drag
            // offset on top of the new model start for a frame.
            panel.clearMoveFollow()
            panel.clearLandingPreview()
            lastPreviewDesired = -1
            lastPreviewTrack = -1
            if (!moved) {
                if (wantsMenu) {
                    clipItem.y = Theme.clipSelectionRingWidth
                    clipItem.openContextMenu()
                }
                return
            }
            // Snapped here, not only in the preview. The landing outline has always been drawn
            // at the snapped position while the commit below took the raw pointer position, so a
            // clip visibly locked onto an edge and then settled a few pixels off it. Mirrors
            // updateMovePreview() exactly — same clamp, same call — so the clip lands on the
            // outline it was showing. Recomputed rather than read back from panel.dropStartSeconds
            // because that is throttled to whole pixels and can be a pixel stale at release.
            const rawStart = Math.max(0, (clipItem.x - clipItem.xInset)
                                          / panel.pxPerSecond)
            const newStart = panel.snapClipStart(rawStart, clipItem.clipData.duration,
                                                 clipItem.clipData.id).start
            const pos = clipItem.mapToItem(timelineColumn, clipItem.width / 2, clipItem.height / 2)
            const target = typeof panel.dropTargetAtY === "function"
                         ? panel.dropTargetAtY(pos.y)
                         : { "track": panel.trackIndexAtY(pos.y), "lane": -1 }
            clipItem.y = Theme.clipSelectionRingWidth
            const isAdjustment = clipItem.clipData.kind === "adjustment"
            const wasInLane = panel.tracks[originTrack].isAdjustmentLane === true
            // Use indices captured on press — after the model updates, clipIndex
            // on this delegate can already refer to a different clip.
            if (isAdjustment && target.lane >= 0) {
                // Released on a lane strip: an ordinary cross-track move onto that lane. Only an
                // adjustment can land there — a media clip aimed at the strip falls through to
                // the row's own clip area rather than snapping back from a rejected move.
                if (target.lane !== originTrack)
                    EditorState.moveClipToTrack(originTrack, originClip, target.lane, newStart)
                else
                    EditorState.moveClip(originTrack, originClip, newStart)
            } else if (isAdjustment && target.track >= 0
                       && panel.tracks[target.track].type !== "adjustment") {
                // An adjustment released on a media track's body nests inside it: it stops
                // applying to everything composited below and applies only to that track.
                EditorState.moveAdjustmentToLane(originTrack, originClip, target.track, newStart)
            } else if (isAdjustment && wasInLane && target.track < 0) {
                // Dragged clear of every row: the inverse gesture, back to a track of its own
                // affecting everything below it.
                EditorState.moveAdjustmentToOwnTrack(originTrack, originClip, newStart)
            } else if (!isAdjustment && insertTo >= 0 && !copyDrop) {
                EditorState.insertClipBefore(originTrack, originClip, insertTo, insertBefore) // VEDIT
            } else if (!isAdjustment) {
                // VEDIT: dropped on the seam between two tracks (the red line) makes a new track
                // there; above or below every row makes one at that end; Ctrl copies.
                const seamRaw = typeof panel.seamInsertIndexAtY === "function" ? panel.seamInsertIndexAtY(pos.y) : -1
                const seam = seamRaw >= 0 && panel.seamAllowsType(seamRaw, panel.tracks[originTrack].type) ? seamRaw : -1
                panel.moveInsertIndex = -1
                const newAt = seam >= 0 ? seam
                            : (target.track < 0 && typeof panel.newTrackIndexForMove === "function")
                              ? panel.newTrackIndexForMove(pos.y, originTrack) : -1
                EditorState.dropClip(originTrack, originClip, seam >= 0 ? -1 : target.track, newStart, newAt, copyDrop)
            } else if (target.track >= 0 && target.track !== originTrack) {
                EditorState.moveClipToTrack(originTrack, originClip, target.track, newStart)
            } else {
                EditorState.moveClip(originTrack, originClip, newStart)
            }
            // Closes the gesture pickUp opened, and clears the snap and lane latches the drag left
            // engaged.
            Haptics.drop()
        }
        onPositionChanged: (mouse) => {
            if (pressed && drag.active) {
                panel.noteEdgeDrag(clipItem, mapToItem(panel.timelineFlick, mouse.x, 0).x) // VEDIT: edge scroll
                copyDrag = (mouse.modifiers & Qt.ControlModifier) !== 0 // VEDIT
                didDrag = true
                if (!moving) {
                    moving = true
                    panel.beginMoveFollow(originTrack, originClip)
                }
                panel.updateMoveFollow(clipItem.x - moveOriginX,
                                       clipItem.y - Theme.clipSelectionRingWidth)
                // VEDIT: light the seam the clip's middle is on (adjustments keep their own rules).
                if (clipItem.clipData.kind !== "adjustment" && typeof panel.seamInsertIndexAtY === "function") {
                    const mid = clipItem.mapToItem(timelineColumn, clipItem.width / 2, clipItem.height / 2)
                    const seamAt = panel.seamInsertIndexAtY(mid.y)
                    panel.moveInsertIndex = seamAt >= 0 && panel.seamAllowsType(seamAt, panel.tracks[originTrack].type)
                                            ? seamAt : -1
                    // VEDIT: head on a seam of a gap-filling track → open it up (끼워 넣기).
                    const target = panel.moveInsertIndex >= 0 ? -1 : panel.trackIndexAtY(mid.y)
                    const head = Math.max(0, (clipItem.x - clipItem.xInset) / panel.pxPerSecond)
                    if (target >= 0 && !copyDrag)
                        panel.updateInsertPreview(target, head, clipItem.clipData.duration, clipItem.clipData.id)
                    else
                        panel.clearInsertPreview()
                    if (panel.insertTrack >= 0)
                        panel.clearLandingPreview()
                }
            }
        }
        onCanceled: {
            panel.endEdgeDrag() // VEDIT
            panel.moveInsertIndex = -1 // VEDIT
            panel.clearInsertPreview() // VEDIT
            copyDrag = false // VEDIT
            didDrag = false
            moving = false
            moveArmed = false
            panel.clearMoveFollow()
            panel.clearLandingPreview()
            lastPreviewDesired = -1
            lastPreviewTrack = -1
            // No drop tick: the clip went back where it came from. The latches still have to go.
            Haptics.reset()
        }
    }

    // Edge autoscroll while a clip is being dragged on touch. Guarded on the panel
    // actually offering it, because this file is shared with the desktop TimelinePanel
    // (same pattern as panel.openClipProperties).
    // Armed means this clip owns the gesture. The Flickable is told to stop competing for
    // it rather than the MouseArea trying to hold the grab: raising preventStealing after
    // the press sets keepMouseGrab without stealMouse, and drag activation needs both.
    onLiftedChanged: {
        if (clipItem.touchMode && typeof panel.setScrollLocked === "function")
            panel.setScrollLocked(clipItem.lifted)
    }
    Timer {
        id: dragAutoScroll
        interval: 16
        repeat: true
        running: clipItem.touchMode && clipMouse.moving && clipMouse.drag.active
                 && typeof panel.dragEdgeScroll === "function"

        // Ramps with how far past the edge the finger is, so a nudge creeps and a
        // finger pinned to the edge travels.
        function step(depth) { return Math.min(24, 6 + depth * 0.4) }

        onTriggered: {
            const px = clipItem.x + clipMouse.grabX - panel.timelineViewX
            const py = clipItem.mapToItem(timelineColumn, 0, clipMouse.grabY).y
                       + panel.seekHeaderHeight - panel.timelineViewY
            // Bands are a fraction of the axis, capped: the timeline pane is only ~120px
            // tall at its minimum, and fixed 48px bands there would meet in the middle and
            // leave every position inside one — scrolling on any drag, in one direction,
            // forever. The seek strip is pinned to the top of the viewport, so the vertical
            // band starts below it.
            const edgeX = Math.min(48, panel.timelineViewW * 0.25)
            const usableTop = panel.seekHeaderHeight
            const usableH = Math.max(0, panel.timelineViewH - usableTop)
            const edgeY = Math.min(48, usableH * 0.25)
            var dx = 0
            var dy = 0
            if (px < edgeX)
                dx = -step(edgeX - px)
            else if (px > panel.timelineViewW - edgeX)
                dx = step(px - (panel.timelineViewW - edgeX))
            if (edgeY > 0) {
                if (py < usableTop + edgeY)
                    dy = -step(usableTop + edgeY - py)
                else if (py > panel.timelineViewH - edgeY)
                    dy = step(py - (panel.timelineViewH - edgeY))
            }
            if (dx === 0 && dy === 0)
                return

            const applied = panel.dragEdgeScroll(dx, dy)
            // Qt recomputes the drag target's position from a *scene* anchor mapped
            // through the parent's current transform, so the scroll delta is already
            // folded into the next move event — adding it here does not double-count,
            // it only keeps the clip under the finger while no move events arrive.
            clipItem.x += applied.x
            clipItem.y += applied.y
        }
    }

    // Fade dots sit above trim handles so they stay
    // grabable at the corners (zero fade). Trim the
    // edge below the dots; grab the dots to fade.
    Rectangle {
        id: fadeInHandle
        width: clipItem.fadeHandleSize
        height: clipItem.fadeHandleSize
        radius: clipItem.fadeHandleSize / 2
        y: clipItem.touchMode
           ? Math.max(0, (clipItem.height - clipItem.fadeHandleSize) / 2)
           : 2
        z: 40
        visible: clipItem.timelineFadeHandles && clipItem.selected
                 && clipItem.width > clipItem.fadeHandleMinWidth
        // VEDIT (조박사님): lights up a little under the pointer, so it reads as something to grab.
        color: (fadeInMouse.containsMouse || fadeInMouse.pressed) ? Qt.lighter(Theme.primary, 1.3) : Theme.primary
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        border.color: Theme.onMedia
        border.width: 2

        Binding {
            target: fadeInHandle
            property: "x"
            when: !fadeInMouse.pressed
            value: Math.max(clipItem.fadeHandleInset,
                            Math.min(clipItem.width - clipItem.fadeHandleInset - fadeInHandle.width,
                                     clipItem.edgeInLength * panel.pxPerSecond - fadeInHandle.width / 2))
        }

        MouseArea {
            id: fadeInMouse
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            anchors.topMargin: -6
            anchors.bottomMargin: clipItem.height < 35 ? 4 : -6
            z: 1
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                mouse.accepted = true
                EditorState.beginPreviewDrag(qsTr("Adjust fade"))
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const px = Math.max(0, Math.min(clipItem.width,
                                                mapToItem(clipItem, mouse.x, mouse.y).x))
                fadeInHandle.x = Math.max(clipItem.fadeHandleInset,
                                          Math.min(clipItem.width - clipItem.fadeHandleInset
                                                   - fadeInHandle.width,
                                                   px - fadeInHandle.width / 2))
                EditorState.previewSetClipFade(clipItem.trackIndex, clipItem.clipIndex,
                                               px / panel.pxPerSecond,
                                               clipItem.edgeOutLength)
            }
            onReleased: EditorState.commitPreviewDrag()
            onCanceled: EditorState.cancelPreviewDrag()

            HoverHandler { cursorShape: Qt.SizeHorCursor }

            Loader {
                anchors.fill: parent
                active: clipItem.tooltipsActive
                sourceComponent: Component {
                    ThemedToolTip {
                        visible: fadeInMouse.pressed || fadeInMouse.containsMouse
                        text: qsTr("Fade in %1s").arg((clipItem.clipData.fadeIn || 0).toFixed(2))
                    }
                }
            }
        }
    }

    Rectangle {
        id: fadeOutHandle
        width: clipItem.fadeHandleSize
        height: clipItem.fadeHandleSize
        radius: clipItem.fadeHandleSize / 2
        y: clipItem.touchMode
           ? Math.max(0, (clipItem.height - clipItem.fadeHandleSize) / 2)
           : 2
        z: 40
        visible: clipItem.timelineFadeHandles && clipItem.selected
                 && clipItem.width > clipItem.fadeHandleMinWidth
        // VEDIT (조박사님): lights up a little under the pointer, so it reads as something to grab.
        color: (fadeOutMouse.containsMouse || fadeOutMouse.pressed) ? Qt.lighter(Theme.primary, 1.3) : Theme.primary
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        border.color: Theme.onMedia
        border.width: 2

        Binding {
            target: fadeOutHandle
            property: "x"
            when: !fadeOutMouse.pressed
            value: Math.max(clipItem.fadeHandleInset,
                            Math.min(clipItem.width - clipItem.fadeHandleInset - fadeOutHandle.width,
                                     clipItem.width - clipItem.edgeOutLength * panel.pxPerSecond - fadeOutHandle.width / 2))
        }

        MouseArea {
            id: fadeOutMouse
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            anchors.topMargin: -6
            anchors.bottomMargin: clipItem.height < 35 ? 4 : -6
            z: 1
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                mouse.accepted = true
                EditorState.beginPreviewDrag(qsTr("Adjust fade"))
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const px = Math.max(0, Math.min(clipItem.width,
                                                mapToItem(clipItem, mouse.x, mouse.y).x))
                fadeOutHandle.x = Math.max(clipItem.fadeHandleInset,
                                           Math.min(clipItem.width - clipItem.fadeHandleInset
                                                    - fadeOutHandle.width,
                                                    px - fadeOutHandle.width / 2))
                EditorState.previewSetClipFade(clipItem.trackIndex, clipItem.clipIndex,
                                               clipItem.edgeInLength,
                                               Math.max(0, (clipItem.width - px) / panel.pxPerSecond))
            }
            onReleased: EditorState.commitPreviewDrag()
            onCanceled: EditorState.cancelPreviewDrag()

            HoverHandler { cursorShape: Qt.SizeHorCursor }

            Loader {
                anchors.fill: parent
                active: clipItem.tooltipsActive
                sourceComponent: Component {
                    ThemedToolTip {
                        visible: fadeOutMouse.pressed || fadeOutMouse.containsMouse
                        text: qsTr("Fade out %1s").arg((clipItem.clipData.fadeOut || 0).toFixed(2))
                    }
                }
            }
        }
    }

    Rectangle {
        id: leftTrimHandle
        // Thin edge bar; hotspots still use the wide Theme width when idle.
        // VEDIT (조박사님): the yellow bar is drawn half as thick; the grab area keeps its full width.
        // VEDIT (조박사님): the thick yellow end shows only on the side the pointer is on.
        readonly property bool hot: leftTrimMouse.containsMouse || leftTrimHover.hovered || leftTrimMouse.pressed
        width: clipItem.trimHandleWidth / 2
        anchors.left: clipBackground.left
        anchors.top: clipBackground.top
        anchors.bottom: clipBackground.bottom
        color: clipItem.showTrimHandles ? Theme.primary : "transparent"
        opacity: clipItem.showTrimHandles && hot ? 1.0 : 0

        Behavior on opacity {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on width {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Loader {
            anchors.fill: parent
            active: clipItem.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    text: qsTr("Drag to trim the start")
                    visible: clipItem.showTrimHandles
                             && (leftTrimMouse.containsMouse || leftTrimHover.hovered)
                             && !leftTrimMouse.pressed
                }
            }
        }
        z: 30

        MouseArea {
            id: leftTrimMouse
            property bool selectOnRelease: false // VEDIT
            // VEDIT: a fixed-size hotspot. It used to fill the handle, which thins on hover, so
            // the pointer fell off the shrunken strip, it widened again, caught the pointer,
            // thinned … and the clip start shook under a still mouse.
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: -clipItem.trimHotspotExtra
            // VEDIT (조박사님): on desktop 10 px inside the clip (a third of the 30 px it was)
            width: clipItem.touchMode ? clipItem.trimHandleWidth + clipItem.trimHotspotExtra + 8 : 10
            // Leave the top corner for the fade-in dot.
            anchors.topMargin: clipItem.timelineFadeHandles && clipItem.showTrimHandles
                               && !clipItem.touchMode ? (clipItem.height < 35 ? 10 : 16)
                                                     : (clipItem.touchMode ? -6 : 0)
            anchors.bottomMargin: clipItem.touchMode ? -6 : 0 // VEDIT: not past the clip
            // Same reason as the move drag: these are ~38px strips at both edges of
            // every clip and they hold the grab, so on touch they turned each clip
            // boundary into another place the timeline could not be panned. Only the
            // selected clip — the one actually showing trim handles — arms them.
            enabled: !clipItem.touchMode || clipItem.showTrimHandles
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.BlankCursor

            HoverHandler {
                id: leftTrimHover
                cursorShape: Qt.BlankCursor
            }

            // Last position this edge was actually committed at, in whole pixels. A high-polling
            // mouse delivers several positionChanged per frame, and each one used to mutate the
            // project and rebuild the whole timeline model. Same idiom the move drag uses above.
            property int lastTrimPx: -2147483647
            // Where the edge was last asked to go. Negative means the gesture never moved it,
            // so there is nothing to commit.
            property real lastTrimSeconds: -1
            // Set while this handle owns an open preview drag, so the drag is closed exactly
            // once however the gesture ends.
            property bool trimming: false

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(leftTrimMouse, mouse)
                // VEDIT: select on release, not on press — selecting opens the clip's settings
                // panel (heavy for text and subtitles), and building it at the press made the
                // edge lag behind the pointer at the start of the drag.
                selectOnRelease = !wasSelected
                if (!wasSelected)
                    Haptics.select()
                // One undo entry and one dirty mark for the whole drag. Without this a trim was
                // invisible to both: it never moved the undo stack index, and the dirty flag is
                // only ever set from that index changing — so trimming, closing, and being asked
                // nothing about saving lost the work outright.
                EditorState.beginPreviewDrag(qsTr("Trim clip"))
                EditorState.beginTrimGesture(clipItem.trackIndex, clipItem.clipIndex, -1)
                lastTrimPx = -2147483647
                lastTrimSeconds = -1
                trimming = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                // A vertical drag on this strip is someone reaching for another layer, not for
                // this clip's in point.
                if (clipItem.edgeGestureScrolled(leftTrimMouse, mouse))
                    return
                const end = (clipItem.clipData.start || 0)
                            + (clipItem.clipData.duration || 0)
                const raw = mapToItem(trackRow, mouse.x, mouse.y).x / panel.pxPerSecond
                // Floor duration so the clip stays at least
                // clipMinWidth; handles remain draggable to extend.
                const newStart = Math.max(0, Math.min(raw, end - clipItem.minDurationSeconds))
                const px = Math.round(newStart * panel.pxPerSecond)
                if (px === lastTrimPx)
                    return
                lastTrimPx = px
                lastTrimSeconds = newStart
                // Previewed, not applied. The reply reports what the trim would do -- snapping
                // and every limit that can stop this edge live inside it, and the one the user
                // needs told, the source running out, has no cue on screen at all.
                const p = EditorState.previewTrimLeft(clipItem.trackIndex, clipItem.clipIndex,
                                                      newStart)
                // VEDIT (조박사님): the preview shows the frame the clip now starts on.
                EditorState.showTrimEdge(clipItem.trackIndex, clipItem.clipIndex, -1, newStart)
                // VEDIT: "unchanged" means back at the clip's own edge — show that too, or the
                // preview stays where the pointer last was and the edge stops following it.
                if (p.ok) {
                    clipItem.trimPreviewStart = p.start
                    clipItem.trimPreviewDuration = p.duration
                    clipItem.trimPreviewIn = p.inPoint
                    clipItem.trimPreviewOut = p.outPoint
                    clipItem.trimPreviewActive = true
                    panel.setTrimFollow(clipItem.clipData.linkId, clipItem.clipData.id,
                                        p.start, p.duration, p.inPoint, p.outPoint)
                }
                Haptics.trimStep(p.ok ? p.outcome : 0)
            }
            onReleased: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    // The one and only edit of the gesture. Dropping the preview first so the
                    // geometry bindings are reading the project again by the time it lands.
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimEdge() // VEDIT
                    // One call, so the edit, the undo push and finishEdit announce the timeline
                    // once between them rather than three times.
                    EditorState.commitTrim(clipItem.trackIndex, clipItem.clipIndex,
                                           -1, lastTrimSeconds)
                }
                if (selectOnRelease) { // VEDIT
                    selectOnRelease = false
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                }
            }
            onCanceled: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimEdge() // VEDIT
                    EditorState.endTrimGesture()
                    EditorState.cancelPreviewDrag()
                }
                selectOnRelease = false // VEDIT
            }
        }
    }

    Rectangle {
        id: rightTrimHandle
        // VEDIT (조박사님): the yellow bar is drawn half as thick; the grab area keeps its full width.
        // VEDIT (조박사님): the thick yellow end shows only on the side the pointer is on.
        readonly property bool hot: rightTrimMouse.containsMouse || rightTrimHover.hovered || rightTrimMouse.pressed
        width: clipItem.trimHandleWidth / 2
        anchors.right: clipBackground.right
        anchors.top: clipBackground.top
        anchors.bottom: clipBackground.bottom
        color: clipItem.showTrimHandles ? Theme.primary : "transparent"
        opacity: clipItem.showTrimHandles && hot ? 1.0 : 0

        Behavior on opacity {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on width {
            enabled: clipItem.showTrimHandles
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Loader {
            anchors.fill: parent
            active: clipItem.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    text: qsTr("Drag to trim the end")
                    visible: clipItem.showTrimHandles
                             && (rightTrimMouse.containsMouse || rightTrimHover.hovered)
                             && !rightTrimMouse.pressed
                }
            }
        }
        z: 30

        MouseArea {
            id: rightTrimMouse
            property bool selectOnRelease: false // VEDIT
            // VEDIT: fixed-size hotspot (see leftTrimMouse).
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: -clipItem.trimHotspotExtra
            // VEDIT (조박사님): on desktop 10 px inside the clip (a third of the 30 px it was)
            width: clipItem.touchMode ? clipItem.trimHandleWidth + clipItem.trimHotspotExtra + 8 : 10
            // Leave the top corner for the fade-out dot.
            anchors.topMargin: clipItem.timelineFadeHandles && clipItem.showTrimHandles
                               && !clipItem.touchMode ? (clipItem.height < 35 ? 10 : 16)
                                                     : (clipItem.touchMode ? -6 : 0)
            anchors.bottomMargin: clipItem.touchMode ? -6 : 0 // VEDIT: not past the clip
            // Same reason as the move drag: these are ~38px strips at both edges of
            // every clip and they hold the grab, so on touch they turned each clip
            // boundary into another place the timeline could not be panned. Only the
            // selected clip — the one actually showing trim handles — arms them.
            enabled: !clipItem.touchMode || clipItem.showTrimHandles
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.BlankCursor

            HoverHandler {
                id: rightTrimHover
                cursorShape: Qt.BlankCursor
            }

            // See the matching properties on the left handle.
            property int lastTrimPx: -2147483647
            // Where the edge was last asked to go. Negative means the gesture never moved it,
            // so there is nothing to commit.
            property real lastTrimSeconds: -1
            property bool trimming: false

            onPressed: (mouse) => {
                Qt.callLater(function() { clipItem.forceActiveFocus() })
                const wasSelected = clipItem.selected
                Haptics.reset()
                clipItem.beginEdgeGesture(rightTrimMouse, mouse)
                // VEDIT: select on release, not on press — selecting opens the clip's settings
                // panel (heavy for text and subtitles), and building it at the press made the
                // edge lag behind the pointer at the start of the drag.
                selectOnRelease = !wasSelected
                if (!wasSelected)
                    Haptics.select()
                EditorState.beginPreviewDrag(qsTr("Trim clip"))
                EditorState.beginTrimGesture(clipItem.trackIndex, clipItem.clipIndex, 1)
                lastTrimPx = -2147483647
                lastTrimSeconds = -1
                trimming = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                if (clipItem.edgeGestureScrolled(rightTrimMouse, mouse))
                    return
                const start = clipItem.clipData.start || 0
                const raw = mapToItem(trackRow, mouse.x, mouse.y).x / panel.pxPerSecond
                const newEnd = Math.max(raw, start + clipItem.minDurationSeconds)
                const px = Math.round(newEnd * panel.pxPerSecond)
                if (px === lastTrimPx)
                    return
                lastTrimPx = px
                lastTrimSeconds = newEnd
                // See the left handle: previewed here, committed once on release.
                const p = EditorState.previewTrimRight(clipItem.trackIndex, clipItem.clipIndex,
                                                       newEnd)
                // VEDIT (조박사님): the preview shows the frame the clip now ends on.
                EditorState.showTrimEdge(clipItem.trackIndex, clipItem.clipIndex, 1, newEnd)
                // VEDIT: "unchanged" means back at the clip's own edge — show that too, or the
                // preview stays where the pointer last was and the edge stops following it.
                if (p.ok) {
                    clipItem.trimPreviewStart = p.start
                    clipItem.trimPreviewDuration = p.duration
                    clipItem.trimPreviewIn = p.inPoint
                    clipItem.trimPreviewOut = p.outPoint
                    clipItem.trimPreviewActive = true
                    panel.setTrimFollow(clipItem.clipData.linkId, clipItem.clipData.id,
                                        p.start, p.duration, p.inPoint, p.outPoint)
                }
                Haptics.trimStep(p.ok ? p.outcome : 0)
            }
            onReleased: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    // The one and only edit of the gesture. Dropping the preview first so the
                    // geometry bindings are reading the project again by the time it lands.
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimEdge() // VEDIT
                    // One call, so the edit, the undo push and finishEdit announce the timeline
                    // once between them rather than three times.
                    EditorState.commitTrim(clipItem.trackIndex, clipItem.clipIndex,
                                           1, lastTrimSeconds)
                }
                if (selectOnRelease) { // VEDIT
                    selectOnRelease = false
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                }
            }
            onCanceled: {
                Haptics.reset()
                if (trimming) {
                    trimming = false
                    clipItem.trimPreviewActive = false
                    panel.clearTrimFollow()
                    EditorState.endTrimEdge() // VEDIT
                    EditorState.endTrimGesture()
                    EditorState.cancelPreviewDrag()
                }
                selectOnRelease = false // VEDIT
            }
        }
    }

    // VEDIT: 둥근 네 귀퉁이 — the film strip and waveform are square and cover the rounded
    // background, so the corners are cut back to the track colour on top of everything. Two clips
    // side by side then show a small notch where they meet (CapCut). Cheap: four tiny canvases.
    readonly property color cornerBackdrop: Qt.tint(Theme.panelBackground,
                                                    Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                            Theme.panelAccent.b, 0.22))
    Repeater {
        model: clipItem.width >= Theme.clipCornerRadius * 3 ? 4 : 0
        delegate: Canvas {
            required property int index
            readonly property real r: Theme.clipCornerRadius
            readonly property bool atRight: index === 1 || index === 3 // not "right": Item's anchor line
            readonly property bool atBottom: index >= 2
            width: r
            height: r
            x: atRight ? clipItem.width - r : 0
            y: atBottom ? clipItem.height - r : 0
            z: 60
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = clipItem.cornerBackdrop
                ctx.fillRect(0, 0, width, height)
                ctx.globalCompositeOperation = "destination-out"
                ctx.beginPath()
                ctx.arc(atRight ? 0 : r, atBottom ? 0 : r, r, 0, Math.PI * 2)
                ctx.fill()
            }
            Component.onCompleted: requestPaint()
            Connections {
                target: clipItem
                function onCornerBackdropChanged() { requestPaint() }
            }
        }
    }

    // VEDIT (조박사님, CapCut keyframes on the clip). KEY at the foot of a picture clip shows a red
    // line through the middle; each ◆ on it holds every value at that moment (position, size,
    // rotation, opacity, adjustments). It lights up by itself once the clip has keyframes, and
    // turns the line off and on. Double-click the line: a ◆ there. Click a ◆: pick it (the
    // playhead goes there, so the panel edits it). Drag: move it. Delete: take it out.
    readonly property bool keyCapable: trackType !== "audio" && clipData.kind !== "adjustment"
                                       && clipData.kind !== "audio"
    readonly property var keyMarks: {
        void EditorState.tracksRevision
        void clipData.start
        void clipData.duration
        return keyCapable ? EditorState.clipKeyframeMarks(trackIndex, clipIndex) : []
    }
    readonly property bool hasKeys: keyMarks.length > 0
    // VEDIT (조박사님): a keyframe made or picked on this clip turns KEY on and shows the ◆ line,
    // even if KEY had been switched off.
    property int seenKeyCount: -1
    onKeyMarksChanged: {
        if (seenKeyCount >= 0 && keyMarks.length > seenKeyCount && panel.keyLineState[clipData.id] === false)
            panel.setKeyLine(clipData.id, true)
        seenKeyCount = keyMarks.length
    }
    Connections {
        target: EditorState
        enabled: clipItem.keyCapable
        function onSelectedVolumeKeyChanged() {
            if (EditorState.selectedVolumeKey.clipId === clipItem.clipData.id && !clipItem.keyLineOn)
                panel.setKeyLine(clipItem.clipData.id, true)
        }
    }
    readonly property bool keyLineOn: {
        const st = panel.keyLineState ? panel.keyLineState[clipData.id] : undefined
        return keyCapable && (st === undefined ? hasKeys : st === true)
    }

    // VEDIT (조박사님): the fade (or 들어오기 / 나가기 movement) lengths in seconds, in the same box
    // the crossfades show, at the middle of each edge's ramp.
    Repeater {
        model: [{ "side": "in" }, { "side": "out" }]
        delegate: Rectangle {
            required property var modelData
            readonly property real len: modelData.side === "in" ? clipItem.edgeInLength : clipItem.edgeOutLength
            readonly property real lenPx: len * panel.pxPerSecond
            visible: len > 0.001 && clipItem.width > width + 8 && clipItem.height > 24
            width: edgeLenText.implicitWidth + 8
            height: edgeLenText.implicitHeight + 2
            radius: 3
            z: 47
            x: modelData.side === "in"
               ? Math.max(2, Math.min(clipItem.width - width - 2, lenPx / 2 - width / 2))
               : Math.max(2, Math.min(clipItem.width - width - 2, clipItem.width - lenPx / 2 - width / 2))
            y: Math.round(clipItem.height / 2 - height / 2)
            color: Qt.rgba(0.12, 0.12, 0.12, 0.85)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.75)
            Text {
                id: edgeLenText
                anchors.centerIn: parent
                text: panel.overlapLengthLabel(parent.len)
                color: "#ffffff"
                font.family: Theme.monoFontFamily
                font.pixelSize: 11
            }
        }
    }

    Rectangle { // KEY
        visible: clipItem.keyCapable && clipItem.width > 40 && clipItem.height > 30
        x: 4
        y: clipItem.height - height - 4
        z: 48
        width: 24
        height: 13
        radius: 2
        color: clipItem.keyLineOn ? "#ff3b30" : Qt.rgba(0, 0, 0, 0.55)
        border.width: 1
        border.color: clipItem.keyLineOn || clipItem.hasKeys ? "#ff3b30" : Qt.rgba(1, 1, 1, 0.6)
        Text {
            anchors.centerIn: parent
            text: "KEY"
            color: "#ffffff"
            font.pixelSize: 8
            font.bold: true
            font.family: Theme.fontFamily
        }
        HoverHandler { id: keyBtnHover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.setKeyLine(clipItem.clipData.id, !clipItem.keyLineOn)
        }
        ThemedToolTip {
            visible: keyBtnHover.hovered
            text: qsTr("Keyframe line — double-click it to add a keyframe")
        }
    }

    Item {
        id: keyLine
        visible: clipItem.keyLineOn
        anchors.fill: parent
        z: 46
        readonly property real midY: Math.round(height / 2)
        function xOf(sec) { return (sec - (Number(clipItem.clipData.start) || 0)) * panel.pxPerSecond }
        function secOf(x) { return (Number(clipItem.clipData.start) || 0) + x / panel.pxPerSecond }

        Rectangle {
            x: 0
            y: keyLine.midY - 1
            width: parent.width
            height: 2
            color: "#ff3b30"
        }
        MouseArea { // the line: double-click adds a ◆
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            onPressed: (mouse) => {
                if (Math.abs(mouse.y - keyLine.midY) > 7)
                    mouse.accepted = false // not the line: the clip takes it
            }
            onDoubleClicked: (mouse) => {
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                EditorState.addClipKeyframesAt(clipItem.trackIndex, clipItem.clipIndex, keyLine.secOf(mouse.x))
            }
        }
        Repeater {
            model: clipItem.keyMarks
            delegate: Item {
                id: keyMark
                required property var modelData
                readonly property real markSeconds: Number(modelData)
                property real dragDx: 0
                // VEDIT (조박사님): picked, or — on the selected clip — the one under the playhead, which
                // is where a keyframe made from the panel (◇) lands: it is yellow and draggable at once.
                readonly property bool picked: (EditorState.selectedVolumeKey.clipId === clipItem.clipData.id
                    && Math.abs(Number(EditorState.selectedVolumeKey.seconds) - markSeconds) < 0.002)
                    || (clipItem.selected && EditorState.selectedVolumeKey.clipId === undefined
                        && Math.abs(EditorState.playheadSeconds - markSeconds) < 0.02)
                width: 18
                height: 18
                x: keyLine.xOf(markSeconds) - width / 2 + dragDx
                y: keyLine.midY - height / 2
                z: 2
                visible: x > -width && x < clipItem.width
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    rotation: 45
                    color: keyMark.picked || keyMarkMouse.pressed ? Theme.primary : "#ffffff"
                    border.width: 1
                    border.color: "#000000"
                }
                MouseArea {
                    id: keyMarkMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true // VEDIT: the clip's own drag must not take the ◆ away
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeHorCursor
                    property real pressX: 0
                    property bool moved: false
                    onPressed: (mouse) => {
                        pressX = mapToItem(clipItem, mouse.x, 0).x
                        moved = false
                        EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                        EditorState.selectVolumeKey(clipItem.trackIndex, clipItem.clipIndex, keyMark.markSeconds)
                        if (mouse.button === Qt.RightButton)
                            keyMarkMenu.popup()
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed || mouse.buttons !== Qt.LeftButton)
                            return
                        const dx = mapToItem(clipItem, mouse.x, 0).x - pressX
                        if (Math.abs(dx) > 3)
                            moved = true
                        if (moved)
                            keyMark.dragDx = Math.max(-keyLine.xOf(keyMark.markSeconds),
                                                      Math.min(clipItem.width - keyLine.xOf(keyMark.markSeconds), dx))
                    }
                    onReleased: {
                        if (!moved)
                            return
                        const to = keyMark.markSeconds + keyMark.dragDx / panel.pxPerSecond
                        keyMark.dragDx = 0
                        moved = false
                        EditorState.moveClipKeyframesAt(clipItem.trackIndex, clipItem.clipIndex,
                                                         keyMark.markSeconds, to)
                        EditorState.selectVolumeKey(clipItem.trackIndex, clipItem.clipIndex, to)
                    }
                    onCanceled: {
                        keyMark.dragDx = 0
                        moved = false
                    }
                }
                ThemedContextMenu {
                    id: keyMarkMenu
                    ThemedMenuItem {
                        text: qsTr("Delete")
                        icon.name: Theme.icons.trash
                        onTriggered: EditorState.deleteSelectedVolumeKey()
                    }
                }
            }
        }
    }

    // VEDIT (조박사님, Vegas volume envelope). VL at the foot of a sound clip shows a white volume
    // line: middle = 0 dB, top = +12 dB (400 %), bottom = silence. Double-click the line: a point
    // (selected, yellow). Click a point: select it. Drag it: left/right moves it in time, up/down
    // sets the volume. Delete (key or right-click menu) removes it. Picking a point moves the
    // playhead there, so the audio panel's volume edits that point.
    readonly property bool envelopeOn: trackType === "audio" && panel.envelopeClips[clipData.id] === true
    // The line's own scale (also AudioMixer's envelopeNorm): 0.5 = 0 dB, 1 = +12 dB, 0 = -48 dB.
    function envNorm(g) {
        if (g <= 0.0001)
            return 0
        const db = 20 * Math.log(g) / Math.LN10
        return db >= 0 ? 0.5 + 0.5 * Math.min(1, db / 12) : 0.5 * Math.max(0, 1 + db / 48)
    }
    function envGainOfNorm(n) {
        n = Math.max(0, Math.min(1, n))
        if (n <= 0.0005)
            return 0
        const db = n >= 0.5 ? (n - 0.5) / 0.5 * 12 : (n / 0.5 - 1) * 48
        return Math.pow(10, db / 20)
    }
    function envGainToY(g) {
        const h = Math.max(1, height - 6)
        let n = 0
        if (g > 0.0001) {
            const db = 20 * Math.log(g) / Math.LN10
            n = db >= 0 ? 0.5 + 0.5 * Math.min(1, db / 12) : 0.5 * Math.max(0, 1 + db / 48)
        }
        return 3 + h * (1 - n)
    }
    function envYToGain(y) {
        const h = Math.max(1, height - 6)
        const n = Math.max(0, Math.min(1, 1 - (y - 3) / h))
        if (n <= 0.0005)
            return 0
        const db = n >= 0.5 ? (n - 0.5) / 0.5 * 12 : (n / 0.5 - 1) * 48
        return Math.pow(10, db / 20)
    }
    function envDbText(g) {
        if (g <= 0.0001)
            return "-inf dB"
        const db = 20 * Math.log(g) / Math.LN10
        return (db > 0 ? "+" : "") + db.toFixed(1) + " dB"
    }
    property int envDragIndex: -1
    property real envDragSec: 0
    property real envDragGain: 1
    readonly property var envKeys: {
        void EditorState.tracksRevision
        void clipData.start
        void clipData.duration
        if (!envelopeOn)
            return []
        const keys = EditorState.clipKeyframes(trackIndex, clipIndex, "volume")
        const out = []
        for (let i = 0; i < keys.length; ++i)
            out.push({ "seconds": keys[i].seconds, "value": keys[i].value })
        if (envDragIndex >= 0 && envDragIndex < out.length)
            out[envDragIndex] = { "seconds": envDragSec, "value": envDragGain }
        out.sort((a, b) => a.seconds - b.seconds)
        return out
    }
    function envGainAt(sec) {
        const k = envKeys
        if (k.length === 0)
            return 1
        if (sec <= k[0].seconds)
            return k[0].value
        for (let i = 1; i < k.length; ++i)
            if (sec <= k[i].seconds) {
                const t = (sec - k[i - 1].seconds) / Math.max(1e-9, k[i].seconds - k[i - 1].seconds)
                const st = t * t * (3 - 2 * t) // VEDIT: the same S-curve the sound follows,
                const n0 = clipItem.envNorm(k[i - 1].value) // on the line's own (dB) scale
                const n1 = clipItem.envNorm(k[i].value)
                return clipItem.envGainOfNorm(n0 + (n1 - n0) * st)
            }
        return k[k.length - 1].value
    }
    function envXOf(sec) { return (sec - (Number(clipData.start) || 0)) * panel.pxPerSecond }
    function envSecOf(x) { return (Number(clipData.start) || 0) + x / panel.pxPerSecond }

    Rectangle { // VL
        visible: clipItem.trackType === "audio" && clipItem.width > 40 && clipItem.height > 30
        x: 4
        y: clipItem.height - height - 4
        z: 48
        width: 24 // VEDIT (조박사님): 30 % smaller, room for "VOL"
        height: 13
        radius: 2
        color: clipItem.envelopeOn ? Theme.primary : Qt.rgba(0, 0, 0, 0.55)
        border.width: 1
        border.color: clipItem.envelopeOn ? Theme.primary : Qt.rgba(1, 1, 1, 0.6)
        Text {
            anchors.centerIn: parent
            text: "VOL" // VEDIT (조박사님)
            color: clipItem.envelopeOn ? "#000000" : "#ffffff"
            font.pixelSize: 8
            font.bold: true
            font.family: Theme.fontFamily
        }
        HoverHandler { id: vlHover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.toggleEnvelope(clipItem.clipData.id)
        }
        ThemedToolTip {
            visible: vlHover.hovered
            text: qsTr("Volume line — double-click it to add a point")
        }
    }

    Item {
        id: envelope
        visible: clipItem.envelopeOn
        anchors.fill: parent
        z: 47
        Canvas {
            id: envCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const k = clipItem.envKeys
                ctx.strokeStyle = "#ff3b30" // VEDIT (조박사님): the volume line in red
                ctx.lineWidth = 1.5
                ctx.beginPath()
                if (k.length === 0) {
                    const y = clipItem.envGainToY(1)
                    ctx.moveTo(0, y)
                    ctx.lineTo(width, y)
                } else {
                    ctx.moveTo(0, clipItem.envGainToY(k[0].value))
                    ctx.lineTo(clipItem.envXOf(k[0].seconds), clipItem.envGainToY(k[0].value))
                    // Smooth S-curves between points (조박사님), sampled every few pixels.
                    for (let i = 1; i < k.length; ++i) {
                        const x0 = clipItem.envXOf(k[i - 1].seconds)
                        const x1 = clipItem.envXOf(k[i].seconds)
                        const steps = Math.max(2, Math.ceil((x1 - x0) / 3))
                        for (let j = 1; j <= steps; ++j) {
                            const t = j / steps
                            const st = t * t * (3 - 2 * t)
                            const n0 = clipItem.envNorm(k[i - 1].value)
                            const n1 = clipItem.envNorm(k[i].value)
                            const n = n0 + (n1 - n0) * st
                            ctx.lineTo(x0 + (x1 - x0) * t, 3 + Math.max(1, height - 6) * (1 - n))
                        }
                    }
                    ctx.lineTo(width, clipItem.envGainToY(k[k.length - 1].value))
                }
                ctx.stroke()
            }
            Connections {
                target: clipItem
                function onEnvKeysChanged() { envCanvas.requestPaint() }
                function onWidthChanged() { envCanvas.requestPaint() }
                function onHeightChanged() { envCanvas.requestPaint() }
                function onEnvelopeOnChanged() { envCanvas.requestPaint() }
            }
        }
        Repeater {
            model: clipItem.envKeys
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property bool picked: (EditorState.selectedVolumeKey.clipId === clipItem.clipData.id
                    && Math.abs(Number(EditorState.selectedVolumeKey.seconds) - modelData.seconds) < 0.002)
                    || clipItem.envDragIndex === index
                x: clipItem.envXOf(modelData.seconds) - width / 2
                y: clipItem.envGainToY(modelData.value) - height / 2
                width: 9
                height: 9
                color: picked ? Theme.primary : "#ffffff"
                border.width: 1
                border.color: "#000000"
            }
        }
        Rectangle { // the value while dragging a point
            visible: clipItem.envDragIndex >= 0
            x: Math.min(parent.width - width, Math.max(0, clipItem.envXOf(clipItem.envDragSec) + 8))
            y: Math.max(0, clipItem.envGainToY(clipItem.envDragGain) - height - 6)
            width: envDragText.implicitWidth + 8
            height: envDragText.implicitHeight + 4
            radius: 3
            color: Qt.rgba(0.1, 0.1, 0.1, 0.9)
            Text {
                id: envDragText
                anchors.centerIn: parent
                text: clipItem.envDbText(clipItem.envDragGain)
                color: "#ffffff"
                font.pixelSize: 12
                font.family: Theme.monoFontFamily
            }
        }
        MouseArea {
            id: envMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            property int hitIndex: -1
            property real pressX: 0
            property real pressY: 0
            property real fromSec: 0
            property bool moved: false
            function keyAt(mx, my) {
                const k = clipItem.envKeys
                for (let i = 0; i < k.length; ++i) {
                    const dx = clipItem.envXOf(k[i].seconds) - mx
                    const dy = clipItem.envGainToY(k[i].value) - my
                    if (dx * dx + dy * dy <= 64)
                        return i
                }
                return -1
            }
            function nearLine(mx, my) {
                return Math.abs(clipItem.envGainToY(clipItem.envGainAt(clipItem.envSecOf(mx))) - my) <= 7
            }
            cursorShape: containsMouse && keyAt(mouseX, mouseY) >= 0 ? Qt.SizeAllCursor
                       : (containsMouse && nearLine(mouseX, mouseY) ? Qt.PointingHandCursor : Qt.ArrowCursor)
            onPressed: (mouse) => {
                hitIndex = keyAt(mouse.x, mouse.y)
                if (hitIndex < 0 && !nearLine(mouse.x, mouse.y)) {
                    mouse.accepted = false // not the envelope: the clip takes it
                    return
                }
                moved = false
                pressX = mouse.x
                pressY = mouse.y
                if (hitIndex >= 0) {
                    const key = clipItem.envKeys[hitIndex]
                    fromSec = key.seconds
                    EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                    EditorState.selectVolumeKey(clipItem.trackIndex, clipItem.clipIndex, key.seconds)
                    if (mouse.button === Qt.RightButton)
                        envMenu.popup()
                }
            }
            onPositionChanged: (mouse) => {
                if (!pressed || hitIndex < 0 || mouse.buttons !== Qt.LeftButton)
                    return
                if (!moved && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 3)
                    return
                if (!moved) {
                    moved = true
                    EditorState.beginPreviewDrag(qsTr("Volume point"))
                }
                const k = clipItem.envKeys
                const start = Number(clipItem.clipData.start) || 0
                const lo = hitIndex > 0 ? k[hitIndex - 1].seconds + 0.01 : start
                const hi = hitIndex < k.length - 1 ? k[hitIndex + 1].seconds - 0.01
                                                   : start + (Number(clipItem.clipData.duration) || 0)
                const sec = Math.max(lo, Math.min(hi, clipItem.envSecOf(mouse.x)))
                const gain = Math.min(4, clipItem.envYToGain(mouse.y))
                EditorState.previewMoveClipKeyframe(clipItem.trackIndex, clipItem.clipIndex, "volume",
                                                    clipItem.envDragIndex >= 0 ? clipItem.envDragSec : fromSec,
                                                    sec, gain)
                clipItem.envDragIndex = hitIndex
                clipItem.envDragSec = sec
                clipItem.envDragGain = gain
            }
            onReleased: {
                if (moved) {
                    const sec = clipItem.envDragSec
                    EditorState.commitPreviewDrag()
                    clipItem.envDragIndex = -1
                    EditorState.selectVolumeKey(clipItem.trackIndex, clipItem.clipIndex, sec)
                }
                moved = false
            }
            onCanceled: {
                if (moved)
                    EditorState.cancelPreviewDrag()
                clipItem.envDragIndex = -1
                moved = false
            }
            onDoubleClicked: (mouse) => {
                // VEDIT (조박사님): double-click a point → back to 0 dB.
                const onKey = keyAt(mouse.x, mouse.y)
                if (onKey >= 0) {
                    const key = clipItem.envKeys[onKey]
                    EditorState.setClipKeyframe(clipItem.trackIndex, clipItem.clipIndex, "volume", key.seconds, 1.0)
                    EditorState.selectVolumeKey(clipItem.trackIndex, clipItem.clipIndex, key.seconds)
                    return
                }
                const sec = clipItem.envSecOf(mouse.x)
                EditorState.selectClip(clipItem.trackIndex, clipItem.clipIndex)
                EditorState.addVolumeKey(clipItem.trackIndex, clipItem.clipIndex, sec, clipItem.envGainAt(sec))
            }
        }
        ThemedContextMenu {
            id: envMenu
            ThemedMenuItem {
                text: qsTr("Delete")
                icon.name: Theme.icons.trash
                onTriggered: EditorState.deleteSelectedVolumeKey()
            }
        }
    }

    // VEDIT (조박사님): a 1 px black line where this clip meets the one before it.
    readonly property bool meetsPrevious: {
        const track = panel.tracks && panel.tracks[trackIndex]
        if (!track || !track.clips)
            return false
        const start = Number(clipData.start) || 0
        for (let i = 0; i < track.clips.length; ++i) {
            const c = track.clips[i]
            if (c.id !== clipData.id && Math.abs(c.start + c.duration - start) < 0.0005)
                return true
        }
        return false
    }
    Rectangle {
        visible: clipItem.meetsPrevious
        x: 0
        width: 1
        height: parent.height
        color: "#000000"
        z: 55
    }

    // VEDIT: a locked clip reads as held in place — dimmed, with a small lock in the corner.
    Rectangle {
        anchors.fill: parent
        visible: clipItem.trackLocked
        radius: Theme.clipCornerRadius // VEDIT: rounded so the meeting point of two clips shows
        color: Qt.rgba(0, 0, 0, 0.35)
        z: 50
        IconGlyph {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            glyph: Theme.icons.lock
            iconSize: 14
            iconColor: "#ffffff"
        }
    }
}
