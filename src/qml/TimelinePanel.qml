import QtQuick
import QtCore // VEDIT: Settings
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"
import "components/timeline"

PanelFrame {
    id: root

    property real zoom: 1.0
    property string propertiesTab: ""

    // Timeline tool mode (CapCut-style exclusive modes). One of:
    //   ""          Select — normal editing (toolbar pointer / V)
    //   "split"     Blade — click a clip to split it (toolbar scissors / B)
    //   "trimStart"  click a clip to drop everything left of the cut line
    //   "trimEnd"    click a clip to drop everything right of the cut line
    // While a cut tool is active, hovering the timeline shows a red dashed
    // virtual playhead; trim tools also tint the doomed side of the clip.
    property string timelineTool: ""
    // Snapped position (seconds) of the virtual cut playhead; < 0 when not hovering.
    property real cutHoverSeconds: -1
    // Clip physically under the pointer while a tool is active (-1 when none).
    property int cutHoverTrack: -1
    property int cutHoverClip: -1
    onTimelineToolChanged: if (timelineTool === "") {
        cutHoverSeconds = -1
        cutHoverTrack = -1
        cutHoverClip = -1
    }

    property real panLastSceneX: 0
    property real panLastSceneY: 0

    // CapCut: V = Select, B = Blade. These are registered actions ("selectTool" /
    // "bladeTool") so they appear in the shortcut list and can be rebound; the tool
    // state lives here rather than in the backend, so Main.qml dispatches them
    // alongside Escape, which exits the tool without clearing the selection.

    // Y offset of a track row within the track column (excludes ruler/bookmark).
    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return cursor
    }
    // Trailing empty runway after the last clip — constant pixel length at any
    // zoom (viewport-relative), not a fixed number of seconds.
    readonly property real timelineEndPadPx: Math.max(
        Theme.timelineEndPadMinPx, flick.width * Theme.timelineEndPadFraction)

    // Fixed floor for wheel/slider zoom-out. Not tied to project duration —
    // use fitZoom() when you want the timeline fitted to the viewport.
    // Low enough for multi-hour projects without live recalibration.
    readonly property real minZoom: 0.0001
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    // Exposed for clip filmstrip viewport culling (Flickable ids are local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width
    // Exposed so the overview strip can clamp its scroll target without
    // duplicating the Flickable's contentWidth formula.
    readonly property real timelineContentWidth: flick.contentWidth

    // Fit the whole project (plus end pad) into the timeline viewport.
    function fitZoom() {
        if (!(EditorState.durationSeconds > 0)) {
            zoom = 1.0
            flick.contentX = 0
            return
        }
        const viewportW = Math.max(flick.width, 1)
        const usable = Math.max(viewportW - timelineEndPadPx, 1)
        const fit = usable / (EditorState.durationSeconds * Theme.pixelsPerSecondBase)
        zoom = Math.max(minZoom, Math.min(maxZoom, fit))
        flick.contentX = 0
    }

    // Keep `anchorSeconds` glued to the same viewport X after the scale change.
    // Without this, zooming expands from the content origin and the playhead
    // (or the point under the cursor) drifts off to the right.
    function setZoomAround(newZoom, anchorSeconds, viewportX) {
        const z = Math.max(minZoom, Math.min(maxZoom, newZoom))
        if (z === zoom)
            return
        contentXAnimation.stop()
        zoom = z
        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(newMaxX,
                                              anchorSeconds * pxPerSecond - viewportX))
    }

    // Toolbar / slider zoom: hold the playhead steady in the viewport.
    function setZoom(newZoom) {
        const anchorSeconds = Math.max(0, EditorState.playheadSeconds)
        // VEDIT (조박사님): and keep it on screen.
        const viewportX = Math.max(0, Math.min(Math.max(1, flick.width), anchorSeconds * pxPerSecond - flick.contentX))
        setZoomAround(newZoom, anchorSeconds, viewportX)
    }

    // Ruler tick interval (seconds): the smallest "nice" step whose labels still
    // have room to breathe at the current zoom, so timestamps never squash.
    // VEDIT: what the ruler shows under a second — frames (30 by default, or 24 / 25 / 60) or
    // hundredths. Picked from the ruler's right-click menu and remembered.
    Settings {
        id: rulerSettings
        category: "timeline"
        property int rulerFps: 30
    }
    // VEDIT: an overlap's length as Vegas writes it — "12.17" is 12 s 17 frames (at the ruler's
    // frame rate; hundredths when the ruler shows them), "1:05.03" past a minute.
    function overlapLengthLabel(seconds) {
        const fps = root.rulerFps > 0 ? root.rulerFps : 100
        let whole = Math.floor(seconds + 1e-6)
        let part = Math.round((seconds - whole) * fps)
        if (part >= fps) {
            whole += 1
            part = 0
        }
        const pp = String(part).padStart(fps >= 100 ? 2 : String(fps - 1).length, "0")
        if (whole >= 60)
            return Math.floor(whole / 60) + ":" + String(whole % 60).padStart(2, "0") + "." + pp
        return whole + "." + pp
    }
    readonly property int rulerFps: [0, 24, 25, 30, 60].indexOf(rulerSettings.rulerFps) >= 0
                                    ? rulerSettings.rulerFps : 30
    function setRulerFps(fps) {
        rulerSettings.rulerFps = fps
    }
    // Frame steps that divide the second evenly (2, 5, 10, 15 frames … at 30 fps).
    readonly property var subSecondSteps: {
        if (rulerFps <= 0)
            return [0.01, 0.02, 0.05, 0.1, 0.2, 0.5]
        const out = []
        for (const k of [1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30])
            if (k < rulerFps && rulerFps % k === 0)
                out.push(k / rulerFps)
        return out
    }
    onPxPerSecondChanged: EditorState.setSnapPixelsPerSecond(pxPerSecond) // VEDIT: snap reach follows the zoom
    Component.onCompleted: EditorState.setSnapPixelsPerSecond(pxPerSecond)

    readonly property real tickStepSeconds: {
        const minLabelPx = 74
        const needed = minLabelPx / pxPerSecond
        const steps = subSecondSteps.concat([1, 2, 5, 10, 15, 30,
                       60, 120, 300, 600, 900, 1800, 3600,
                       7200, 10800, 14400, 21600, 43200])
        for (var i = 0; i < steps.length; i++)
            if (steps[i] >= needed)
                return steps[i]
        return steps[steps.length - 1]
    }

    // HH:MM:SS, plus :FF frames (or .CC hundredths) once zoomed into sub-second ticks.
    function formatTick(seconds) {
        const pad = (n) => (n < 10 ? "0" : "") + n
        if (rulerFps > 0) { // VEDIT: 00:00:05:15 = 5 seconds and 15 frames
            const ff = Math.round(Math.max(0, seconds) * rulerFps)
            const whole = Math.floor(ff / rulerFps)
            let out = pad(Math.floor(whole / 3600)) + ":" + pad(Math.floor((whole % 3600) / 60)) + ":"
                    + pad(whole % 60)
            if (tickStepSeconds < 1)
                out += ":" + pad(ff % rulerFps)
            return out
        }
        const cc = Math.round(Math.max(0, seconds) * 100)
        let out = pad(Math.floor(cc / 360000)) + ":"
                + pad(Math.floor((cc % 360000) / 6000)) + ":"
                + pad(Math.floor((cc % 6000) / 100))
        if (tickStepSeconds < 1)
            out += "." + pad(cc % 100)
        return out
    }
    readonly property var tracks: EditorState.tracks
    readonly property real playheadSeconds: EditorState.playheadSeconds
    // True between beginPlayheadSeek and endPlayheadSeek when playback was
    // interrupted so a click or drag could land, and should resume on release.
    property bool resumePlaybackAfterSeek: false
    readonly property int selectedTrack: EditorState.selectedTrack
    readonly property int selectedClip: EditorState.selectedClip

    // True while a library asset is being dragged in from the media panel.
    readonly property bool assetDragActive: EditorState.draggingAssetIndex >= 0

    // Nothing here may depend on the drag: a reserved lane that appeared when a
    // drag started shifted every track row, the Flickable's contentHeight and
    // the drop overlay at the exact moment QDrag::exec() entered its nested
    // event loop, so Mutter hit-tested against geometry that had just moved out
    // from under it — the target vanished mid-drag and only the track already
    // under the pointer would accept. A new track is signalled by an insertion
    // line drawn over the tracks instead, which costs no layout.

    // Live snap guide (seconds; < 0 when hidden) shown while dragging a clip.
    property real snapGuideSeconds: -1
    // Landing preview outline: where a dragged asset (from the library) or an
    // existing clip being moved would come to rest, snapped.
    property int dropTrackIndex: -1
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    // True while a library asset would land on a brand-new track, and the index
    // that track would be inserted at (0 = above everything, tracks.length =
    // below everything).
    property bool dropCreatesNewTrack: false
    property int dropNewTrackIndex: 0
    // Clip under an in-progress effect drag (for drop highlight).
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1
    // CapCut-style marquee (drag on empty track space to box-select clips).
    property bool marqueeActive: false
    property bool marqueeAdditive: false
    property real marqueeOriginX: 0
    property real marqueeOriginY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    readonly property real marqueeLeft: Math.min(marqueeOriginX, marqueeCurrentX)
    readonly property real marqueeTop: Math.min(marqueeOriginY, marqueeCurrentY)
    readonly property real marqueeWidth: Math.abs(marqueeCurrentX - marqueeOriginX)
    readonly property real marqueeHeight: Math.abs(marqueeCurrentY - marqueeOriginY)

    // Clips whose timeline rect intersects the marquee (track-column coords).
    function clipsInMarquee(left, top, right, bottom) {
        const pairs = []
        for (let t = 0; t < tracks.length; t++) {
            const trackTop = trackOffsetY(t)
            const trackBottom = trackTop + trackHeight(t)
            if (trackBottom < top || trackTop > bottom)
                continue
            const clips = tracks[t].clips
            for (let c = 0; c < clips.length; c++) {
                const clipLeft = clips[c].start * pxPerSecond
                const clipRight = (clips[c].start + clips[c].duration) * pxPerSecond
                if (clipRight < left || clipLeft > right)
                    continue
                pairs.push({ "track": t, "clip": c })
            }
        }
        return pairs
    }

    function applyMarqueeSelection() {
        const hits = clipsInMarquee(marqueeLeft, marqueeTop,
                                    marqueeLeft + marqueeWidth,
                                    marqueeTop + marqueeHeight)
        if (marqueeAdditive) {
            const merged = []
            const seen = {}
            const existing = EditorState.selection
            for (let i = 0; i < existing.length; i++) {
                const key = existing[i].track + ":" + existing[i].clip
                seen[key] = true
                merged.push({ "track": existing[i].track, "clip": existing[i].clip })
            }
            for (let i = 0; i < hits.length; i++) {
                const key = hits[i].track + ":" + hits[i].clip
                if (seen[key])
                    continue
                seen[key] = true
                merged.push(hits[i])
            }
            EditorState.setSelection(merged)
        } else {
            EditorState.setSelection(hits)
        }
    }

    function endMarquee(apply) {
        if (!marqueeActive)
            return
        if (apply && (marqueeWidth > 2 || marqueeHeight > 2))
            applyMarqueeSelection()
        else if (apply && !marqueeAdditive) {
            EditorState.clearSelection()
            // VEDIT: CapCut — a plain click on empty timeline space moves the playhead there.
            EditorState.playheadSeconds = Math.max(0, marqueeOriginX) / root.pxPerSecond
        }
        marqueeActive = false
    }

    // While dragging a clip, other selected clips (linked A/V partners included)
    // ride along on the X and Y axes so they don't sit still until drop. CapCut-style.

    // A trim in progress, broadcast so the dragged clip's linked A/V companion can follow it on
    // screen. The commit gives the partner identical timing (syncLinkedTiming), so the preview
    // is simply the same geometry applied to both.
    property bool trimFollowActive: false
    property string trimFollowLinkId: ""
    property string trimFollowClipId: ""
    property real trimFollowStart: 0
    property real trimFollowDuration: 0
    property real trimFollowIn: 0
    property real trimFollowOut: 0

    function setTrimFollow(linkId, clipId, start, duration, inPoint, outPoint) {
        trimFollowLinkId = linkId || ""
        trimFollowClipId = clipId || ""
        trimFollowStart = start
        trimFollowDuration = duration
        trimFollowIn = inPoint
        trimFollowOut = outPoint
        trimFollowActive = trimFollowLinkId !== ""
    }

    function clearTrimFollow() {
        trimFollowActive = false
        trimFollowLinkId = ""
        trimFollowClipId = ""
    }

    // VEDIT (조박사님): 끼워 넣기 preview. While a clip's head is on a seam of a track with 빈틈 메움
    // on, the clips from the seam on stand aside by its length and the opening shows red.
    property int insertTrack: -1
    property real insertSeamSec: 0
    property real insertDurSec: 0
    property string insertBeforeId: ""
    property string insertExcludeId: ""
    readonly property real insertSnapPx: 18

    function clearInsertPreview() {
        insertTrack = -1
        insertBeforeId = ""
        insertExcludeId = ""
    }

    // headSec: where the dragged clip's head is now. Picks the seam (the start of a clip, or the end
    // of the last) closest to it within insertSnapPx; none in reach clears the preview.
    function updateInsertPreview(trackIdx, headSec, durSec, excludeId) {
        if (trackIdx < 0 || trackIdx >= tracks.length || !EditorState.trackInsertsOnDrop(trackIdx)) {
            clearInsertPreview()
            return
        }
        const clips = (tracks[trackIdx].clips || []).filter(c => c.id !== excludeId)
            .sort((a, b) => a.start - b.start)
        let best = -1, bestDist = 1e9, bestBefore = "", bestSeam = 0
        for (let i = 0; i <= clips.length; ++i) {
            const seam = i < clips.length ? clips[i].start
                                          : (clips.length ? clips[clips.length - 1].start + clips[clips.length - 1].duration : 0)
            const dist = Math.abs(headSec - seam) * pxPerSecond
            if (dist < bestDist) {
                bestDist = dist
                best = i
                bestSeam = seam
                bestBefore = i < clips.length ? clips[i].id : ""
            }
        }
        // The end of the track is an ordinary drop, not an insert.
        if (best < 0 || bestDist > insertSnapPx || best === clips.length) {
            clearInsertPreview()
            return
        }
        insertTrack = trackIdx
        insertSeamSec = bestSeam
        insertDurSec = durSec
        insertBeforeId = bestBefore
        insertExcludeId = excludeId
    }

    property bool moveFollowActive: false
    property int moveLeaderTrack: -1
    property int moveLeaderClip: -1
    property real moveFollowDeltaX: 0
    property real moveFollowDeltaY: 0

    function beginMoveFollow(trackIndex, clipIndex) {
        moveLeaderTrack = trackIndex
        moveLeaderClip = clipIndex
        moveFollowDeltaX = 0
        moveFollowDeltaY = 0
        moveFollowActive = true
    }

    function updateMoveFollow(deltaX, deltaY) {
        if (!moveFollowActive)
            return
        moveFollowDeltaX = deltaX
        moveFollowDeltaY = deltaY || 0
    }

    function clearMoveFollow() {
        moveFollowActive = false
        moveLeaderTrack = -1
        moveLeaderClip = -1
        moveFollowDeltaX = 0
        moveFollowDeltaY = 0
    }

    // Shared by library drops and in-timeline clip moves so both snap and show
    // the same outline the same way.
    // VEDIT (조박사님, Vegas volume envelope): which audio clips show their volume line (VL).
    property var envelopeClips: ({})
    // VEDIT (조박사님, CapCut keyframes): a clip's KEY line, when the user turned it on or off
    // (absent: on exactly when the clip has keyframes) — and the lane above the tracks, shown only
    // from a clip's right-click menu.
    property var keyLineState: ({})
    function setKeyLine(clipId, on) {
        const next = Object.assign({}, keyLineState)
        next[clipId] = on
        keyLineState = next
    }
    property bool keyframeLaneShown: false
    function toggleEnvelope(clipId) {
        const next = Object.assign({}, envelopeClips)
        next[clipId] = !next[clipId]
        envelopeClips = next
    }

    // VEDIT (조박사님, Vegas): the clip being dragged, so its live overlaps can be drawn as it goes.
    property string dropExcludeId: ""

    // VEDIT: where the dragged clip, at its live spot, overlaps the clips of `trackIndex` —
    // [{start, end}] in seconds — drawn as crossfades from the moment the overlap begins.
    // VEDIT: the clip whose end is being dragged out, at its live length (see TimelineClipItem).
    property int liveTrimTrack: -1
    property string liveTrimId: ""
    property real liveTrimStart: 0
    property real liveTrimDuration: 0
    function setLiveTrim(trackIndex, clipId, start, duration) {
        liveTrimTrack = trackIndex
        liveTrimId = clipId
        liveTrimStart = start
        liveTrimDuration = duration
    }
    function clearLiveTrim(clipId) {
        if (clipId === liveTrimId) {
            liveTrimTrack = -1
            liveTrimId = ""
        }
    }

    function liveOverlaps(trackIndex) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return []
        let a0 = 0, a1 = 0, skip = ""
        if (moveFollowActive && dropTrackIndex === trackIndex) {
            a0 = dropStartSeconds
            a1 = dropStartSeconds + dropDurationSeconds
            skip = dropExcludeId
        } else if (liveTrimTrack === trackIndex && liveTrimId !== "") {
            a0 = liveTrimStart
            a1 = liveTrimStart + liveTrimDuration
            skip = liveTrimId
        } else {
            return []
        }
        const out = []
        const clips = tracks[trackIndex].clips || []
        for (let i = 0; i < clips.length; ++i) {
            const c = clips[i]
            if (c.id === skip)
                continue
            const s = Math.max(a0, c.start)
            const e = Math.min(a1, c.start + c.duration)
            if (e - s >= 1 / 60)
                out.push({ "start": s, "end": e })
        }
        return out
    }

    function showLandingPreview(trackIndex, desiredStart, duration, excludeClipId) {
        const snapped = snapClipStart(desiredStart, duration, excludeClipId)
        dropExcludeId = excludeClipId || ""
        dropTrackIndex = trackIndex
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }

    // Drops only the landing outline. dropCreatesNewTrack belongs to the
    // panel-level drop area: a track row leaving the drag must not clear it,
    // since reaching the new-track slop always fires a leave on row 0.
    function clearLandingOutline() {
        dropTrackIndex = -1
        snapGuideSeconds = -1
    }

    function clearLandingPreview() {
        clearLandingOutline()
        dropCreatesNewTrack = false
    }

    function clearEffectDropHighlight() {
        effectDropTrackIndex = -1
        effectDropClipIndex = -1
    }

    function updateEffectDropHighlight(trackIndex, xPixels) {
        const clipIndex = clipIndexAtPosition(trackIndex, xPixels)
        effectDropTrackIndex = clipIndex >= 0 ? trackIndex : -1
        effectDropClipIndex = clipIndex
    }

    // A visual-only transition dropped on an audio track becomes the crossfade it can actually be.
    function transitionKindForTrack(trackIndex, kind) {
        const kinds = EditorState.transitionKindsForTrack(trackIndex)
        for (let i = 0; i < kinds.length; ++i) {
            if (kinds[i].kind === kind)
                return kind
        }
        return "crossfade"
    }

    // Find the outgoing (earlier) clip index for a transition drop at timeline x.
    function transitionLeftClipAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const track = tracks[trackIndex]
        if (track.type !== "video" && track.type !== "shape" && track.type !== "text"
                && track.type !== "audio")
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = track.clips
        let best = -1
        let bestDist = 1e9
        for (let i = 0; i < clips.length; i++) {
            const left = clips[i]
            for (let j = 0; j < clips.length; j++) {
                if (i === j)
                    continue
                const right = clips[j]
                if (right.start < left.start)
                    continue
                const leftEnd = left.start + left.duration
                const gap = right.start - leftEnd
                if (gap > 0.001)
                    continue
                let regionStart
                let regionEnd
                if (right.start < leftEnd) {
                    regionStart = right.start
                    regionEnd = leftEnd
                } else {
                    regionStart = leftEnd - 0.25
                    regionEnd = leftEnd + 0.25
                }
                if (seconds >= regionStart && seconds <= regionEnd) {
                    const mid = (regionStart + regionEnd) / 2
                    const dist = Math.abs(seconds - mid)
                    if (dist < bestDist) {
                        bestDist = dist
                        best = i
                    }
                }
            }
        }
        return best
    }

    function applyTransitionDrop(trackIndex, xPixels, kind) {
        const leftClip = transitionLeftClipAtPosition(trackIndex, xPixels)
        if (leftClip < 0 || !kind || kind.length === 0)
            return
        // VEDIT (조박사님, Vegas): a transition goes only where two clips overlap, for that long.
        EditorState.addTransitionOnOverlap(trackIndex, leftClip, transitionKindForTrack(trackIndex, kind))
    }

    // The bin row's media kind ("video", "audio", "image", ...), which is what the track-fit
    // questions below are really asking about. A file still in flight from the file manager has
    // no row, so it answers them with AssetLibrary.provisionalKindForUrl() instead.
    function assetKind(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        return asset ? (asset.kind || "video") : "video"
    }

    function assetDurationSeconds(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset)
            return 5.0
        if (asset.kind === "image" || !(asset.durationSeconds > 0))
            return 5.0
        // A bin-preview trim shortens what actually lands, so the landing preview matches it.
        return asset.placedDurationSeconds
    }

    // Snap a clip's desired start against timeline targets, testing both edges.
    // Returns {start, guide}; guide < 0 means no snap occurred.
    // `excludeClipId` is the clip being dragged, if any: it is still parked at its old start in
    // the model, so leaving its edges in the target set pins every short drag back to the origin.
    function snapClipStart(desiredStart, duration, excludeClipId) {
        const r = snapClipStartRaw(desiredStart, duration, excludeClipId)
        // VEDIT (조박사님: 비디오는 프레임 단위): a clip on a picture track moves by whole frames —
        // a snap target between two frames pulls it onto the nearer one. Sound moves freely.
        if (excludeClipId && clipTrackType(excludeClipId) === "video") {
            const fps = Math.max(1, EditorState.projectFps())
            const s = Math.round(r.start * fps) / fps
            return { "start": s, "guide": r.guide >= 0 ? r.guide + (s - r.start) : -1 }
        }
        return r
    }
    function clipTrackType(clipId) {
        for (let t = 0; t < root.tracks.length; ++t) {
            const clips = root.tracks[t].clips || []
            for (let i = 0; i < clips.length; ++i)
                if (clips[i].id === clipId)
                    return root.tracks[t].type
        }
        return ""
    }
    function snapClipStartRaw(desiredStart, duration, excludeClipId) {
        const ex = excludeClipId || ""
        const l = EditorState.snapTime(desiredStart, ex)
        const rEdge = EditorState.snapTime(desiredStart + duration, ex)
        const lSnapped = Math.abs(l - desiredStart) > 0.0005
        const rSnapped = Math.abs(rEdge - (desiredStart + duration)) > 0.0005
        if (lSnapped && (!rSnapped || Math.abs(l - desiredStart) <= Math.abs(rEdge - duration - desiredStart)))
            return { "start": l, "guide": l }
        if (rSnapped)
            return { "start": rEdge - duration, "guide": rEdge }
        return { "start": desiredStart, "guide": -1 }
    }

    // Actual row height: the per-type default, the lane's DAW-style vertical zoom, and room for
    // any nested adjustment lanes. Zero for a nested lane itself, which is drawn inside its
    // parent's row rather than getting one of its own.
    //
    // The rule lives in C++ so TimelinePanel, TrackHeaderColumn and AndroidTimeline cannot drift
    // apart — they used to hold three copies of it that had to agree or the headers slid out of
    // line with the rows.
    function trackHeight(index) {
        // Reading `tracks` is deliberate: EditorState.trackRowHeight is a plain call with no
        // binding dependency of its own, so without this a caller's binding would never
        // re-evaluate when a lane is added or a track's scale changes.
        const dep = tracks.length
        return EditorState.trackRowHeight(index, {
            "video": Theme.trackHeightVideo,
            "audio": Theme.trackHeightAudio,
            "text": Theme.trackHeightText,
            "subtitle": Theme.trackHeightSubtitle,
            "shape": Theme.trackHeightShape,
            "adjustment": Theme.trackHeightAdjustment,
            "lane": Theme.adjustmentLaneHeight
        })
    }

    // Track indices of the nested lanes belonging to `trackIndex`, topmost first. Derived from
    // `tracks` rather than asked of EditorState so it re-evaluates on its own.
    function adjustmentLanesFor(trackIndex) {
        var out = []
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return out
        // VEDIT: only the lanes drawn as strips — a lane of effects pinned to video clips shows
        // as "FX" in those clips' bands instead. `tracks` read above keeps this re-evaluating.
        return EditorState.adjustmentLanes(trackIndex)
    }

    // Nested lanes take no row of their own, so they must not contribute a gap either.
    function trackOccupiesARow(index) {
        return index >= 0 && index < tracks.length && !tracks[index].isAdjustmentLane
    }

    // Adjustment layers are tinted by what they act on, so a glance at a lane says whether it is
    // grading the picture, treating the audio, or cutting a mask.
    function adjustmentColor(kind) {
        if (kind === "audioEffects") return Theme.clipAdjustmentAudio
        if (kind === "mask") return Theme.clipAdjustmentMask
        return Theme.clipAdjustmentVideo
    }
    function clipColor(type) {
        if (type === "text") return Theme.clipText;
        if (type === "subtitle") return Theme.clipSubtitle;
        if (type === "audio") return Theme.clipAudio;
        if (type === "graphic") return Theme.clipGraphic;
        if (type === "effect" || type === "adjustment") return Theme.clipEffect;
        if (type === "composite") return Theme.clipComposite;
        return Theme.clipVideoPlaceholder; // video: no flat fill, thumbnails would go here
    }

    // Computed once per model change instead of at each of the ten call sites below — one of
    // which is the drag bounds of every clip delegate, making it O(clips x tracks) on any edit.
    readonly property real totalTracksHeightCached: (root.tracks, root.totalTracksHeight())

    function totalTracksHeight() {
        var h = 0;
        var rows = 0;
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue;
            h += trackHeight(i);
            if (rows > 0) h += Theme.trackGap;
            rows++;
        }
        return h;
    }

    // Finds the clip (if any) under a given x position (px) on a track, for
    // dropping an effect card directly onto a clip.
    function clipIndexAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = tracks[trackIndex].clips
        for (var i = 0; i < clips.length; i++) {
            if (seconds >= clips[i].start && seconds < clips[i].start + clips[i].duration)
                return i
        }
        return -1
    }

    // Empty stretches on a track, sorted left-to-right. Gaps are never stored — they're
    // just whatever time isn't covered by a clip — so this recomputes them from the
    // current clip list on every call rather than caching anything.
    function gapsForTrack(trackIndex) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return []
        const sorted = tracks[trackIndex].clips.slice().sort(function (a, b) {
            return a.start - b.start
        })
        const gaps = []
        for (var i = 0; i < sorted.length - 1; i++) {
            const gapStart = sorted[i].start + sorted[i].duration
            const gapEnd = sorted[i + 1].start
            if (gapEnd > gapStart)
                gaps.push({ start: gapStart, end: gapEnd })
        }
        return gaps
    }

    function timelineHasClips() {
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].clips.length > 0)
                return true
        }
        return false
    }

    function firstCompatibleTrackIndexForKind(mediaKind) {
        for (var i = 0; i < tracks.length; i++) {
            if (EditorState.trackAcceptsKind(i, mediaKind))
                return i
        }
        return -1
    }

    function firstCompatibleTrackIndex(assetIndex) {
        return firstCompatibleTrackIndexForKind(assetKind(assetIndex))
    }

    function trackIndexAtY(y) {
        var cursor = 0;
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue;
            const th = trackHeight(i);
            if (y >= cursor && y < cursor + th)
                return i;
            cursor += th + Theme.trackGap;
        }
        return -1;
    }

    // VEDIT: a clip dropped clear of every row gets a new track — above the top row, at the top;
    // below the last row, right after the last track of its own kind (a video clip dropped low
    // stays among the video tracks rather than landing under the audio).
    // VEDIT (조박사님): a clip dragged onto the seam between two tracks makes a new track there.
    // While its middle is within a few pixels of a seam, the seam lights up red; letting go
    // inserts a fresh track at that seam with the clip on it. -1: not on a seam.
    property int moveInsertIndex: -1
    function seamInsertIndexAtY(y) {
        var cursor = 0
        var first = -1
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            const h = trackHeight(i)
            const slop = Math.min(12, h / 4)
            // the seam above this row (between it and the previous one)
            const seam = first < 0 ? cursor : cursor - Theme.trackGap / 2
            if (first >= 0 && Math.abs(y - seam) <= slop)
                return i
            first = i
            cursor += h + Theme.trackGap
        }
        return -1
    }
    // VEDIT: whether a new track of `type` may open at `index` — pictures above every sound
    // track, sounds below every picture track (the same rule the app applies when it inserts).
    function seamAllowsType(index, type) {
        let firstSound = tracks.length, lastPicture = -1
        for (let i = 0; i < tracks.length; i++) {
            if (tracks[i].type === "audio")
                firstSound = Math.min(firstSound, i)
            else
                lastPicture = i
        }
        return type === "audio" ? index > lastPicture : index <= firstSound
    }
    function seamY(insertIndex) {
        return newTrackBoundaryY(insertIndex)
    }

    function newTrackIndexForMove(y, originTrack) {
        if (y < 0)
            return 0
        const type = tracks[originTrack] ? tracks[originTrack].type : ""
        var after = -1
        for (var i = 0; i < tracks.length; i++)
            if (tracks[i].type === type)
                after = i
        return after >= 0 ? after + 1 : tracks.length
    }

    // Where a dragged clip should land, from a y in track-column coordinates.
    // `track` is the row it fell on (-1 for empty space below every row); `lane` is the track
    // index of the nested lane inside that row, or -1 for the row's own clip area.
    function dropTargetAtY(y) {
        const track = trackIndexAtY(y)
        if (track < 0)
            return { "track": -1, "lane": -1 }
        return { "track": track, "lane": laneIndexAtY(track, y - trackOffsetY(track)) }
    }

    // Which nested lane of `trackIndex`, if any, a y inside that row falls in. Lanes stack as
    // strips across the top of the row; -1 means the clip area below them.
    function laneIndexAtY(trackIndex, yInRow) {
        const lanes = EditorState.adjustmentLanes(trackIndex)
        for (var i = 0; i < lanes.length; i++) {
            const top = i * Theme.adjustmentLaneHeight
            if (yInRow >= top && yInRow < top + Theme.adjustmentLaneHeight)
                return lanes[i]
        }
        return -1
    }

    // Depth of the "insert a new track here" band on a track row.
    function newTrackEdge(index) {
        return Math.min(Theme.newTrackHitSlop, trackHeight(index) / 4)
    }

    // Resolve a track-column y into a drop target, in the coordinates the track
    // rows actually live in (no reserved lane, so column y == track y).
    // Returns { newTrack: false, track: i } to land on an existing track, or
    // { newTrack: true, insertIndex: n } to create one at that index.
    //
    // Landing on the top/bottom edge of a row means "new track above/below it",
    // which is what makes a lane reachable underneath the last track — dropping
    // in the empty space below the tracks appends one.
    function assetDropTargetAtY(assetIndex, y) {
        return dropTargetAtYForKind(assetKind(assetIndex), y)
    }

    function dropTargetAtYForKind(mediaKind, y) {
        const count = tracks.length
        if (count === 0)
            return { "newTrack": true, "insertIndex": 0 }

        var cursor = 0
        for (var i = 0; i < count; i++) {
            if (!trackOccupiesARow(i))
                continue
            const h = trackHeight(i)
            const rowEnd = cursor + h
            // Claim the trailing gap too, so the 6px between rows resolves to a
            // boundary rather than to nothing.
            if (y < rowEnd + Theme.trackGap / 2) {
                if (!EditorState.trackAcceptsKind(i, mediaKind))
                    return { "newTrack": true,
                             "insertIndex": y < cursor + h / 2 ? i : i + 1 }
                const edge = newTrackEdge(i)
                // Only the topmost row needs a leading band: on every other row
                // that boundary is already the previous row's trailing band, and
                // claiming it twice just ate the middle of the track.
                if (i === 0 && y < cursor + edge)
                    return { "newTrack": true, "insertIndex": 0 }
                if (y >= rowEnd - edge)
                    return { "newTrack": true, "insertIndex": i + 1 }
                return { "newTrack": false, "track": i }
            }
            cursor = rowEnd + Theme.trackGap
        }
        return { "newTrack": true, "insertIndex": count }
    }

    // Y of the boundary a new track would be inserted at, in track-column
    // coordinates — where the insertion line is drawn.
    function newTrackBoundaryY(insertIndex) {
        var cursor = 0
        for (var i = 0; i < insertIndex && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return Math.max(0, cursor - Theme.trackGap / 2)
    }

    // The one place that decides where a dropped clip lands, so the preview during the drag and
    // the placement on release can never disagree — they used to hold two copies of this rule.
    // An empty project fills its existing track wherever you aim, rather than stacking a second
    // one on top of it.
    function resolveDropTarget(mediaKind, dropY) {
        if (!timelineHasClips()) {
            const firstIdx = firstCompatibleTrackIndexForKind(mediaKind)
            if (firstIdx >= 0)
                return { "newTrack": false, "track": firstIdx }
        }
        return dropTargetAtYForKind(mediaKind, dropY)
    }

    function updateDropPreviewForKind(mediaKind, duration, dropX, dropY) {
        const desired = Math.max(0, dropX / pxPerSecond)
        const target = resolveDropTarget(mediaKind, dropY)

        if (!target.newTrack) {
            dropCreatesNewTrack = false
            showLandingPreview(target.track, desired, duration)
            return
        }

        const snapped = snapClipStart(desired, duration)
        dropCreatesNewTrack = true
        dropNewTrackIndex = target.insertIndex
        dropTrackIndex = -1
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }

    function updateAssetDropPreview(assetIndex, dropX, dropY) {
        updateDropPreviewForKind(assetKind(assetIndex), assetDurationSeconds(assetIndex),
                                 dropX, dropY)
    }

    // Placement against an already-resolved target. Split out because the file-manager drop has
    // to resolve its target at drop time and place minutes later, once the import has probed:
    // dropY would be meaningless by then if the user scrolled the tracks in the meantime.
    // `onPlaced` runs once the clip is actually in the project, which is not necessarily before
    // this returns: the first clip of a pristine project opens the canvas setup dialog first, and
    // a multi-file drop cannot place its second clip until that has been answered. It never runs
    // if the dialog is dismissed, which is the existing meaning of dismissing it — nothing added.
    function placeAssetAtTarget(assetIndex, atSeconds, target, onPlaced) {
        if (assetIndex < 0 || !target)
            return

        function runAdd() {
            if (target.newTrack)
                EditorState.addClipFromAssetOnNewTrackAt(assetIndex, target.insertIndex, atSeconds)
            else
                EditorState.addClipFromAssetAt(assetIndex, target.track, atSeconds)
            if (onPlaced)
                onPlaced()
        }

        if (typeof Window !== "undefined" && Window.window && Window.window.configureAndAddAsset)
            Window.window.configureAndAddAsset(assetIndex, runAdd)
        else
            runAdd()
    }

    function performAssetDrop(assetIndex, dropX, dropY) {
        if (assetIndex < 0)
            return
        // VEDIT (조박사님): several picked items dropped together go in one after another from the
        // drop point, in the order they were shot.
        const group = EditorState.draggingAssetGroup()
        if (group.length > 1) {
            EditorState.setDraggingAssetGroup([])
            EditorState.addClipsFromAssetsAt(group, Math.max(0, dropX / pxPerSecond))
            return
        }
        placeAssetAtTarget(assetIndex, Math.max(0, dropX / pxPerSecond),
                           resolveDropTarget(assetKind(assetIndex), dropY))
    }

    // File-manager drops ---------------------------------------------------------------------
    //
    // A file dragged straight from the file manager onto a track is imported and placed in one
    // gesture. It still lands in the bin: a clip is addressed by asset index, so there is no such
    // thing as a clip whose media is outside the library — this is "import, then place", and the
    // assets panel shows the row either way. Dragging to the panel first is untouched.

    // Media kind of the file drag currently over the timeline, "" when there is none. The
    // new-track ghost sizes its lane from this, having no asset index to ask about yet.
    property string pendingDropKind: ""

    // Set from the drop until the import it started settles. The landing outline stays painted at
    // the frozen spot for that whole window — probing a large file takes seconds, and clearing
    // the outline on release made the drop look like it had been ignored.
    property bool importDropPending: false

    // Nothing has been opened at drag time, let alone probed, so the outline cannot promise a
    // real length. Same fallback assetDurationSeconds() uses for a row whose duration is unknown.
    readonly property real urlDropPreviewDuration: 5.0

    // What the drag lands as. A mixed selection is placed file by file, each on a track that
    // fits it, but the outline can only promise one spot — so it promises the first file's.
    function urlDropKind(urls) {
        if (!urls)
            return ""
        for (var i = 0; i < urls.length; ++i) {
            const kind = AssetLibrary.provisionalKindForUrl(urls[i])
            if (kind.length > 0)
                return kind
        }
        return ""
    }

    function updateUrlDropPreview(drop) {
        // A drag that cannot say what it carries yet — some platforms only hand the payload over
        // on release — is previewed as video rather than not previewed at all. The drop itself
        // re-reads the urls and places by what they really are.
        const kind = drop.hasUrls ? urlDropKind(drop.urls) : "video"
        pendingDropKind = kind
        if (kind.length === 0) {
            // Nothing in the drag is media. Promise nothing rather than a spot it cannot land on.
            clearLandingPreview()
            return
        }
        updateDropPreviewForKind(kind, urlDropPreviewDuration, drop.x, drop.y)
    }

    function clearUrlDropState() {
        pendingDropKind = ""
        importDropPending = false
    }

    function performUrlDrop(urls, dropX, dropY) {
        if (!urls || urls.length === 0) {
            clearUrlDropState()
            clearLandingPreview()
            return
        }

        const kind = urlDropKind(urls)
        if (kind.length === 0) {
            clearUrlDropState()
            clearLandingPreview()
            // Still handed to the import, so it reports why in the usual words instead of the
            // drop appearing to do nothing at all.
            MediaImport.importUrls(urls, true)
            return
        }

        // Frozen here rather than read back in the callback: the import is asynchronous, and the
        // user is free to scroll and zoom while it runs — by then dropY would name a different
        // track and dropX a different second.
        const atSeconds = Math.max(0, dropX / pxPerSecond)
        const target = resolveDropTarget(kind, dropY)

        root.importDropPending = true
        const started = MediaImport.importUrls(urls, true, function (added) {
            root.clearUrlDropState()
            root.clearLandingPreview()
            if (added <= 0)
                return
            // The idiom every caller of this callback uses: the rows it reports are the last
            // `added` in the library. MediaImport waits for their probes first, so their
            // durations and resolutions are real by now — which is what the clip lengths and the
            // canvas setup both need.
            root.placeImportedAssets(AssetLibrary.count - added, added, atSeconds, target)
        })

        if (!started) {
            root.clearUrlDropState()
            root.clearLandingPreview()
        }
    }

    // Where a file after the first one goes when the track the previous one landed on will not
    // take it — an audio file among videos, typically. The first track that fits, else a new one
    // at the bottom, which is where adding it from the bin would have put it.
    function targetForFollowOnKind(mediaKind) {
        const idx = firstCompatibleTrackIndexForKind(mediaKind)
        if (idx >= 0)
            return { "newTrack": false, "track": idx }
        return { "newTrack": true, "insertIndex": tracks.length }
    }

    // Places the rows a drop just imported: the first at the spot the drag promised, the rest
    // back to back after it. One at a time rather than a loop, because placing the first can
    // block on the canvas setup dialog and the rest have to queue behind that answer.
    function placeImportedAssets(firstIndex, count, atSeconds, target) {
        function placeAt(n, cursor, landing) {
            if (n >= count)
                return

            const assetIndex = firstIndex + n
            const kind = assetKind(assetIndex)
            // The promised spot belongs to the first file. Each one after it follows on the track
            // the previous one actually landed on, whenever that track will have it, so a batch
            // reads as one sequence instead of fanning out across new tracks.
            const fits = landing && !landing.newTrack
                         && EditorState.trackAcceptsKind(landing.track, kind)
            const here = fits ? landing : root.targetForFollowOnKind(kind)

            root.placeAssetAtTarget(assetIndex, cursor, here, function () {
                // Read back rather than advanced by the asset's duration: snapping and a
                // bin-preview trim both change where the clip really ends, and the next one has
                // to start there or it lands after a gap.
                const t = EditorState.selectedTrack
                const c = EditorState.selectedClip
                var next = cursor
                var landed = here
                if (t >= 0 && t < root.tracks.length) {
                    const clips = root.tracks[t].clips || []
                    if (c >= 0 && c < clips.length)
                        next = clips[c].start + clips[c].duration
                    landed = { "newTrack": false, "track": t }
                }
                placeAt(n + 1, next, landed)
            })
        }

        placeAt(0, atSeconds, target)
    }

    function handleTimelineWheel(wheel) {
        if (wheel.modifiers & Qt.ControlModifier) {
            // VEDIT (조박사님): zoom about the playhead, not the mouse — it stays where it is on
            // screen while the wheel turns, and never leaves the view (one already off screen is
            // brought back in at the edge).
            const anchorSeconds = Math.max(0, EditorState.playheadSeconds)
            const viewW = Math.max(1, flick.width)
            const viewportX = Math.max(0, Math.min(viewW, anchorSeconds * pxPerSecond - flick.contentX))
            const factor = wheel.angleDelta.y > 0 ? 1.15 : 1.0 / 1.15
            setZoomAround(zoom * factor, anchorSeconds, viewportX)
            return
        }

        const dy = wheel.angleDelta.y
        const dx = wheel.angleDelta.x
        const invert = EditorState.invertTimelineScroll

        // Invert (Kdenlive-style): wheel pans along time, Shift+wheel moves
        // between tracks. Trackpad horizontal motion always pans time.
        if (invert) {
            if (wheel.modifiers & Qt.ShiftModifier) {
                const delta = dy !== 0 ? dy : dx
                panTimelineBy(0, delta)
                return
            }
            if (dx !== 0)
                panTimelineBy(dx, 0)
            if (dy !== 0)
                panTimelineBy(dy, 0)
            return
        }

        // Shift forces horizontal scrolling regardless of overflow.
        if (wheel.modifiers & Qt.ShiftModifier) {
            const delta = dy !== 0 ? dy : dx
            panTimelineBy(delta, 0)
            return
        }

        // Trackpad horizontal component always scrolls horizontally.
        if (dx !== 0)
            panTimelineBy(dx, 0)

        // Vertical wheel scrolls the tracks when they overflow the viewport;
        // otherwise it falls back to horizontal so short timelines keep the
        // previous wheel-to-pan behaviour.
        if (dy !== 0) {
            const maxY = Math.max(0, flick.contentHeight - flick.height)
            if (maxY > 0)
                panTimelineBy(0, dy)
            else
                panTimelineBy(dy, 0)
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Math.round(seconds));
        const m = Math.floor(total / 60);
        const s = total % 60;
        return m.toString().padStart(2, "0") + ":" + s.toString().padStart(2, "0");
    }

    property int renameClipTrack: -1
    property int renameClipIndex: -1

    property int savePresetTrack: -1
    property int savePresetClip: -1

    function requestSaveEffectPreset(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        root.savePresetTrack = trackIndex
        root.savePresetClip = clipIndex
        const clips = root.tracks[trackIndex].clips || []
        const name = clipIndex < clips.length ? (clips[clipIndex].name || "") : ""
        effectPresetNameDialog.openWith(qsTr("Save effect preset"), name)
    }

    function requestRenameClip(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        if (trackIndex >= root.tracks.length)
            return
        const clips = root.tracks[trackIndex].clips || []
        if (clipIndex >= clips.length)
            return
        root.renameClipTrack = trackIndex
        root.renameClipIndex = clipIndex
        clipRenameField.text = clips[clipIndex].name || ""
        clipRenameDialog.open()
    }

    // Seeking while the engine clock is running lands ahead of the click: the
    // sink's processedUSecs is cumulative from play(), so the visible playhead
    // becomes clickTime + elapsed. Pause for the gesture and resume on release.
    function beginPlayheadSeek() {
        if (!EditorState.playing)
            return
        resumePlaybackAfterSeek = true
        EditorState.playing = false
    }

    function endPlayheadSeek() {
        endEdgeDrag() // VEDIT
        if (!resumePlaybackAfterSeek)
            return
        resumePlaybackAfterSeek = false
        EditorState.playing = true
    }

    // VEDIT (조박사님): dragging the playhead or a clip past the left edge (over the track
    // headers) or the right edge scrolls the timeline that way, faster the further out the
    // pointer is, and the dragged thing rides along under the pointer.
    property var edgeDragItem: null
    readonly property Item timelineFlick: flick
    property real edgePointerX: 0
    function noteEdgeDrag(item, viewportX) {
        edgeDragItem = item
        edgePointerX = viewportX
    }
    function endEdgeDrag() {
        edgeDragItem = null
    }
    Timer {
        interval: 16
        repeat: true
        running: root.edgeDragItem !== null
        onTriggered: {
            const vx = root.edgePointerX
            let step = 0
            if (vx < 0)
                step = -Math.min(48, 4 + (-vx) * 0.3)
            else if (vx > flick.width)
                step = Math.min(48, 4 + (vx - flick.width) * 0.3)
            if (step === 0)
                return
            const maxX = Math.max(0, flick.contentWidth - flick.width)
            const nx = Math.max(0, Math.min(maxX, flick.contentX + step))
            const d = nx - flick.contentX
            if (d === 0)
                return
            contentXAnimation.stop()
            flick.contentX = nx
            const it = root.edgeDragItem
            if (it)
                it.x = Math.max(0, it.x + d)
        }
    }

    function ensurePlayheadVisible() {
        const playheadX = EditorState.playheadSeconds * pxPerSecond;
        const margin = 64;
        var target = -1
        if (playheadX < flick.contentX + margin)
            target = Math.max(0, playheadX - margin)
        else if (playheadX > flick.contentX + flick.width - margin)
            target = Math.min(Math.max(0, flick.contentWidth - flick.width),
                              playheadX - flick.width + margin)
        if (target < 0)
            return
        // During play, assign contentX directly — restarting a NumberAnimation
        // every ~16ms on a multi-hour Flickable freezes the UI.
        if (EditorState.playing || flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        scrollToX(target)
    }

    // Smooth horizontal scroll helper. Skipped while the user is dragging the
    // view, so it never fights a flick in progress.
    function scrollToX(target) {
        if (flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        contentXAnimation.stop()
        contentXAnimation.from = flick.contentX
        contentXAnimation.to = target
        contentXAnimation.start()
    }

    NumberAnimation {
        id: contentXAnimation
        target: flick
        property: "contentX"
        duration: Theme.durationBase
        easing.type: Theme.easingInOut
    }

    function panTimelineBy(dx, dy) {
        contentXAnimation.stop()
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (dx)
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dx))
        if (dy)
            flick.contentY = Math.max(0, Math.min(maxY, flick.contentY - dy))
    }

    function beginTimelinePan(item, mouse) {
        const p = item.mapToItem(null, mouse.x, mouse.y)
        panLastSceneX = p.x
        panLastSceneY = p.y
        contentXAnimation.stop()
    }

    function updateTimelinePan(item, mouse) {
        const p = item.mapToItem(null, mouse.x, mouse.y)
        panTimelineBy(p.x - panLastSceneX, p.y - panLastSceneY)
        panLastSceneX = p.x
        panLastSceneY = p.y
    }

    Column {
        anchors.fill: parent

        // === toolbar =================================================================
        TimelineToolbar {
            id: toolbar
            width: parent.width
            panel: root
        }

        // === full-project overview strip ==============================================
        // Toggled from the toolbar's right end; collapses to nothing rather than merely
        // hiding, so the tracks get the height back.
        TimelineOverview {
            id: overviewStrip
            width: parent.width
            panel: root
            visible: EditorState.timelineOverviewVisible
            height: visible ? Theme.timelineOverviewHeight : 0
        }

        // === ruler + track labels + tracks ================================================
        Column {
            width: parent.width
            height: parent.height - toolbar.height - overviewStrip.height

            KeyframeGraph {
                id: keyframesBar
                width: parent.width
                userShown: root.keyframeLaneShown // VEDIT
                pxPerSecond: root.pxPerSecond
                labelsWidth: EditorState.trackLabelsWidth
                propertiesTab: root.propertiesTab
                // Keep keys/playhead lined up with the track scroll view below.
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            SubtitleCueLane {
                id: subtitleLane
                width: parent.width
                pxPerSecond: root.pxPerSecond
                labelsWidth: EditorState.trackLabelsWidth
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            Row {
                width: parent.width
                height: parent.height - keyframesBar.height - subtitleLane.height

            // --- fixed left label column --------------------------------------------
            Column {
                id: labelsColumn
                width: EditorState.trackLabelsWidth
                height: parent.height

                // CapCut-style: add-track sits at the timeline origin, above the
                // track headers, not buried in the top toolbar.
                Item {
                    width: parent.width
                    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                    IconButton {
                        id: addTrackButton
                        // VEDIT: moved left to make room for the 대·중·소 clip-size switch.
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.plus
                        variant: "text"
                        tooltip: qsTr("Add new track")
                        onClicked: addTrackMenu.open()

                        NewTrackMenu {
                            id: addTrackMenu
                            x: Math.max(0, (addTrackButton.width - width) / 2)
                            y: addTrackButton.height + 4
                        }
                    }

                    // VEDIT: 클립 크기 — 소·중·대 for every row at once.
                    Row {
                        id: clipSizeSwitch
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Repeater {
                            model: [{ "label": qsTr("S", "clip size small"), "size": 0 },
                                    { "label": qsTr("M", "clip size medium"), "size": 1 },
                                    { "label": qsTr("L", "clip size large"), "size": 2 }]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: EditorState.timelineClipSize === modelData.size
                                // VEDIT: no box (조박사님) — just the bar, 20% smaller; the chosen size
                                // is the yellow one. The transparent cell keeps a usable hit area.
                                width: 21
                                height: 24
                                color: "transparent"
                                // A low / middle / tall bar stands for the row height (조박사님).
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 11
                                    height: [5, 9, 14][modelData.size]
                                    radius: 2
                                    color: parent.active ? Theme.primary
                                                         : (sizeMouse.containsMouse ? Theme.foreground
                                                                                    : Theme.mutedForeground)
                                }
                                MouseArea {
                                    id: sizeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // VEDIT (조박사님): every row alike — rows sized one by one go back.
                                    onClicked: {
                                        EditorState.resetTrackHeightScales()
                                        EditorState.timelineClipSize = modelData.size
                                    }
                                }
                                ToolTip.visible: sizeMouse.containsMouse
                                ToolTip.delay: 500
                                ToolTip.text: qsTr("Clip size") + " — " + modelData.label
                            }
                        }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: 1
                        height: parent.height
                        color: Theme.panelBorder
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.panelBorder
                    }
                }

                TrackHeaderColumn {
                    id: trackLabelsArea
                    width: parent.width
                    // Clamped: went negative at small panel heights, which spilled
                    // the absolutely-positioned label rows out of the column.
                    height: Math.max(0, parent.height - Theme.timelineRulerHeight
                                        - Theme.timelineBookmarkRowHeight)
                    labelsWidth: EditorState.trackLabelsWidth
                    resizable: true
                    tracks: root.tracks
                    contentY: flick.contentY
                }
            }

            // --- scrollable ruler + tracks --------------------------------------------
            Flickable {
                id: flick
                width: parent.width - EditorState.trackLabelsWidth
                height: parent.height

                // VEDIT: Ctrl + mouse wheel zooms anywhere on the timeline. The content-space
                // MouseAreas only see the wheel over empty space — over a clip or a track row the
                // event bubbles to this Flickable, which scrolled instead. A handler on the
                // Flickable gets it before the Flickable's own wheel scrolling does.
                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    target: null
                    onWheel: (event) => {
                        const contentX = flick.contentX + point.position.x
                        root.handleTimelineWheel({
                            modifiers: Qt.ControlModifier,
                            angleDelta: event.angleDelta,
                            x: contentX
                        })
                        event.accepted = true
                    }
                }

                // Height of the pinned ruler + bookmark strip at the top.
                readonly property real headerHeight: Theme.timelineRulerHeight
                                                     + Theme.timelineBookmarkRowHeight

                contentWidth: Math.max(width,
                    EditorState.durationSeconds * root.pxPerSecond + root.timelineEndPadPx)
                // Was `height`, so once the tracks were taller than the panel the
                // lower ones were silently truncated and could not be reached at
                // all. totalTracksHeight() was already computed but never used.
                contentHeight: Math.max(height,
                                        headerHeight + root.totalTracksHeightCached
                                        + Theme.trackGap)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AlwaysOn }
                ScrollBar.vertical: AppScrollBar { }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    // Wheel + middle-click pan: scoped to the ruler so it does not
                    // block timeline drops. Left/right clicks fall through.
                    MouseArea {
                        id: rulerWheelArea
                        width: parent.width
                        y: flick.contentY
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        z: 1
                        acceptedButtons: Qt.MiddleButton
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                        onPressed: (mouse) => root.beginTimelinePan(rulerWheelArea, mouse)
                        onPositionChanged: (mouse) => {
                            if (pressed)
                                root.updateTimelinePan(rulerWheelArea, mouse)
                        }
                    }

                    // Horizontal scroll / zoom wheel over track rows (below the drop overlay).
                    MouseArea {
                        id: tracksWheelArea
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeightCached, Theme.trackHeightVideo)
                        z: 150
                        acceptedButtons: Qt.MiddleButton
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                        onPressed: (mouse) => root.beginTimelinePan(tracksWheelArea, mouse)
                        onPositionChanged: (mouse) => {
                            if (pressed)
                                root.updateTimelinePan(tracksWheelArea, mouse)
                        }
                    }

                    // seek strip (ruler + bookmark lane) ------------------------------------
                    // CapCut/Premiere-style: the whole pinned header is one scrub area so
                    // the playhead is easy to move without hunting for a tiny hit target.
                    Item {
                        id: seekStrip
                        width: parent.width
                        y: flick.contentY
                        z: 2
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.panelBackground
                        }

                        // Subtle bottom edge so the seek strip reads as a distinct bar.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.panelBorder
                        }

                        MouseArea {
                            id: rulerScrub
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            preventStealing: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton // VEDIT: right = time format menu
                            // Leave room for the playhead head so its drag can start first.
                            z: 1

                            function scrubTo(x) {
                                EditorState.playheadSeconds =
                                    EditorState.snapTime(Math.max(0, x) / root.pxPerSecond)
                            }

                            // Seeking is not a selection change: scrubbing to look at a
                            // different point used to drop the clip you were editing.
                            onPressed: (mouse) => {
                                if (mouse.button === Qt.RightButton) { // VEDIT
                                    rulerFormatMenu.popup()
                                    return
                                }
                                root.forceActiveFocus()
                                root.beginPlayheadSeek()
                                scrubTo(mouse.x)
                            }
                            onPositionChanged: (mouse) => {
                                if (pressed && (pressedButtons & Qt.LeftButton))
                                    scrubTo(mouse.x)
                            }
                            onReleased: (mouse) => {
                                if (mouse.button === Qt.LeftButton)
                                    root.endPlayheadSeek()
                            }
                            onCanceled: root.endPlayheadSeek()

                            // VEDIT: how the ruler counts under a second.
                            ThemedContextMenu {
                                id: rulerFormatMenu
                                Repeater {
                                    model: [{ fps: 30, label: qsTr("Frames (30 per second)") },
                                            { fps: 24, label: qsTr("Frames (24 per second, film)") },
                                            { fps: 25, label: qsTr("Frames (25 per second)") },
                                            { fps: 60, label: qsTr("Frames (60 per second)") },
                                            { fps: 0, label: qsTr("Hundredths of a second") }]
                                    delegate: ThemedMenuItem {
                                        required property var modelData
                                        text: (root.rulerFps === modelData.fps ? "✓  " : "     ") + modelData.label
                                        onTriggered: root.setRulerFps(modelData.fps)
                                    }
                                }
                            }
                            // MouseArea would otherwise swallow wheel and block Ctrl-zoom.
                            onWheel: (wheel) => root.handleTimelineWheel(wheel)

                            ThemedToolTip {
                                text: qsTr("Click or drag to seek")
                                visible: rulerScrub.containsMouse && !rulerScrub.pressed
                                         && !playheadDragArea.drag.active
                            }
                        }

                        // Time ticks live in the upper half of the seek strip.
                        // Only instantiate ticks that intersect the viewport — a 2h
                        // timeline at 1× zoom would otherwise create thousands of Items.
                        Item {
                            id: ruler
                            width: parent.width
                            height: Theme.timelineRulerHeight
                            z: 0

                            readonly property real tickStepPx: root.tickStepSeconds * root.pxPerSecond
                            readonly property int tickIndexMax: Math.max(0,
                                Math.ceil(flick.contentWidth / Math.max(1, tickStepPx)))
                            readonly property int firstVisibleTick: Math.max(0,
                                Math.floor(flick.contentX / Math.max(1, tickStepPx)) - 1)
                            readonly property int visibleTickCount: Math.min(
                                tickIndexMax - firstVisibleTick + 1,
                                Math.ceil(flick.width / Math.max(1, tickStepPx)) + 3)

                            // VEDIT (조박사님): 임시 렌더링 band along the ruler's foot — green where a
                            // stretch is rendered and plays from the file, orange where it is still
                            // waiting or rendering.
                            Repeater {
                                model: EditorState.renderCacheSegments
                                delegate: Rectangle {
                                    required property var modelData
                                    x: modelData.start * root.pxPerSecond
                                    width: Math.max(1, (modelData.end - modelData.start) * root.pxPerSecond)
                                    y: ruler.height - height
                                    height: 3
                                    color: modelData.ready ? "#43a047" : "#fb8c00"
                                    z: 1
                                }
                            }

                            Repeater {
                                model: Math.max(0, ruler.visibleTickCount)
                                delegate: Item {
                                    readonly property real tickSeconds:
                                        (ruler.firstVisibleTick + index) * root.tickStepSeconds
                                    x: tickSeconds * root.pxPerSecond
                                    width: root.tickStepSeconds * root.pxPerSecond
                                    height: ruler.height

                                    Rectangle {
                                        x: 0
                                        y: parent.height - 10
                                        width: 1
                                        height: 8
                                        color: Qt.rgba(Theme.mutedForeground.r,
                                                       Theme.mutedForeground.g,
                                                       Theme.mutedForeground.b, 0.28)
                                    }

                                    Text {
                                        x: 4
                                        y: 4
                                        text: root.formatTick(parent.tickSeconds)
                                        color: Theme.mutedForeground
                                        font.pixelSize: Theme.fontSizeTick
                                        font.family: Theme.fontFamily
                                    }
                                }
                            }
                        }

                        // VEDIT: beat marks under the ruler (after 박자에 맞추기), only the ones
                        // in view — a long song has thousands.
                        Item {
                            id: beatMarks
                            // The beat list is read once per analysis, not once per scroll pixel.
                            property var beats: []
                            property real windowFrom: -1
                            property real windowTo: -1
                            property var visibleBeats: []
                            Connections {
                                target: EditorState
                                function onBeatAnalysisChanged() { beatMarks.reload() }
                            }
                            Component.onCompleted: reload()
                            function reload() {
                                const a = EditorState.beatAnalysis
                                beats = (a && a.beats) ? a.beats : []
                                windowFrom = windowTo = -1
                                refresh()
                            }
                            function lowerBound(t) {
                                let lo = 0, hi = beats.length
                                while (lo < hi) {
                                    const mid = (lo + hi) >> 1
                                    if (beats[mid] < t) lo = mid + 1
                                    else hi = mid
                                }
                                return lo
                            }
                            // Rebuild only when the view leaves the padded window it was built for.
                            function refresh() {
                                if (!EditorState.beatGridVisible || beats.length === 0) {
                                    visibleBeats = []
                                    return
                                }
                                const from = flick.contentX / root.pxPerSecond
                                const to = (flick.contentX + flick.width) / root.pxPerSecond
                                if (from >= windowFrom && to <= windowTo)
                                    return
                                const pad = (to - from) * 0.5
                                windowFrom = from - pad
                                windowTo = to + pad
                                const out = []
                                for (let i = lowerBound(windowFrom); i < beats.length && beats[i] <= windowTo && out.length < 800; ++i)
                                    out.push(beats[i])
                                visibleBeats = out
                            }
                            Connections {
                                target: flick
                                function onContentXChanged() { beatMarks.refresh() }
                                function onWidthChanged() { beatMarks.windowFrom = -1; beatMarks.refresh() }
                            }
                            Connections {
                                target: root
                                function onPxPerSecondChanged() { beatMarks.windowFrom = -1; beatMarks.refresh() }
                            }
                        }
                        Repeater {
                            model: beatMarks.visibleBeats
                            delegate: Rectangle {
                                required property var modelData
                                x: modelData * root.pxPerSecond - 2
                                y: Theme.timelineRulerHeight - 6
                                width: 4
                                height: 4
                                radius: 2
                                color: Theme.primary
                                z: 1
                            }
                        }

                        Item {
                            id: bookmarkRow
                            y: Theme.timelineRulerHeight
                            width: parent.width
                            height: Theme.timelineBookmarkRowHeight
                            z: 2

                            property int renameIndex: -1

                            Repeater {
                                model: EditorState.bookmarks
                                delegate: Item {
                                    id: bookmarkDelegate
                                    x: (bookmarkMouse.dragging
                                        ? bookmarkMouse.dragSeconds
                                        : modelData.seconds) * root.pxPerSecond - width / 2
                                    width: 10
                                    height: parent.height
                                    z: bookmarkMouse.containsMouse || bookmarkMouse.dragging ? 4 : 3

                                    // Stem into the ruler so markers read as jump flags, not dots.
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: 0
                                        width: 1
                                        height: parent.height + 6
                                        color: Theme.primary
                                        opacity: 0.7
                                    }

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8
                                        height: 8
                                        rotation: 45
                                        radius: 1
                                        color: Theme.primary
                                        border.width: bookmarkMouse.containsMouse ? Theme.borderWidth : 0
                                        border.color: Theme.primaryForeground
                                    }

                                    ThemedToolTip {
                                        visible: bookmarkMouse.containsMouse && !bookmarkMouse.dragging
                                        text: modelData.label + " @ "
                                              + root.formatTime(modelData.seconds)
                                    }

                                    MouseArea {
                                        id: bookmarkMouse
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        hoverEnabled: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        // Stop the seek strip from eating bookmark clicks / drags.
                                        preventStealing: true

                                        property bool dragging: false
                                        property bool didDrag: false
                                        property real dragSeconds: modelData.seconds
                                        property real pressX: 0

                                        onPressed: (mouse) => {
                                            if (mouse.button !== Qt.LeftButton)
                                                return
                                            didDrag = false
                                            dragging = false
                                            pressX = mouse.x
                                            dragSeconds = modelData.seconds
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!pressed || !(mouse.buttons & Qt.LeftButton))
                                                return
                                            if (!didDrag && Math.abs(mouse.x - pressX) < 3)
                                                return
                                            didDrag = true
                                            dragging = true
                                            const globalX = mapToItem(bookmarkRow, mouse.x, 0).x
                                            dragSeconds = EditorState.snapTime(
                                                Math.max(0, globalX / root.pxPerSecond))
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && didDrag) {
                                                EditorState.updateBookmark(
                                                    index, dragSeconds, modelData.label)
                                            }
                                            dragging = false
                                            // Keep didDrag through onClicked (fires after release).
                                            Qt.callLater(function() { didDrag = false })
                                        }
                                        onClicked: (mouse) => {
                                            if (mouse.button === Qt.RightButton) {
                                                bookmarkContextMenu.bookmarkIndex = index
                                                bookmarkContextMenu.bookmarkLabel = modelData.label
                                                bookmarkContextMenu.bookmarkSeconds = modelData.seconds
                                                bookmarkContextMenu.popup()
                                                return
                                            }
                                            if (!didDrag)
                                                EditorState.goToBookmark(index)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button !== Qt.LeftButton)
                                                return
                                            bookmarkRow.renameIndex = index
                                            bookmarkRenameField.text = modelData.label
                                            bookmarkRenameDialog.open()
                                        }
                                    }
                                }
                            }

                            ThemedContextMenu {
                                id: bookmarkContextMenu
                                property int bookmarkIndex: -1
                                property string bookmarkLabel: ""
                                property real bookmarkSeconds: 0

                                ThemedMenuItem {
                                    text: qsTr("Go to bookmark")
                                    icon.name: Theme.icons.bookmark
                                    onTriggered: EditorState.goToBookmark(bookmarkContextMenu.bookmarkIndex)
                                }
                                ThemedMenuItem {
                                    text: qsTr("Rename…")
                                    onTriggered: {
                                        bookmarkRow.renameIndex = bookmarkContextMenu.bookmarkIndex
                                        bookmarkRenameField.text = bookmarkContextMenu.bookmarkLabel
                                        bookmarkRenameDialog.open()
                                    }
                                }
                                ThemedMenuSeparator { }
                                ThemedMenuItem {
                                    text: qsTr("Delete")
                                    icon.name: Theme.icons.trash
                                    onTriggered: EditorState.removeBookmark(bookmarkContextMenu.bookmarkIndex)
                                }
                            }
                        }

                        // Work area highlight in the bookmark lane (between In/Out markers).
                        Rectangle {
                            visible: EditorState.workAreaActive
                            y: Theme.timelineRulerHeight
                            height: Theme.timelineBookmarkRowHeight
                            x: EditorState.workAreaInSeconds * root.pxPerSecond
                            width: Math.max(0, (EditorState.workAreaOutSeconds - EditorState.workAreaInSeconds)
                                             * root.pxPerSecond)
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                            z: 1
                        }
                    }

                    // Work area overlay across the track rows.
                    Item {
                        id: workAreaOverlay
                        z: 1
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeightCached + Theme.trackGap,
                                         flick.height - flick.headerHeight)

                        readonly property real inX: EditorState.workAreaInSeconds >= 0
                                                   ? EditorState.workAreaInSeconds * root.pxPerSecond
                                                   : -1
                        readonly property real outX: EditorState.workAreaOutSeconds >= 0
                                                    ? EditorState.workAreaOutSeconds * root.pxPerSecond
                                                    : -1

                        Rectangle {
                            visible: EditorState.workAreaActive
                            x: parent.inX
                            width: Math.max(0, parent.outX - parent.inX)
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
                        }

                        Rectangle {
                            visible: EditorState.workAreaInSeconds >= 0
                            x: parent.inX
                            width: 2
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Theme.primary
                            opacity: 0.85
                        }

                        Rectangle {
                            visible: EditorState.workAreaOutSeconds >= 0
                            x: parent.outX - width
                            width: 2
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Theme.primary
                            opacity: 0.85
                        }
                    }

                    // A project with no tracks used to render as a completely
                    // blank timeline, with the only route to a first track being
                    // one button among sixteen in the toolbar.
                    EmptyState {
                        x: flick.contentX + (flick.width - width) / 2
                        y: flick.contentY + flick.headerHeight
                           + Math.max(0, (flick.height - flick.headerHeight - height) / 2)
                        width: Math.min(flick.width - Theme.spacing3xl, 320)
                        visible: root.tracks.length === 0
                        z: 4
                        glyph: Theme.icons.layers
                        title: qsTr("Your timeline is empty")
                        hint: qsTr("Drag media here from the library, or add an empty track to start.")
                        actionText: qsTr("New track")
                        actionVariant: "primary"
                        onActionTriggered: newTrackMenuFromEmpty.open()

                        NewTrackMenu {
                            id: newTrackMenuFromEmpty
                            y: parent.height
                        }
                    }

                    // track rows ---------------------------------------------------------
                    // CapCut marquee: drag empty track space to box-select.
                    // Sits under the track column so clips still receive presses;
                    // empty gaps fall through to this grabber.
                    MouseArea {
                        id: marqueeArea
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeightCached + Theme.trackGap,
                                         flick.height - flick.headerHeight)
                        z: 0
                        enabled: root.timelineTool === ""
                        acceptedButtons: Qt.LeftButton | Qt.RightButton // VEDIT: right = track menu
                        // CapCut: empty-space drag is marquee, not flick-scroll.
                        preventStealing: true
                        cursorShape: pressed ? Qt.CrossCursor : Qt.ArrowCursor

                        onPressed: (mouse) => {
                            // VEDIT (조박사님): right-click on a track's empty space → new track menu.
                            if (mouse.button === Qt.RightButton) {
                                emptyTrackMenu.trackIndex = root.trackIndexAtY(mouse.y)
                                emptyTrackMenu.popup()
                                return
                            }
                            root.forceActiveFocus()
                            root.marqueeAdditive = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                            root.marqueeOriginX = mouse.x
                            root.marqueeOriginY = mouse.y
                            root.marqueeCurrentX = mouse.x
                            root.marqueeCurrentY = mouse.y
                            root.marqueeActive = true
                            // VEDIT: a paste lands on the track last clicked.
                            EditorState.noteTrackClicked(root.trackIndexAtY(mouse.y))
                            if (!root.marqueeAdditive)
                                EditorState.clearSelection()
                        }
                        // VEDIT: the menu a right-click on empty track space opens.
                        ThemedContextMenu {
                            id: emptyTrackMenu
                            property int trackIndex: -1
                            ThemedMenuItem {
                                visible: emptyTrackMenu.trackIndex >= 0
                                text: qsTr("New track here")
                                icon.name: Theme.icons.layers
                                onTriggered: EditorState.addTrackNear(emptyTrackMenu.trackIndex)
                            }
                            ThemedMenuItem { // VEDIT (조박사님)
                                visible: emptyTrackMenu.trackIndex >= 0
                                text: qsTr("Delete this track")
                                icon.name: Theme.icons.trash
                                onTriggered: EditorState.removeTrack(emptyTrackMenu.trackIndex)
                            }
                            ThemedMenuSeparator { visible: emptyTrackMenu.trackIndex >= 0 }
                            ThemedMenuItem {
                                text: qsTr("New video track")
                                onTriggered: EditorState.addTrack("video")
                            }
                            ThemedMenuItem {
                                text: qsTr("New text track")
                                onTriggered: EditorState.addTrack("text")
                            }
                            ThemedMenuItem {
                                text: qsTr("New audio track")
                                onTriggered: EditorState.addTrack("audio")
                            }
                        }
                        onPositionChanged: (mouse) => {
                            if (!root.marqueeActive)
                                return
                            root.marqueeCurrentX = Math.max(0, mouse.x)
                            root.marqueeCurrentY = Math.max(0, mouse.y)
                            if (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                                root.applyMarqueeSelection()
                        }
                        onReleased: root.endMarquee(true)
                        onCanceled: root.endMarquee(false)
                    }

                    Column {
                        id: trackColumn
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        spacing: Theme.trackGap
                        z: 1

                        Repeater {
                            model: root.tracks.length
                            delegate: Rectangle {
                                id: trackRow
                                property int trackIndex: index
                                // Drawn inside its parent's row instead of getting one here.
                                // Column skips invisible children entirely, so this costs no
                                // spacing either.
                                visible: !root.tracks[trackIndex].isAdjustmentLane
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                // VEDIT (조박사님): a hidden track shows at half strength
                                opacity: root.tracks[trackIndex].hidden === true ? 0.5 : 1.0
                                // Faint row tint on hover, and an empty track now
                                // reads as a track rather than as blank space.
                                color: trackHover.hovered
                                       ? Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.5)
                                       : Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.22)

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                HoverHandler { id: trackHover }

                                DropArea {
                                    anchors.fill: parent
                                    keys: ["text/plain", "application/x-drift-effect",
                                           "application/x-drift-audio-effect",
                                           "application/x-drift-shape", "application/x-drift-transition",
                                           "application/x-drift-mask"]

                                    function isEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-effect") !== -1
                                    }

                                    function isMaskDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-mask") !== -1
                                    }

                                    // A mask lane only ever masks the track it is nested in, so
                                    // audio (no picture) and adjustment rows (a lane inside a lane
                                    // would have two scopes) take neither drop.
                                    function acceptsMask(trackIndex) {
                                        const track = root.tracks[trackIndex]
                                        return !!track && track.type !== "audio"
                                               && !track.isAdjustmentLane
                                    }

                                    function isAudioEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-audio-effect") !== -1
                                    }

                                    function isShapeDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-shape") !== -1
                                    }

                                    function isTransitionDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-transition") !== -1
                                    }

                                    function assetIndexFromDrop(drop) {
                                        if (EditorState.draggingAssetIndex >= 0)
                                            return EditorState.draggingAssetIndex
                                        const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                                        const idx = parseInt(text)
                                        return isNaN(idx) ? -1 : idx
                                    }

                                    function updateAssetPreview(drop) {
                                        if (isTransitionDrag(drop)) {
                                            root.clearLandingPreview()
                                            root.clearEffectDropHighlight()
                                            return
                                        }
                                        if (isMaskDrag(drop)) {
                                            if (!acceptsMask(trackRow.trackIndex)) {
                                                root.clearLandingOutline()
                                                root.clearEffectDropHighlight()
                                                return
                                            }
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            // Over a gap the mask lands as its own lane clip, so
                                            // promise the span the same way a media drop does.
                                            if (root.clipIndexAtPosition(trackRow.trackIndex, drop.x) < 0) {
                                                const at = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, at, 5.0)
                                            } else {
                                                root.clearLandingOutline()
                                            }
                                            return
                                        }
                                        if (isEffectDrag(drop) || isAudioEffectDrag(drop)) {
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            if (isEffectDrag(drop) && root.tracks[trackRow.trackIndex].type === "video"
                                                    && root.clipIndexAtPosition(trackRow.trackIndex, drop.x) < 0) {
                                                const desired = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, desired, 5.0)
                                            } else {
                                                root.clearLandingOutline()
                                            }
                                            return
                                        }
                                        root.clearEffectDropHighlight()
                                        if (isShapeDrag(drop)) {
                                            if (root.tracks[trackRow.trackIndex].type === "shape") {
                                                const desired = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, desired, 5.0)
                                            } else {
                                                root.clearLandingPreview()
                                            }
                                            return
                                        }
                                        // Media fallback when the panel overlay does not
                                        // receive the drag (seen on some Wayland compositors).
                                        // drop.y is row-local, so lift it into track-column
                                        // coordinates and reuse the same resolution the
                                        // overlay uses — otherwise the two disagree about
                                        // where the drop would land.
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        root.updateAssetDropPreview(
                                            assetIndex, drop.x,
                                            root.trackOffsetY(trackRow.trackIndex) + drop.y)
                                    }

                                    onEntered: (drop) => updateAssetPreview(drop)
                                    onPositionChanged: (drop) => updateAssetPreview(drop)
                                    onExited: {
                                        root.clearLandingOutline()
                                        root.clearEffectDropHighlight()
                                    }
                                    onDropped: (drop) => {
                                        drop.accept(Qt.CopyAction)
                                        if (isTransitionDrag(drop)) {
                                            const kind = drop.getDataAsString("application/x-drift-transition")
                                            root.applyTransitionDrop(trackRow.trackIndex, drop.x, kind)
                                            return
                                        }
                                        if (isMaskDrag(drop)) {
                                            const maskId = drop.getDataAsString("application/x-drift-mask")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            root.clearLandingOutline()
                                            if (maskId.length === 0 || !acceptsMask(trackRow.trackIndex))
                                                return
                                            // No selectClip here, unlike the effect branch: both
                                            // calls select the mask clip they minted, which is
                                            // what opens its inspector and preview handles.
                                            if (clipIndex >= 0) {
                                                EditorState.addMaskToClip(trackRow.trackIndex, clipIndex, maskId)
                                            } else {
                                                const atSec = Math.max(0, drop.x / root.pxPerSecond)
                                                EditorState.addMaskLaneClip(trackRow.trackIndex, maskId, atSec)
                                            }
                                            return
                                        }
                                        if (isEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            root.clearLandingOutline()
                                            // No selectClip: addEffect selects the adjustment it
                                            // put the stack on, which is where the Effects tab is.
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addEffect(trackRow.trackIndex, clipIndex, effectId)
                                            } else if (effectId.length > 0 && root.tracks[trackRow.trackIndex].type === "video") {
                                                const atSec = Math.max(0, drop.x / root.pxPerSecond)
                                                EditorState.addAdjustmentClipWithEffect(effectId, trackRow.trackIndex, atSec)
                                            }
                                            return
                                        }
                                        if (isAudioEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-audio-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0)
                                                EditorState.addAudioEffect(trackRow.trackIndex, clipIndex, effectId)
                                            return
                                        }
                                        if (isShapeDrag(drop)) {
                                            const shapeId = drop.getDataAsString("application/x-drift-shape")
                                            const atSeconds = Math.max(0, drop.x / root.pxPerSecond)
                                            root.clearLandingPreview()
                                            EditorState.addShapeClipAt(shapeId, trackRow.trackIndex, atSeconds)
                                            return
                                        }
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        root.clearLandingPreview()
                                        // Reconstruct track-column Y so performAssetDrop
                                        // resolves the same target this row saw.
                                        const dropY = root.trackOffsetY(trackRow.trackIndex)
                                                    + drop.y
                                        root.performAssetDrop(assetIndex, drop.x, dropY)
                                    }
                                }

                                // VEDIT: the opening a clip will be inserted into (끼워 넣기).
                                Rectangle {
                                    visible: root.insertTrack === trackRow.trackIndex
                                    x: root.insertSeamSec * root.pxPerSecond
                                    width: root.insertDurSec * root.pxPerSecond
                                    height: parent.height
                                    color: Qt.rgba(0.9, 0.15, 0.15, 0.30)
                                    border.width: 2
                                    border.color: "#e53935"
                                    z: 6
                                }

                                // Landing preview outline (library drop or clip move).
                                // Hidden while creating a new track — that state uses the
                                // reserved lane above, not a ghost on an existing row.
                                Rectangle {
                                    visible: root.dropTrackIndex === trackRow.trackIndex
                                             && !root.dropCreatesNewTrack
                                    x: root.dropStartSeconds * root.pxPerSecond
                                    width: root.dropDurationSeconds * root.pxPerSecond
                                    height: parent.height
                                    radius: Theme.radiusSm
                                    color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                                    border.width: 2
                                    border.color: Theme.primary
                                    z: 5

                                    // The outline is held on screen after a file drop for as
                                    // long as the import it started is still probing, so it has
                                    // to say why it is sitting there empty.
                                    Text {
                                        anchors.centerIn: parent
                                        visible: root.importDropPending
                                        text: qsTr("Importing…")
                                        color: Theme.primary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeTiny
                                        font.weight: Font.Medium

                                        SequentialAnimation on opacity {
                                            running: root.importDropPending
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.45; duration: Theme.durationSlow; easing.type: Theme.easing }
                                            NumberAnimation { to: 1.0; duration: Theme.durationSlow; easing.type: Theme.easing }
                                        }
                                    }
                                }

                                // Nested adjustment lanes, drawn as strips across the top of
                                // this row. A lane is a track in its own right — that is what
                                // keeps (trackIndex, clipIndex) addressing flat everywhere else —
                                // but it has no row of its own, so it is rendered in here.
                                Column {
                                    id: adjustmentLaneStrips
                                    width: trackRow.width
                                    spacing: 0
                                    z: 4

                                    Repeater {
                                        model: root.adjustmentLanesFor(trackRow.trackIndex)
                                        delegate: Item {
                                            id: laneStrip
                                            required property var modelData
                                            // The lane's own track index, under the name
                                            // TimelineClipItem looks for on its `trackRow` —
                                            // which is this Item, not the row above.
                                            property int trackIndex: modelData
                                            width: trackRow.width
                                            height: Theme.adjustmentLaneHeight

                                            Rectangle {
                                                anchors.fill: parent
                                                color: Qt.rgba(Theme.clipEffect.r, Theme.clipEffect.g,
                                                               Theme.clipEffect.b, 0.12)
                                                Rectangle {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    height: 1
                                                    color: Qt.rgba(0, 0, 0, 0.25)
                                                }
                                            }

                                            Repeater {
                                                // The lane's role model, not a count over
                                                // root.tracks: it notifies per clip and per
                                                // field, so an edit to one clip no longer re-runs
                                                // every other delegate's bindings.
                                                model: EditorState.clipsModel(laneStrip.trackIndex)
                                                delegate: TimelineClipItem {
                                                    panel: root
                                                    timelineColumn: trackColumn
                                                    trackIndex: laneStrip.trackIndex
                                                }
                                            }
                                        }
                                    }
                                }

                                // The track's own clips sit below the strips. This wrapper is
                                // what TimelineClipItem reads as its `trackRow` (it takes its
                                // parent), so the clips size themselves to the space left over
                                // without knowing lanes exist.
                                Item {
                                    id: trackClipArea
                                    // Carried explicitly, because TimelineClipItem takes this
                                    // Item as its own `trackRow` property — which shadows the
                                    // outer `trackRow` id at the binding below. Reaching for
                                    // trackRow.trackIndex there silently resolves to this Item,
                                    // and an Item without the property yields 0, so every track
                                    // rendered track 0's clips.
                                    property int trackIndex: trackRow.trackIndex
                                    y: adjustmentLaneStrips.height
                                    width: trackRow.width
                                    height: Math.max(0, trackRow.height - adjustmentLaneStrips.height)

                                    Repeater {
                                        model: EditorState.clipsModel(trackClipArea.trackIndex)
                                        delegate: TimelineClipItem {
                                            // panel: root is safe — TimelineClipItem's id is clipItem,
                                            // so it does not shadow TimelinePanel's root.
                                            panel: root
                                            timelineColumn: trackColumn
                                            trackIndex: trackClipArea.trackIndex
                                        }
                                    }
                                }

                                // Right-click a gap to ripple everything after it (and any
                                // linked partner clips on other tracks) left to close it.
                                Repeater {
                                    model: root.gapsForTrack(trackRow.trackIndex)
                                    delegate: Item {
                                        id: gapItem
                                        required property var modelData
                                        x: modelData.start * root.pxPerSecond
                                        width: (modelData.end - modelData.start) * root.pxPerSecond
                                        height: trackRow.height
                                        z: 1

                                        Rectangle {
                                            anchors.fill: parent
                                            visible: gapMouse.containsMouse
                                            color: Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                           Theme.panelAccent.b, 0.35)
                                        }

                                        MouseArea {
                                            id: gapMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.RightButton
                                            onPressed: gapContextMenu.popup()

                                            ThemedContextMenu {
                                                id: gapContextMenu
                                                ThemedMenuItem {
                                                    text: qsTr("Close Gap")
                                                    icon.name: Theme.icons.chevronsRightLeft
                                                    onTriggered: EditorState.closeGap(trackRow.trackIndex,
                                                                                      gapItem.modelData.start)
                                                }
                                                ThemedMenuItem { // VEDIT
                                                    text: qsTr("New track here")
                                                    icon.name: Theme.icons.layers
                                                    onTriggered: EditorState.addTrackNear(trackRow.trackIndex)
                                                }
                                            }
                                        }
                                    }
                                }

                                // VEDIT (조박사님, Vegas): the crossfade appears the moment a dragged clip
                                // starts to overlap another, not only after it is let go.
                                Repeater {
                                    model: root.liveOverlaps(trackRow.trackIndex)
                                    delegate: Rectangle {
                                        required property var modelData
                                        z: 11
                                        x: modelData.start * root.pxPerSecond
                                        width: Math.max(2, (modelData.end - modelData.start) * root.pxPerSecond)
                                        y: Theme.clipSelectionRingWidth
                                        height: parent.height - Theme.clipSelectionRingWidth * 2
                                        radius: Theme.radiusSm
                                        color: Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                       Theme.transitionOverlap.b, 0.55)
                                        border.width: 1
                                        border.color: Theme.transitionOverlap
                                        Canvas {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            onPaint: {
                                                const ctx = getContext("2d")
                                                ctx.reset()
                                                if (width < 0.5 || height < 0.5)
                                                    return
                                                const smooth = (t) => t * t * (3 - 2 * t)
                                                const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                                                ctx.strokeStyle = "rgba(255,255,255,0.75)"
                                                ctx.lineWidth = 1.4
                                                for (const gainAt of [(t) => 1.0 - smooth(t), (t) => smooth(t)]) {
                                                    ctx.beginPath()
                                                    for (let i = 0; i <= steps; ++i) {
                                                        const t = i / steps
                                                        const y = height * (1.0 - gainAt(t))
                                                        if (i === 0) ctx.moveTo(t * width, y)
                                                        else ctx.lineTo(t * width, y)
                                                    }
                                                    ctx.stroke()
                                                }
                                            }
                                            onWidthChanged: requestPaint()
                                            onHeightChanged: requestPaint()
                                        }
                                        // VEDIT (조박사님): the overlap's length while dragging too — the
                                        // same box as after the drop, shown even when the overlap is
                                        // still narrower than the box.
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: liveOverlapText.implicitWidth + 8
                                            height: liveOverlapText.implicitHeight + 2
                                            radius: 3
                                            color: Qt.rgba(0.12, 0.12, 0.12, 0.85)
                                            border.width: 1
                                            border.color: Qt.rgba(1, 1, 1, 0.75)
                                            Text {
                                                id: liveOverlapText
                                                anchors.centerIn: parent
                                                text: root.overlapLengthLabel(modelData.end - modelData.start)
                                                color: "#ffffff"
                                                font.family: Theme.monoFontFamily
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }

                                // Transition overlap regions (purple) — above clips for hit-testing.
                                Repeater {
                                    model: Math.max(0, root.tracks[trackRow.trackIndex].clips.length)
                                    delegate: Item {
                                        id: transitionRegion
                                        property int leftClipIndex: modelData
                                        property var leftClip: root.tracks[trackRow.trackIndex].clips[leftClipIndex]
                                        property string trackType: root.tracks[trackRow.trackIndex].type
                                        // Reading root.tracks keeps this bound to tracksChanged; without
                                        // that dependency the invokable is evaluated once and a transition
                                        // added later never registers (clicking would re-add a crossfade).
                                        property var transitionData: root.tracks[trackRow.trackIndex]
                                            ? EditorState.transitionBetweenClips(trackRow.trackIndex, leftClipIndex)
                                            : ({})
                                        property bool hasTransition: transitionData
                                                                       && Object.keys(transitionData).length > 0
                                        property bool transitionSelected: EditorState.selectedTransitionTrack === trackRow.trackIndex
                                                                          && EditorState.selectedTransitionLeftClip === leftClipIndex

                                        // VEDIT (조박사님): while an end is held the pair stays the pair
                                        // and the region stays up — the end can be taken past the other
                                        // one, where the dissolve is gone and the clips just meet.
                                        property bool edgeHeld: false
                                        property int heldPartner: -1
                                        // Find partner clip that abuts/overlaps this one.
                                        property int partnerIndex: {
                                            if (edgeHeld && heldPartner >= 0)
                                                return heldPartner
                                            const clips = root.tracks[trackRow.trackIndex].clips
                                            const left = leftClip
                                            if (!left)
                                                return -1
                                            let best = -1
                                            let bestStart = 1e12
                                            for (let i = 0; i < clips.length; i++) {
                                                if (i === leftClipIndex)
                                                    continue
                                                const right = clips[i]
                                                if (right.start < left.start)
                                                    continue
                                                const gap = right.start - (left.start + left.duration)
                                                if (gap > 0.001)
                                                    continue
                                                if (right.start < bestStart) {
                                                    bestStart = right.start
                                                    best = i
                                                }
                                            }
                                            return best
                                        }
                                        property var rightClip: partnerIndex >= 0
                                            ? root.tracks[trackRow.trackIndex].clips[partnerIndex]
                                            : null
                                        property bool physicallyOverlapping: leftClip && rightClip
                                            && rightClip.start < (leftClip.start + leftClip.duration)
                                        // VEDIT (조박사님): where the clips overlap, the dissolve's
                                        // ends ARE the clips' ends — always the whole overlap.
                                        property real regionStart: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (edgeHeld)
                                                return Math.min(rightClip.start, leftClip.start + leftClip.duration)
                                            if (physicallyOverlapping)
                                                return rightClip.start
                                            if (hasTransition && transitionData.start !== undefined)
                                                return transitionData.start
                                            return 0
                                        }
                                        property real regionEnd: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (edgeHeld)
                                                return Math.max(rightClip.start, leftClip.start + leftClip.duration)
                                            if (physicallyOverlapping)
                                                return leftClip.start + leftClip.duration
                                            if (hasTransition && transitionData.end !== undefined)
                                                return transitionData.end
                                            return 0
                                        }
                                        property bool showRegion: (trackType === "video"
                                                                   || trackType === "shape"
                                                                   || trackType === "text"
                                                                   || trackType === "audio")
                                                                  && leftClip && rightClip
                                                                  // VEDIT (조박사님): only a real hand-off — at least one
                                                                  // frame of overlap or of transition. A sliver left by
                                                                  // rounding drew an 8 px hatched box between clips that
                                                                  // merely touch, and meant nothing.
                                                                  && (physicallyOverlapping || hasTransition)
                                                                  && regionEnd - regionStart > 1e-4 // VEDIT: every real overlap shows

                                        z: 10
                                        // VEDIT: while its clip is being dragged the overlap is drawn live
                                        // (below), so the one from before the drag steps aside.
                                        readonly property bool draggedAway: (root.moveFollowActive && root.dropExcludeId !== ""
                                            && ((leftClip && leftClip.id === root.dropExcludeId)
                                                || (rightClip && rightClip.id === root.dropExcludeId)))
                                            || (root.liveTrimId !== ""
                                                && ((leftClip && leftClip.id === root.liveTrimId)
                                                    || (rightClip && rightClip.id === root.liveTrimId)))
                                        visible: (showRegion || edgeHeld) && !draggedAway
                                        x: regionStart * root.pxPerSecond
                                        // VEDIT (조박사님): no overlap, nothing drawn — not an 8 px sliver
                                        // that reads as a one-frame crossfade.
                                        width: (physicallyOverlapping || edgeHeld)
                                               ? Math.max(1, (regionEnd - regionStart) * root.pxPerSecond)
                                               : Math.max(8, (regionEnd - regionStart) * root.pxPerSecond)
                                        height: parent.height - Theme.clipSelectionRingWidth * 2
                                        y: Theme.clipSelectionRingWidth

                                        Rectangle {
                                            anchors.fill: parent
                                            // VEDIT: held at no overlap: nothing to show (the held edge
                                            // keeps its grab — opacity 0 on the region would drop it).
                                            visible: transitionRegion.physicallyOverlapping || !transitionRegion.edgeHeld
                                            radius: Theme.radiusSm
                                            color: transitionRegion.transitionSelected
                                                   ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                             Theme.transitionOverlap.b, 0.85)
                                                   : (transitionRegion.hasTransition
                                                      ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.65)
                                                      : Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.35))
                                            border.width: transitionRegion.transitionSelected ? 2 : 1
                                            border.color: Theme.transitionOverlap

                                            // VEDIT: a crossfade draws its two gain curves the way Vegas
                                            // does — outgoing falls slow, incoming rises fast, and they
                                            // cross above the middle (equal-power), so the blend is
                                            // visible at a glance instead of a flat hatch.
                                            readonly property bool drawsCurves: transitionRegion.hasTransition
                                                && transitionRegion.transitionData.kind === "crossfade"
                                            Canvas {
                                                id: crossfadeCurves
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                visible: parent.drawsCurves
                                                // VEDIT (조박사님): drawn exactly like a single clip's fade —
                                                // the same smooth (smoothstep) curve, 1.5 px white line and
                                                // the faded part shaded — the outgoing clip fading out and the
                                                // incoming one fading in.
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.reset()
                                                    if (width < 0.5 || height < 0.5)
                                                        return
                                                    const smooth = (t) => {
                                                        const u = Math.max(0, Math.min(1, t))
                                                        return u * u * (3 - 2 * u)
                                                    }
                                                    const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                                                    const shadeAndLine = (gainAt) => {
                                                        ctx.fillStyle = "rgba(0,0,0,0.38)"
                                                        ctx.beginPath()
                                                        ctx.moveTo(0, 0)
                                                        ctx.lineTo(width, 0)
                                                        for (let i = steps; i >= 0; --i) {
                                                            const t = i / steps
                                                            ctx.lineTo(t * width, height * (1.0 - gainAt(t)))
                                                        }
                                                        ctx.closePath()
                                                        ctx.fill()
                                                        ctx.strokeStyle = "rgba(255,255,255,0.9)"
                                                        ctx.lineWidth = 1.5
                                                        ctx.beginPath()
                                                        for (let i = 0; i <= steps; ++i) {
                                                            const t = i / steps
                                                            const y = height * (1.0 - gainAt(t))
                                                            if (i === 0)
                                                                ctx.moveTo(t * width, y)
                                                            else
                                                                ctx.lineTo(t * width, y)
                                                        }
                                                        ctx.stroke()
                                                    }
                                                    // VEDIT: the transition's own fade types (Vegas)
                                                    const g = (c, t) => {
                                                        t = Math.max(0, Math.min(1, t))
                                                        if (c === "linear") return t
                                                        if (c === "equalPower") return Math.sin(t * Math.PI / 2)
                                                        if (c === "slow") return 1 - Math.cos(t * Math.PI / 2)
                                                        if (c === "sharp") return 0.5 - Math.sin(Math.asin(1 - 2 * t) / 3)
                                                        return smooth(t)
                                                    }
                                                    const oc = transitionRegion.transitionData.outCurve || "smooth"
                                                    const ic = transitionRegion.transitionData.inCurve || "smooth"
                                                    const outG = (t) => g(oc, 1.0 - t)
                                                    const inG = (t) => g(ic, t)
                                                    ctx.save()
                                                    ctx.globalAlpha = 0.55 // two shades over each other: lighter each
                                                    shadeAndLine(outG) // outgoing: fade out
                                                    shadeAndLine(inG)  // incoming: fade in
                                                    ctx.restore()
                                                    // the two lines on top at full strength
                                                    ctx.strokeStyle = "rgba(255,255,255,0.9)"
                                                    ctx.lineWidth = 1.5
                                                    for (const gainAt of [outG, inG]) {
                                                        ctx.beginPath()
                                                        for (let i = 0; i <= steps; ++i) {
                                                            const t = i / steps
                                                            const y = height * (1.0 - gainAt(t))
                                                            if (i === 0)
                                                                ctx.moveTo(t * width, y)
                                                            else
                                                                ctx.lineTo(t * width, y)
                                                        }
                                                        ctx.stroke()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                                onVisibleChanged: if (visible) requestPaint()
                                                property string curvesKey: (transitionRegion.transitionData.outCurve || "")
                                                                           + (transitionRegion.transitionData.inCurve || "")
                                                onCurvesKeyChanged: requestPaint()
                                            }

                                            // VEDIT (조박사님): any other overlap or transition shows the same
                                            // crossing S-curves faintly behind it, so a hand-off reads as one
                                            // at a glance whatever its kind.
                                            Canvas {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                visible: !parent.drawsCurves
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.reset()
                                                    if (width < 0.5 || height < 0.5)
                                                        return
                                                    const smooth = (t) => t * t * (3 - 2 * t)
                                                    const steps = Math.max(8, Math.min(64, Math.ceil(width / 2)))
                                                    ctx.strokeStyle = "rgba(255,255,255,0.35)"
                                                    ctx.lineWidth = 1.2
                                                    for (const gainAt of [(t) => 1.0 - smooth(t), (t) => smooth(t)]) {
                                                        ctx.beginPath()
                                                        for (let i = 0; i <= steps; ++i) {
                                                            const t = i / steps
                                                            const y = height * (1.0 - gainAt(t))
                                                            if (i === 0)
                                                                ctx.moveTo(t * width, y)
                                                            else
                                                                ctx.lineTo(t * width, y)
                                                        }
                                                        ctx.stroke()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                                onVisibleChanged: if (visible) requestPaint()
                                            }

                                            // Diagonal hatch so overlaps read as a blend zone.
                                            Canvas {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                visible: false // VEDIT: no hatch — it read as a mark with no meaning
                                                opacity: 0.35
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.clearRect(0, 0, width, height)
                                                    ctx.strokeStyle = Theme.onMedia
                                                    ctx.lineWidth = 1
                                                    const step = 6
                                                    for (let x = -height; x < width; x += step) {
                                                        ctx.beginPath()
                                                        ctx.moveTo(x, height)
                                                        ctx.lineTo(x + height, 0)
                                                        ctx.stroke()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                            }

                                            // VEDIT (조박사님, as in Vegas): the overlap's length in a small
                                            // box in the middle — seconds.frames at the ruler's frame rate.
                                            // Under it, the name of a transition put there by hand (an
                                            // overlap's automatic dissolve needs no name).
                                            Column {
                                                anchors.centerIn: parent
                                                spacing: 1
                                                readonly property string transitionName:
                                                    transitionRegion.hasTransition && !transitionRegion.transitionData.autoOverlap
                                                    ? (transitionRegion.transitionData.label || "") : ""
                                                Rectangle {
                                                    id: overlapBadge
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    visible: transitionRegion.width >= width + 4
                                                             && transitionRegion.height >= height + 2
                                                    width: overlapText.implicitWidth + 8
                                                    height: overlapText.implicitHeight + 2
                                                    radius: 3
                                                    color: Qt.rgba(0.12, 0.12, 0.12, 0.85)
                                                    border.width: 1
                                                    border.color: Qt.rgba(1, 1, 1, 0.75)
                                                    Text {
                                                        id: overlapText
                                                        anchors.centerIn: parent
                                                        text: root.overlapLengthLabel(transitionRegion.regionEnd
                                                                                      - transitionRegion.regionStart)
                                                        color: "#ffffff"
                                                        font.family: Theme.monoFontFamily
                                                        font.pixelSize: 11 // 조박사님: 16 px was too big
                                                    }
                                                }
                                                Text {
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    visible: parent.transitionName.length > 0 && overlapBadge.visible
                                                             && transitionRegion.height >= overlapBadge.height + height + 3
                                                    width: Math.min(implicitWidth, transitionRegion.width - 4)
                                                    elide: Text.ElideRight
                                                    horizontalAlignment: Text.AlignHCenter
                                                    text: parent.transitionName
                                                    color: "#ffffff"
                                                    style: Text.Outline
                                                    styleColor: Qt.rgba(0, 0, 0, 0.8)
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 11
                                                }
                                            }

                                            ThemedToolTip {
                                                visible: transitionMouse.containsMouse
                                                         && transitionRegion.hasTransition
                                                delay: 400
                                                text: transitionRegion.transitionData.label
                                                      || transitionRegion.transitionData.kind
                                                      || ""
                                            }
                                        }

                                        DropArea {
                                            anchors.fill: parent
                                            keys: ["application/x-drift-transition"]
                                            onDropped: (drop) => {
                                                drop.accept(Qt.CopyAction)
                                                const kind = drop.getDataAsString("application/x-drift-transition")
                                                if (kind.length > 0)
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              root.transitionKindForTrack(trackRow.trackIndex, kind),
                                                                              0.5)
                                            }
                                        }

                                        FadeCurvePopup { // VEDIT: Vegas's Fade Type grid on a right-click
                                            id: transitionFadePopup
                                            mode: "transition"
                                            y: -height - 6
                                        }

                                        // VEDIT (조박사님, Vegas): grab either end of the transition and drag
                                        // it — longer or shorter. The left end brings in earlier footage of
                                        // the next clip, the right end later footage of the one before; the
                                        // clips' far ends stay put, and it stops where the footage runs out.
                                        Repeater {
                                            model: transitionRegion.rightClip ? [-1, 1] : []
                                            delegate: MouseArea {
                                                id: overlapEdge
                                                required property var modelData
                                                readonly property int side: modelData
                                                width: 18 // VEDIT: easy to catch; the clip's own trim stands aside here
                                                height: parent.height
                                                x: side < 0 ? -width / 2 : parent.width - width / 2
                                                z: 5
                                                hoverEnabled: true
                                                preventStealing: true
                                                cursorShape: Qt.SizeHorCursor
                                                Rectangle {
                                                    visible: overlapEdge.containsMouse || overlapEdge.pressed
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    width: 3
                                                    height: parent.height
                                                    color: Theme.primary
                                                }
                                                onPressed: {
                                                    transitionRegion.heldPartner = transitionRegion.partnerIndex
                                                    transitionRegion.edgeHeld = true
                                                    EditorState.beginPreviewDrag(qsTr("Transition length"))
                                                }
                                                onPositionChanged: (mouse) => {
                                                    if (!pressed)
                                                        return
                                                    const x = mapToItem(trackRow, mouse.x, 0).x
                                                    EditorState.previewOverlapEdge(trackRow.trackIndex,
                                                                                   transitionRegion.leftClipIndex,
                                                                                   transitionRegion.partnerIndex,
                                                                                   side, x / root.pxPerSecond)
                                                }
                                                onReleased: {
                                                    EditorState.commitPreviewDrag()
                                                    transitionRegion.edgeHeld = false
                                                    transitionRegion.heldPartner = -1
                                                }
                                                onCanceled: {
                                                    EditorState.cancelPreviewDrag()
                                                    transitionRegion.edgeHeld = false
                                                    transitionRegion.heldPartner = -1
                                                }
                                                // VEDIT: dragged to no overlap, the dissolve (and this
                                                // edge with it) goes mid-drag — the edit is kept.
                                                Component.onDestruction: if (pressed) EditorState.commitPreviewDrag()
                                            }
                                        }

                                        MouseArea {
                                            id: transitionMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: sliding ? Qt.SizeHorCursor : Qt.PointingHandCursor
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            preventStealing: sliding
                                            // VEDIT (조박사님: Alt + 끌기): hold Alt and drag the dissolve to
                                            // move it along — same length; the clip before's end and the
                                            // next clip's start go with it, as far as their footage lasts.
                                            property bool sliding: false
                                            property bool slid: false
                                            property real pressTrackX: 0
                                            onPressed: (mouse) => {
                                                slid = false
                                                if (mouse.button !== Qt.LeftButton || !(mouse.modifiers & Qt.AltModifier)
                                                    || !transitionRegion.physicallyOverlapping)
                                                    return
                                                pressTrackX = mapToItem(trackRow, mouse.x, 0).x
                                                transitionRegion.heldPartner = transitionRegion.partnerIndex
                                                transitionRegion.edgeHeld = true
                                                if (transitionRegion.hasTransition)
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                EditorState.beginPreviewDrag(qsTr("Move transition"))
                                                sliding = true
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!sliding)
                                                    return
                                                const dx = mapToItem(trackRow, mouse.x, 0).x - pressTrackX
                                                if (Math.abs(dx) >= 2)
                                                    slid = true
                                                EditorState.previewSlideOverlap(trackRow.trackIndex,
                                                                                transitionRegion.leftClipIndex,
                                                                                transitionRegion.partnerIndex,
                                                                                dx / root.pxPerSecond)
                                            }
                                            onReleased: {
                                                if (!sliding)
                                                    return
                                                if (slid)
                                                    EditorState.commitPreviewDrag()
                                                else
                                                    EditorState.cancelPreviewDrag()
                                                sliding = false
                                                transitionRegion.edgeHeld = false
                                                transitionRegion.heldPartner = -1
                                            }
                                            onCanceled: {
                                                if (!sliding)
                                                    return
                                                EditorState.cancelPreviewDrag()
                                                sliding = false
                                                transitionRegion.edgeHeld = false
                                                transitionRegion.heldPartner = -1
                                            }
                                            onClicked: (mouse) => {
                                                if (slid) { // VEDIT: a slide, not a click
                                                    slid = false
                                                    return
                                                }
                                                if (mouse.button === Qt.RightButton) {
                                                    if (!transitionRegion.hasTransition)
                                                        EditorState.addTransition(trackRow.trackIndex,
                                                                                  transitionRegion.leftClipIndex,
                                                                                  "crossfade", 0.5)
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                    transitionFadePopup.trackIndex = trackRow.trackIndex
                                                    transitionFadePopup.transition =
                                                        EditorState.transitionBetweenClips(trackRow.trackIndex,
                                                                                           transitionRegion.leftClipIndex)
                                                    transitionFadePopup.open()
                                                    return
                                                }
                                                if (transitionRegion.hasTransition) {
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                } else {
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              "crossfade", 0.5)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Library asset drops — declared after track rows so Wayland's
                    // Z-order DnD hit-test prefers this overlay. Do not set
                    // opacity: 0: Item docs say opacity 0 disables mouse events,
                    // and Mutter then never delivers drag moves to this DropArea.
                    DropArea {
                        id: timelineAssetDrop
                        // No `enabled: draggingAssetIndex >= 0` gate: `keys` below
                        // already excludes effect/shape/transition drags, and
                        // gating on state written by the drag source in the same
                        // tick meant that if it did not land before the first
                        // DragEnter the overlay was skipped entirely — leaving only
                        // the per-track DropAreas, which accept nothing but a
                        // compatible track and never a new-track boundary.
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        // Extends past the last track to the bottom of the
                        // content area: dropping in that empty space is how a
                        // track gets appended below the existing ones.
                        height: Math.max(root.totalTracksHeightCached,
                                         flick.contentHeight - flick.headerHeight)
                        z: 250
                        // "text/uri-list" is a drag from outside the app — the file manager,
                        // usually. It carries files that have not been imported yet, where
                        // "text/plain" carries the index of a row already in the bin.
                        keys: ["text/plain", "text/uri-list"]

                        function isFileDrag(drop) {
                            return drop.keys.indexOf("text/uri-list") !== -1
                        }

                        function assetIndexFromDrop(drop) {
                            if (EditorState.draggingAssetIndex >= 0)
                                return EditorState.draggingAssetIndex
                            const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                            const idx = parseInt(text)
                            return isNaN(idx) ? -1 : idx
                        }

                        function updateAssetDrag(drop) {
                            if (isFileDrag(drop)) {
                                root.updateUrlDropPreview(drop)
                                return
                            }
                            const assetIndex = assetIndexFromDrop(drop)
                            if (assetIndex < 0) {
                                root.clearLandingPreview()
                                return
                            }
                            root.updateAssetDropPreview(assetIndex, drop.x, drop.y)
                        }

                        onEntered: (drop) => updateAssetDrag(drop)
                        onPositionChanged: (drop) => updateAssetDrag(drop)
                        onExited: {
                            root.clearUrlDropState()
                            root.clearLandingPreview()
                        }
                        onDropped: (drop) => {
                            drop.accept(Qt.CopyAction)
                            if (isFileDrag(drop)) {
                                // Not cleared first, unlike the branch below: performUrlDrop
                                // keeps the outline on screen at the frozen spot for as long as
                                // the import it starts is still running.
                                root.performUrlDrop(drop.hasUrls ? drop.urls : [], drop.x, drop.y)
                                return
                            }
                            const assetIndex = assetIndexFromDrop(drop)
                            root.clearLandingPreview()
                            root.performAssetDrop(assetIndex, drop.x, drop.y)
                        }
                    }

                    // New-track indicator: a full-height ghost lane straddling the
                    // boundary the track would be inserted at, with the clip that
                    // would land in it. An overlay rather than a reserved row, so
                    // no DropArea geometry moves while a drag is live — and drawn
                    // at real track height so it reads as a track, not a hairline.
                    // VEDIT: the seam a dragged clip would open a new track at — a red line.
                    Rectangle {
                        id: moveSeamLine
                        visible: root.moveInsertIndex >= 0
                        x: 0
                        width: parent.width
                        height: 3
                        radius: 1.5
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.seamY(root.moveInsertIndex) - height / 2
                        color: "#ff3b30"
                        z: 245
                    }

                    Item {
                        id: newTrackIndicator
                        // Not also gated on the drag being active: dropCreatesNewTrack
                        // is only ever set during one, and is cleared on exit, drop
                        // and abandon — an extra condition here is one more way for
                        // the ghost to silently not appear.
                        visible: root.dropCreatesNewTrack
                        readonly property real laneHeight: {
                            // A file-manager drag has no bin row to ask about, so its lane is
                            // sized from the kind guessed off the extension instead.
                            const t = root.pendingDropKind.length > 0
                                    ? EditorState.trackTypeForKind(root.pendingDropKind)
                                    : EditorState.trackTypeForAsset(EditorState.draggingAssetIndex)
                            if (t === "video") return Theme.trackHeightVideo
                            if (t === "audio") return Theme.trackHeightAudio
                            if (t === "shape") return Theme.trackHeightShape
                            if (t === "subtitle") return Theme.trackHeightSubtitle
                            return Theme.trackHeightText
                        }
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.newTrackBoundaryY(root.dropNewTrackIndex)
                           - laneHeight / 2
                        width: parent.width
                        height: laneHeight
                        z: 240

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.panelBackground.r, Theme.panelBackground.g,
                                           Theme.panelBackground.b, 0.92)
                            border.width: Theme.borderWidth
                            border.color: Theme.primary
                        }

                        // The boundary itself, so it is unambiguous which gap the
                        // lane is going into.
                        Rectangle {
                            width: parent.width
                            height: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.primary
                        }

                        Rectangle {
                            x: root.dropStartSeconds * root.pxPerSecond
                            width: Math.max(2, root.dropDurationSeconds * root.pxPerSecond)
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.margins: 2
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.3)
                            border.width: 2
                            border.color: Theme.primary

                            // The outline is held on screen after a file drop for as
                            // long as the import it started is still probing, so it has
                            // to say why it is sitting there empty.
                            Text {
                                anchors.centerIn: parent
                                visible: root.importDropPending
                                text: qsTr("Importing…")
                                color: Theme.primary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeTiny
                                font.weight: Font.Medium

                                SequentialAnimation on opacity {
                                    running: root.importDropPending
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.45; duration: Theme.durationSlow; easing.type: Theme.easing }
                                    NumberAnimation { to: 1.0; duration: Theme.durationSlow; easing.type: Theme.easing }
                                }
                            }
                        }
                    }

                    // CapCut-style selection box drawn while marquee-dragging.
                    Rectangle {
                        visible: root.marqueeActive
                                 && (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                        x: root.marqueeLeft
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.marqueeTop
                        width: root.marqueeWidth
                        height: root.marqueeHeight
                        z: 8
                        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                        border.width: Theme.borderWidth
                        border.color: Theme.primary
                    }

                    // snap guide -------------------------------------------------------------
                    Rectangle {
                        visible: opacity > 0
                        opacity: root.snapGuideSeconds >= 0 ? 1 : 0
                        x: root.snapGuideSeconds * root.pxPerSecond
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: Theme.borderWidth
                        height: root.totalTracksHeightCached
                        color: Theme.snapGuide
                        z: 6

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        // Says what the clip snapped to, instead of leaving a bare
                        // unexplained line on screen.
                        Rectangle {
                            visible: parent.opacity > 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            height: snapLabel.implicitHeight + Theme.spacingSm
                            width: snapLabel.implicitWidth + Theme.spacingLg
                            radius: Theme.radiusXs
                            color: Theme.snapGuide

                            Text {
                                id: snapLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.snapGuideSeconds)
                                color: Theme.overlayColor
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }
                    }

                    // playhead ---------------------------------------------------------------
                    Item {
                        id: playhead
                        y: 0
                        // Above the seek strip (z: 2), otherwise the scrub area
                        // covers the handle and swallows the press before drag starts.
                        z: 3
                        width: Theme.playheadLineWidth
                        // VEDIT: CapCut — the line runs to the bottom of the timeline even below
                        // the last track, so it can be caught anywhere down the panel.
                        height: Math.max(Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                                         + root.totalTracksHeightCached, flick.contentHeight)
                        readonly property real tracksBottom: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                                                             + root.totalTracksHeightCached

                        Binding {
                            target: playhead
                            property: "x"
                            value: EditorState.playheadSeconds * root.pxPerSecond
                            when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
                                  && !playheadLowerGrab.drag.active // VEDIT
                        }

                        // Follow either drag live so the preview scrubs with it.
                        onXChanged: {
                            if (playheadDragArea.drag.active || playheadLineDrag.drag.active
                                    || playheadLowerGrab.drag.active) // VEDIT
                                EditorState.playheadSeconds = playhead.x / root.pxPerSecond
                        }

                        function finishSeek() {
                            EditorState.playheadSeconds =
                                EditorState.snapTime(playhead.x / root.pxPerSecond)
                        }

                        // VEDIT: the line thickens and glows while the pointer is on it or dragging
                        // it, so it reads as caught before the press.
                        readonly property bool caught: playheadLineDrag.containsMouse || playheadLineDrag.pressed
                                                       || playheadLowerGrab.containsMouse || playheadLowerGrab.pressed
                                                       || playheadDragArea.containsMouse || playheadDragArea.pressed
                        // 조박사님: thinner, not thicker — a 1 px bright core with light bleeding out.
                        Rectangle { // the bleed: fades to nothing either side
                            visible: playhead.caught
                            x: Theme.playheadLineWidth / 2 - width / 2
                            y: Theme.timelineRulerHeight * 0.55
                            width: 11 // 조박사님: 11 px of bleed
                            height: parent.height - y
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.0) }
                                GradientStop { position: 0.5; color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.45) }
                                GradientStop { position: 1.0; color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.0) }
                            }
                        }
                        Rectangle {
                            x: playhead.caught ? Theme.playheadLineWidth / 2 - 0.5 : 0
                            y: Theme.timelineRulerHeight * 0.55
                            width: playhead.caught ? 1 : Theme.playheadLineWidth
                            height: parent.height - y
                            color: playhead.caught ? Qt.lighter(Theme.primary, 1.35) : Theme.primary
                        }

                        // CapCut/Premiere-style scrubber head in the seek strip.
                        Item {
                            id: playheadHandle
                            width: Theme.playheadHandleSize
                            height: Theme.playheadHandleSize + 2
                            x: -width / 2 + Theme.playheadLineWidth / 2
                            y: 3

                            Rectangle {
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                height: parent.height * 0.55
                                radius: 2
                                color: Theme.primary
                            }

                            // Pointed tip so the head reads as a scrubber, not a knob.
                            Canvas {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: parent.height * 0.4
                                width: parent.width
                                height: parent.height * 0.6
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.beginPath()
                                    ctx.moveTo(0, 0)
                                    ctx.lineTo(width, 0)
                                    ctx.lineTo(width * 0.5, height)
                                    ctx.closePath()
                                    ctx.fillStyle = Theme.primary
                                    ctx.fill()
                                }
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                Component.onCompleted: requestPaint()
                            }
                        }

                        // Wide grab across the seek strip head.
                        MouseArea {
                            id: playheadDragArea
                            width: Theme.playheadSeekGrabWidth
                            height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: 0
                            cursorShape: Qt.SplitHCursor // VEDIT: a caught line, not a resize
                            preventStealing: true
                            hoverEnabled: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onPressed: (mouse) => {
                                pressSceneX = mapToItem(null, mouse.x, 0).x
                                movedSincePress = false
                                root.beginPlayheadSeek()
                            }
                            onPositionChanged: (mouse) => { // VEDIT: edge scroll
                                if (Math.abs(mapToItem(null, mouse.x, 0).x - pressSceneX) > 2)
                                    movedSincePress = true
                                if (drag.active)
                                    root.noteEdgeDrag(playhead, mapToItem(flick, mouse.x, 0).x)
                            }
                            // VEDIT (조박사님: "재생선 바로 옆 몇 프레임을 찍으면 안 찍힌다"): a press that
                            // never moved is a click — the playhead goes exactly where it landed.
                            property real pressSceneX: 0
                            property bool movedSincePress: false
                            onReleased: (mouse) => {
                                if (!movedSincePress)
                                    playhead.x = Math.max(0, mapToItem(playhead.parent, mouse.x, 0).x)
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                        }

                        // Narrow grab down the timeline line — stays thin so clip
                        // clicks aren't stolen.
                        MouseArea {
                            id: playheadLineDrag
                            width: 9 // VEDIT: a little easier to catch
                            hoverEnabled: true
                            height: playhead.tracksBottom - playheadDragArea.height // VEDIT
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: playheadDragArea.height
                            cursorShape: Qt.SplitHCursor // VEDIT: a caught line, not a resize
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onPressed: (mouse) => {
                                pressSceneX = mapToItem(null, mouse.x, 0).x
                                movedSincePress = false
                                // VEDIT (조박사님): a volume point just made or picked sits right under
                                // the playhead (picking moves it there) — the press is the point's.
                                // The ruler handle still moves the playhead.
                                const key = EditorState.selectedVolumeKey
                                // Same for a picked ◆ on a picture clip's KEY line.
                                if (key && key.clipId
                                        && (root.envelopeClips[key.clipId] || root.keyLineState[key.clipId] !== false)
                                        && Math.abs(Number(key.seconds) - EditorState.playheadSeconds) < 0.05) {
                                    mouse.accepted = false
                                    return
                                }
                                // A ◆ just made from the panel sits under the playhead on the selected clip.
                                if (EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0) {
                                    const marks = EditorState.clipKeyframeMarks(EditorState.selectedTrack,
                                                                                EditorState.selectedClip)
                                    for (let i = 0; i < marks.length; ++i)
                                        if (Math.abs(Number(marks[i]) - EditorState.playheadSeconds) < 0.05) {
                                            mouse.accepted = false
                                            return
                                        }
                                }
                                root.beginPlayheadSeek()
                            }
                            onPositionChanged: (mouse) => { // VEDIT: edge scroll
                                if (Math.abs(mapToItem(null, mouse.x, 0).x - pressSceneX) > 2)
                                    movedSincePress = true
                                if (drag.active)
                                    root.noteEdgeDrag(playhead, mapToItem(flick, mouse.x, 0).x)
                            }
                            // VEDIT (조박사님: "재생선 바로 옆 몇 프레임을 찍으면 안 찍힌다"): a press that
                            // never moved is a click — the playhead goes exactly where it landed.
                            property real pressSceneX: 0
                            property bool movedSincePress: false
                            onReleased: (mouse) => {
                                if (!movedSincePress)
                                    playhead.x = Math.max(0, mapToItem(playhead.parent, mouse.x, 0).x)
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                        }

                        // VEDIT: below the last track nothing else wants the press, so the line
                        // there is a wide (40 px) handle — easy to catch with a mouse or touchpad.
                        MouseArea {
                            id: playheadLowerGrab
                            width: 14 // VEDIT: was 40 — wide enough to catch, narrow enough to click beside
                            hoverEnabled: true
                            height: Math.max(0, parent.height - playhead.tracksBottom)
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: playhead.tracksBottom
                            visible: height > 0
                            cursorShape: Qt.SplitHCursor // VEDIT: a caught line, not a resize
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onPressed: (mouse) => {
                                pressSceneX = mapToItem(null, mouse.x, 0).x
                                movedSincePress = false
                                root.beginPlayheadSeek()
                            }
                            onPositionChanged: (mouse) => { // VEDIT: edge scroll
                                if (Math.abs(mapToItem(null, mouse.x, 0).x - pressSceneX) > 2)
                                    movedSincePress = true
                                if (drag.active)
                                    root.noteEdgeDrag(playhead, mapToItem(flick, mouse.x, 0).x)
                            }
                            // VEDIT (조박사님: "재생선 바로 옆 몇 프레임을 찍으면 안 찍힌다"): a press that
                            // never moved is a click — the playhead goes exactly where it landed.
                            property real pressSceneX: 0
                            property bool movedSincePress: false
                            onReleased: (mouse) => {
                                if (!movedSincePress)
                                    playhead.x = Math.max(0, mapToItem(playhead.parent, mouse.x, 0).x)
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                        }
                    }

                    // cut tools ----------------------------------------------------------------
                    // Sits above the tracks and playhead so it both draws the red
                    // dashed virtual playhead (and, for the trim tools, the red
                    // gradient over the doomed side) and intercepts the click.
                    Item {
                        id: cutOverlay
                        visible: root.timelineTool !== ""
                        enabled: root.timelineTool !== ""
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: root.totalTracksHeightCached
                        z: 20

                        readonly property bool trimMode: root.timelineTool === "trimStart"
                                                         || root.timelineTool === "trimEnd"
                        readonly property var hoverClip: (root.cutHoverTrack >= 0 && root.cutHoverClip >= 0
                            && root.cutHoverTrack < root.tracks.length
                            && root.cutHoverClip < root.tracks[root.cutHoverTrack].clips.length)
                            ? root.tracks[root.cutHoverTrack].clips[root.cutHoverClip]
                            : null

                        function seconds(mx) {
                            return EditorState.snapTime(Math.max(0, mx) / root.pxPerSecond)
                        }

                        function updateHover(mx, my) {
                            root.cutHoverSeconds = seconds(mx)
                            const trackIdx = root.trackIndexAtY(my)
                            root.cutHoverTrack = trackIdx
                            root.cutHoverClip = trackIdx >= 0
                                ? root.clipIndexAtPosition(trackIdx, Math.max(0, mx))
                                : -1
                        }

                        MouseArea {
                            id: cutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.CrossCursor
                            acceptedButtons: Qt.LeftButton
                            onEntered: cutOverlay.updateHover(mouseX, mouseY)
                            onPositionChanged: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
                            onExited: {
                                root.cutHoverSeconds = -1
                                root.cutHoverTrack = -1
                                root.cutHoverClip = -1
                            }
                            onClicked: (mouse) => {
                                const atSeconds = cutOverlay.seconds(mouse.x)
                                const trackIdx = root.trackIndexAtY(mouse.y)
                                if (trackIdx < 0)
                                    return
                                const clipIdx = root.clipIndexAtPosition(trackIdx, Math.max(0, mouse.x))
                                if (clipIdx < 0)
                                    return
                                if (root.timelineTool === "trimStart")
                                    EditorState.splitClipLeftAt(trackIdx, clipIdx, atSeconds)
                                else if (root.timelineTool === "trimEnd")
                                    EditorState.splitClipRightAt(trackIdx, clipIdx, atSeconds)
                                else
                                    EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
                            }
                        }

                        // Red gradient on the doomed side of the hovered clip: a
                        // fixed band attached to the cut line, reddest at the
                        // pointer and fading away from it, clamped to the clip.
                        Rectangle {
                            id: doomGradient
                            readonly property real bandLength: 80
                            visible: cutOverlay.trimMode && cutOverlay.hoverClip !== null
                                     && root.cutHoverSeconds >= 0
                            readonly property real clipStartX: cutOverlay.hoverClip
                                ? cutOverlay.hoverClip.start * root.pxPerSecond : 0
                            readonly property real clipEndX: cutOverlay.hoverClip
                                ? (cutOverlay.hoverClip.start + cutOverlay.hoverClip.duration) * root.pxPerSecond : 0
                            readonly property real cutX: Math.max(clipStartX,
                                Math.min(clipEndX, root.cutHoverSeconds * root.pxPerSecond))
                            readonly property color solid: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.55)
                            readonly property color clear: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.0)

                            x: root.timelineTool === "trimStart"
                               ? Math.max(clipStartX, cutX - bandLength)
                               : cutX
                            width: root.timelineTool === "trimStart"
                                   ? cutX - x
                                   : Math.min(clipEndX, cutX + bandLength) - cutX
                            y: root.trackOffsetY(root.cutHoverTrack)
                            height: cutOverlay.hoverClip
                                ? root.trackHeight(root.cutHoverTrack) : 0
                            radius: Theme.radiusSm

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                // Red toward the cut line (the pointer), fading away.
                                GradientStop {
                                    position: 0.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.clear : doomGradient.solid
                                }
                                GradientStop {
                                    position: 1.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.solid : doomGradient.clear
                                }
                            }
                        }

                        // Red dashed virtual playhead. A column of dashes reflows
                        // with the track height without any repaint plumbing.
                        Column {
                            visible: root.cutHoverSeconds >= 0
                            x: root.cutHoverSeconds * root.pxPerSecond
                            spacing: 3
                            Repeater {
                                model: Math.ceil(cutOverlay.height / 7)
                                delegate: Rectangle {
                                    width: 2
                                    height: 4
                                    color: Theme.destructive
                                }
                            }
                        }
                    }
                }
            }
        }
        } // Column (keyframes + tracks)
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
        // A drag abandoned outside the timeline never fires onExited/onDropped
        // on any DropArea here, so the landing outline used to stay painted.
        function onDraggingAssetIndexChanged() {
            if (EditorState.draggingAssetIndex < 0)
                root.clearLandingPreview()
        }
    }

    ThemedDialog {
        id: bookmarkRenameDialog
        title: qsTr("Rename bookmark")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Label")
                size: "sm"
            }
            ThemedTextField {
                id: bookmarkRenameField
                width: parent.width
                placeholderText: qsTr("Bookmark name")
                // Focus the field once the dialog is up so typing starts immediately.
                Component.onCompleted: Qt.callLater(function() {
                    if (bookmarkRenameDialog.visible)
                        forceActiveFocus()
                })
            }
        }

        onOpened: {
            bookmarkRenameField.forceActiveFocus()
            bookmarkRenameField.selectAll()
        }
        onAccepted: {
            if (bookmarkRow.renameIndex < 0)
                return
            const bookmarks = EditorState.bookmarks
            if (bookmarkRow.renameIndex >= bookmarks.length)
                return
            const seconds = bookmarks[bookmarkRow.renameIndex].seconds
            const label = bookmarkRenameField.text.trim()
            EditorState.updateBookmark(bookmarkRow.renameIndex, seconds,
                                       label.length > 0 ? label : qsTr("Bookmark"))
            bookmarkRow.renameIndex = -1
        }
        onRejected: bookmarkRow.renameIndex = -1
    }

    ThemedDialog {
        id: clipRenameDialog
        title: qsTr("Rename clip")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: clipRenameField
                width: parent.width
                placeholderText: qsTr("Clip name")
            }
        }

        onOpened: {
            clipRenameField.forceActiveFocus()
            clipRenameField.selectAll()
        }
        onAccepted: {
            if (root.renameClipTrack < 0 || root.renameClipIndex < 0)
                return
            const label = clipRenameField.text.trim()
            if (label.length > 0)
                EditorState.setClipName(root.renameClipTrack, root.renameClipIndex, label)
            root.renameClipTrack = -1
            root.renameClipIndex = -1
        }
        onRejected: {
            root.renameClipTrack = -1
            root.renameClipIndex = -1
        }
    }

    NameDialog {
        id: effectPresetNameDialog
        placeholder: qsTr("My look")
        onSubmitted: function(name) {
            if (root.savePresetTrack < 0 || root.savePresetClip < 0)
                return
            EditorState.saveClipEffectsAsPreset(root.savePresetTrack, root.savePresetClip, name)
            root.savePresetTrack = -1
            root.savePresetClip = -1
        }
        onRejected: {
            root.savePresetTrack = -1
            root.savePresetClip = -1
        }
    }
}
