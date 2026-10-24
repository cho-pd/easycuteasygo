import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Fixed left-hand track header column: per-track mute/hide/clip-display toggles,
// name, reorder-by-drag handle and the track context menu. Owns the
// track-drag state; the panel only supplies the track list and vertical scroll.
Item {
    id: root

    // Track model (EditorState.tracks) and the timeline's live vertical scroll,
    // so headers stay aligned with their rows.
    property var tracks: []
    property real contentY: 0
    // Desktop passes EditorState.trackLabelsWidth; Android a narrower fixed value.
    property real labelsWidth: Theme.trackLabelsWidth
    // Phone: grip + mute/hide only (no type glyph / waveform / name band).
    property bool compact: false
    // Touch shell: reorder has to be asked for, and the context menu needs a
    // route that is not the right mouse button.
    property bool touchMode: false
    // Desktop: a handle on the right edge drags EditorState.trackLabelsWidth.
    property bool resizable: false

    // Track-header reorder: source index and live drop target while dragging,
    // plus the insertion boundary the indicator line is drawn at.
    property int draggingTrackFrom: -1
    property int draggingTrackTo: -1
    property int draggingTrackSlot: -1

    onDraggingTrackToChanged: Haptics.lane(draggingTrackTo)

    // Pending delete confirmation — index kept until Accept/Reject so the menu
    // can close without wiping the track immediately.
    property int pendingDeleteTrack: -1
    // Same idea for the rename dialog.
    property int pendingRenameTrack: -1

    clip: true

    ThemedDialog {
        id: renameTrackDialog
        title: qsTr("Rename track")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedTextField {
                id: renameTrackField
                width: parent.width
                placeholderText: qsTr("Track name")
            }
        }

        onOpened: {
            renameTrackField.forceActiveFocus()
            renameTrackField.selectAll()
        }
        onAccepted: {
            if (root.pendingRenameTrack >= 0)
                EditorState.renameTrack(root.pendingRenameTrack, renameTrackField.text)
            root.pendingRenameTrack = -1
        }
        onRejected: root.pendingRenameTrack = -1
    }

    ThemedDialog {
        id: confirmDeleteTrack
        title: qsTr("Delete this track?")
        acceptText: qsTr("Delete track")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        readonly property int clipCount: {
            if (root.pendingDeleteTrack < 0 || root.pendingDeleteTrack >= root.tracks.length)
                return 0
            const clips = root.tracks[root.pendingDeleteTrack].clips
            return clips ? clips.length : 0
        }

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: confirmDeleteTrack.clipCount > 0
                  ? qsTr("This removes the track and its %n clips. You can undo afterwards.",
                         "", confirmDeleteTrack.clipCount)
                  : qsTr("This removes the empty track. You can undo afterwards.")
        }

        onAccepted: {
            if (root.pendingDeleteTrack >= 0)
                EditorState.removeTrack(root.pendingDeleteTrack)
            root.pendingDeleteTrack = -1
        }
        onRejected: root.pendingDeleteTrack = -1
    }

    // The one definition of a row's height, shared with TimelinePanel and AndroidTimeline. It
    // used to be duplicated in all three, which had to agree or the headers slid out of line
    // with their rows; the nested-lane rule made keeping three copies in step untenable.
    function trackHeight(index) {
        // Reading `tracks` is what gives callers' bindings a dependency to re-evaluate on —
        // EditorState.trackRowHeight is a plain call and carries none of its own.
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

    // Raise a row so N channel lanes each get a workable height. Only ever grows, and only
    // on enable: silently shrinking a row the user has already tuned is worse than leaving it
    // tall, and on disable they may well want the height they now have.
    //
    // Computed here rather than in C++ because the row-height metrics are owned by Theme and
    // passed *into* trackRowHeight — C++ deliberately hardcodes none of them.
    function growRowForChannelLanes(index, channels) {
        if (channels < 2)
            return
        const base = tracks[index].type === "audio" ? Theme.trackHeightAudio
                                                    : Theme.trackHeightVideo
        // The waveform sits below the header band, inside the selection ring.
        const chrome = Theme.clipHeaderBandHeight + 2 * Theme.clipSelectionRingWidth
        const wanted = (16 * channels + chrome) / base
        if (wanted > (tracks[index].heightScale || 1))
            EditorState.setTrackHeightScale(index, wanted)
    }

    // A nested lane has no header of its own: it belongs to the row above it, whose header
    // already names it.
    function trackOccupiesARow(index) {
        return index >= 0 && index < tracks.length && !tracks[index].isAdjustmentLane
    }

    // Video-track clip display modes, matching Track::ClipDisplay.
    function clipDisplayGlyph(mode) {
        if (mode === 0) return Theme.icons.film
        if (mode === 2) return Theme.icons.audioLines
        return Theme.icons.panelBottomDashed
    }

    function clipDisplayLabel(mode) {
        if (mode === 0) return qsTr("Video")
        if (mode === 2) return qsTr("Waveform")
        return qsTr("Video + waveform")
    }

    function isHeightPreset(index, scale) {
        return Math.abs((tracks[index].heightScale || 1) - scale) < 0.01
    }
    // VEDIT (조박사님): the per-track sizes are the same three heights as the corner switch
    // (큰/보통/작은 = clip size 1.0 / 0.75 / 0.5), whatever the corner is set to — a track's
    // "Large" is exactly as tall as every track under the corner's "Large".
    function isAbsoluteSize(index, size) {
        return Math.abs((tracks[index].heightScale || 1) * Theme.clipSizeScale - size) < 0.01
    }
    function setAbsoluteSize(index, size) {
        EditorState.setTrackHeightScale(index, size / Theme.clipSizeScale)
    }

    // Header button order: Video -> Video + waveform -> Waveform -> Video.
    function nextClipDisplay(mode) {
        return mode === 0 ? 1 : (mode === 1 ? 2 : 0)
    }

    // Single-letter stand-in for the type glyph plus name band, which do not fit a 72px
    // compact header. "V1"/"A2" is the fallback identification a phone gets until the track
    // has a custom name — see trackCompactLabel, which prefers that name when it's set.
    function trackTypeShortLabel(type) {
        if (type === "adjustment") return qsTr("FX");
        if (type === "audio") return qsTr("A");
        if (type === "text") return qsTr("T");
        if (type === "subtitle") return qsTr("S");
        if (type === "shape") return qsTr("G");
        return qsTr("V");
    }

    // Human label for a track type.
    function trackTypeLabel(type) {
        if (type === "adjustment") return qsTr("Adjustment");
        if (type === "audio") return qsTr("Audio");
        if (type === "text") return qsTr("Text");
        if (type === "subtitle") return qsTr("Subtitle");
        if (type === "shape") return qsTr("Graphic");
        return qsTr("Video");
    }

    // Track numbers are scoped to their media type rather than the absolute row.
    // A project with Video 1 followed by its first extracted audio lane should read
    // Audio 1, not Audio 2. Reordering mixed track types keeps each sequence natural.
    function trackTypeOrdinal(index) {
        if (index < 0 || index >= tracks.length)
            return 1
        const type = tracks[index].type
        var ordinal = 0
        for (var i = 0; i <= index; i++) {
            if (tracks[i].type === type)
                ordinal++
        }
        return Math.max(1, ordinal)
    }

    // VEDIT (조박사님, as in Vegas): a coloured strip at the front of each header, with the
    // track's number, so rows tell apart at a glance.
    readonly property real colorStripWidth: compact ? 6 : 36
    readonly property var trackStripColors: ["#f2c94c", "#6fcf97", "#9b8fd9", "#e08fb6",
                                             "#f0866e", "#f2a65a", "#56c2c4", "#6fa8f2"]
    // "V1", "T1", "A1" — the short code shown in the strip (Vegas-like, the same in every language).
    function trackStripCode(index) {
        const t = tracks[index] ? tracks[index].type : ""
        const code = t === "audio" ? "A" : t === "text" ? "T" : t === "subtitle" ? "S"
                   : t === "shape" ? "G" : t === "adjustment" ? "FX" : "V"
        return code + trackTypeOrdinal(index)
    }
    function trackRowNumber(index) {
        let n = 0
        for (let i = 0; i <= index && i < tracks.length; ++i)
            if (trackOccupiesARow(i))
                ++n
        return n
    }

    function trackRowTop(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return cursor
    }

    // Boundary the dragged row would be inserted at, 0..tracks.length. Note the
    // open upper end: the old version clamped to tracks.length - 1, so "after
    // the last track" was not expressible and a downward drag could not reach
    // the bottom slot.
    function trackInsertSlotAtY(y) {
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            const th = trackHeight(i)
            if (y < cursor + th / 2)
                return i
            cursor += th + Theme.trackGap
        }
        return tracks.length
    }

    // QList::move() destination for dragging `from` into that slot. Removing the
    // row first shifts everything after it up by one, so a downward move lands a
    // slot earlier than the raw boundary — without this the drag committed a
    // move after a single pixel, because a row's own grip already sits past its
    // own midpoint.
    function trackMoveTargetForSlot(from, slot) {
        const to = slot > from ? slot - 1 : slot
        return Math.max(0, Math.min(tracks.length - 1, to))
    }

    function clearTrackDrag() {
        draggingTrackFrom = -1
        draggingTrackTo = -1
        draggingTrackSlot = -1
    }

    Repeater {
        model: root.tracks.length
        delegate: Item {
            id: trackLabelRow

            Accessible.role: Accessible.Row
            Accessible.name: trackLabelRow.trackDisplayName
            // Read through root.tracks (not the EditorState
            // getters) so the toggles rebind on tracksChanged.
            readonly property bool trackMuted: root.tracks[index].muted === true
            readonly property bool trackHidden: root.tracks[index].hidden === true
            readonly property bool trackLocked: root.tracks[index].locked === true // VEDIT
            readonly property bool hasCustomName: !!root.tracks[index].name && root.tracks[index].name.length > 0 // VEDIT
            readonly property bool trackMagnet: root.tracks[index].magnet === true // VEDIT: 빈틈 닫기
            readonly property int clipDisplay: root.tracks[index].clipDisplay !== undefined
                                               ? root.tracks[index].clipDisplay : 1
            readonly property bool trackWaveform: clipDisplay === 2
            // Falls back to the type+position label until a custom name is set.
            readonly property string trackDisplayName:
                root.tracks[index].name && root.tracks[index].name.length > 0
                ? root.tracks[index].name
                : root.trackTypeLabel(root.tracks[index].type)
                  + " " + root.trackTypeOrdinal(index)
            // Same, but falling back to the short "V1"/"A2" form the compact header uses when
            // there's no custom name to show instead.
            readonly property string trackCompactLabel:
                root.tracks[index].name && root.tracks[index].name.length > 0
                ? root.tracks[index].name
                : root.trackTypeShortLabel(root.tracks[index].type)
                  + root.trackTypeOrdinal(index)
            width: root.labelsWidth
            // A lane draws inside its parent's row, so it gets no header of its own.
            visible: root.trackOccupiesARow(index)
            height: root.trackHeight(index)
                    + (index < root.tracks.length - 1 ? Theme.trackGap : 0)
            // Split deliberately: trackRowTop() walks every track ahead of this one, so binding
            // it straight into `y` re-ran that walk for every header on every scroll frame. The
            // walk now depends only on the track model; the per-frame part is one subtraction.
            readonly property real rowTop: root.trackRowTop(index)
            // Follows the timeline's vertical scroll so labels stay
            // aligned with their rows.
            y: rowTop - root.contentY
            opacity: root.draggingTrackFrom === index ? 0.45 : 1.0

            // Reorder drag. Covers the whole header rather than just the grip:
            // reaching for the header body is the instinctive gesture, and the
            // grip alone was a ~22px target that was easy to miss entirely.
            // Declared first so it sits below the toggle buttons and the
            // right-click menu, which keep their own presses.
            MouseArea {
                id: trackHeaderDrag
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                cursorShape: root.draggingTrackFrom === index ? Qt.SizeAllCursor
                                                              : Qt.ArrowCursor

                // A press is not yet a reorder: without a threshold, clicking a
                // header committed a move, because a row's grip already sits
                // past its own midpoint.
                property real pressY: 0
                property bool moved: false
                // Touch only: the hold was recognised, so this row is now draggable.
                // Letting go without moving asks for the context menu instead.
                property bool armed: false
                readonly property real threshold: 4

                // On touch the reorder waits for a hold. Arming on press meant any 4px
                // of finger travel reordered the tracks, and the header column could
                // never be used to scroll the timeline vertically.
                pressAndHoldInterval: 400
                preventStealing: root.touchMode ? armed : true

                property real pressX: 0 // VEDIT
                onPressed: (mouse) => {
                    EditorState.noteTrackClicked(index) // VEDIT: where a paste lands
                    pressX = mouse.x // VEDIT
                    pressY = mouse.y
                    moved = false
                    armed = false
                    if (root.touchMode)
                        return
                    root.draggingTrackFrom = index
                    root.draggingTrackTo = index
                    root.draggingTrackSlot = -1
                }
                onPressAndHold: (mouse) => {
                    if (!root.touchMode || moved)
                        return
                    pressY = mouse.y
                    armed = true
                    root.draggingTrackFrom = index
                    root.draggingTrackTo = index
                    root.draggingTrackSlot = -1
                    Haptics.pickUp()
                }
                onPositionChanged: (mouse) => {
                    if (root.draggingTrackFrom < 0)
                        return
                    if (!moved && Math.abs(mouse.y - pressY) < threshold)
                        return
                    moved = true
                    const local = mapToItem(root, mouse.x, mouse.y)
                    // Rows are drawn at trackRowTop(i) - contentY, so `local` is view
                    // space while trackInsertSlotAtY walks content offsets. Scrolled, the
                    // drop landed on whichever row happened to sit at that screen y.
                    root.draggingTrackSlot = root.trackInsertSlotAtY(local.y + root.contentY)
                    root.draggingTrackTo = root.trackMoveTargetForSlot(
                                               root.draggingTrackFrom,
                                               root.draggingTrackSlot)
                }
                onReleased: {
                    const wantsMenu = root.touchMode && armed && !moved
                    armed = false
                    if (wantsMenu) {
                        root.clearTrackDrag()
                        trackContextMenu.popup()
                        return
                    }
                    if (moved && root.draggingTrackFrom >= 0
                            && root.draggingTrackTo >= 0
                            && root.draggingTrackFrom !== root.draggingTrackTo) {
                        Haptics.confirm()
                        EditorState.moveTrack(root.draggingTrackFrom,
                                              root.draggingTrackTo)
                    } else {
                        Haptics.drop()
                        // VEDIT (조박사님): a plain click on the colour strip picks the whole track —
                        // Delete or the toolbar's bin then removes it.
                        if (!moved && pressX <= root.colorStripWidth)
                            EditorState.selectTrackRow(index)
                    }
                    root.clearTrackDrag()
                }
                onCanceled: {
                    armed = false
                    root.clearTrackDrag()
                }
            }

            // VEDIT (조박사님): the header row gets the same tint as its timeline row, so the
            // 3 px line between tracks runs through the headers too.
            Rectangle {
                width: parent.width
                height: root.trackHeight(index)
                z: -1
                color: Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g, Theme.panelAccent.b, 0.22)
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: root.trackHeight(index)
                color: Theme.panelBorder
            }

            Rectangle { // VEDIT: a picked track — a bright frame round its header
                visible: EditorState.selectedTrackRow === index
                anchors.left: parent.left
                width: parent.width
                height: root.trackHeight(index)
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 2
                border.color: Theme.primary
                z: 6
            }

            Rectangle { // VEDIT: the colour strip
                anchors.left: parent.left
                width: root.colorStripWidth
                height: root.trackHeight(index)
                radius: 2
                color: root.trackStripColors[(root.trackRowNumber(index) - 1) % root.trackStripColors.length]
                // VEDIT (조박사님): the video row's clip-display toggle lives here, under V1
                IconGlyph {
                    // VEDIT (조박사님): at the foot of the strip, 20 % larger again, black.
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    visible: !root.compact && root.tracks[index].type === "video" && root.trackHeight(index) >= 40
                    glyph: root.clipDisplayGlyph(trackLabelRow.clipDisplay)
                    iconSize: 17
                    iconColor: "#000000"
                    ThemedToolTip {
                        visible: stripDisplayMouse.containsMouse
                        text: qsTr("Clips show: %1 (click to change)")
                                  .arg(root.clipDisplayLabel(trackLabelRow.clipDisplay))
                    }
                    MouseArea {
                        id: stripDisplayMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Haptics.press()
                            EditorState.setTrackClipDisplay(
                                index, root.nextClipDisplay(trackLabelRow.clipDisplay))
                        }
                    }
                }
                // The reorder grip and the short code inside the strip: stacked on a tall row,
                // side by side on a short one.
                Column {
                    visible: !root.compact
                    anchors.centerIn: parent
                    spacing: 7 // 조박사님: more room between V1 and the icon below
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 1
                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.gripVertical
                            iconSize: 10
                            iconColor: "#2a2a2a"
                            HoverHandler { id: stripGripHover }
                            ThemedToolTip {
                                visible: stripGripHover.hovered && root.draggingTrackFrom < 0
                                text: qsTr("Drag the header to reorder this track")
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.trackStripCode(index)
                            color: "#1a1a1a"
                            font.family: Theme.fontFamily
                            font.pixelSize: 10 // 조박사님: 30% smaller than 14
                            font.bold: true
                        }
                    }
                    // VEDIT (조박사님): a sound row's stereo sides, under A1 — click to go round
                    // as recorded → L on both → R on both → L/R mix (the whole track).
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.tracks[index].type === "audio" && root.trackHeight(index) >= 40
                        readonly property int mode: root.tracks[index].channelMode || 0
                        text: ["L·R", "L→R", "R→L", "MIX", "L↔R"][mode]
                        color: "#1a1a1a" // 조박사님: black — yellow did not show on the strip
                        font.family: Theme.fontFamily
                        font.pixelSize: 9
                        font.bold: true
                        ThemedToolTip {
                            visible: stripChannelMouse.containsMouse
                            text: qsTr("Sound channels (whole track): %1 — click to change")
                                  .arg([qsTr("as recorded"), qsTr("left on both sides"), qsTr("right on both sides"),
                                        qsTr("left and right mixed"), qsTr("left and right swapped")][parent.mode])
                        }
                        MouseArea {
                            id: stripChannelMouse
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // VEDIT (조박사님, Vegas-style): a menu of the four choices, the current one ticked
                            onClicked: channelMenu.popup(stripChannelMouse, 0, stripChannelMouse.height)
                        }
                        ThemedContextMenu {
                            id: channelMenu
                            implicitWidth: 230
                            ThemedMenuItem {
                                // the tick as a leading mark: the item's own indicator sat on the first letters
                                text: ((root.tracks[index].channelMode || 0) === 0 ? "✓  " : "     ") + qsTr("As recorded (L·R)")
                                onTriggered: EditorState.setTrackChannelMode(index, 0)
                            }
                            ThemedMenuItem {
                                // the tick as a leading mark: the item's own indicator sat on the first letters
                                text: ((root.tracks[index].channelMode || 0) === 1 ? "✓  " : "     ") + qsTr("Left on both sides (L → R)")
                                onTriggered: EditorState.setTrackChannelMode(index, 1)
                            }
                            ThemedMenuItem {
                                // the tick as a leading mark: the item's own indicator sat on the first letters
                                text: ((root.tracks[index].channelMode || 0) === 2 ? "✓  " : "     ") + qsTr("Right on both sides (R → L)")
                                onTriggered: EditorState.setTrackChannelMode(index, 2)
                            }
                            ThemedMenuItem {
                                // the tick as a leading mark: the item's own indicator sat on the first letters
                                text: ((root.tracks[index].channelMode || 0) === 3 ? "✓  " : "     ") + qsTr("Mix left and right (L/R mix)")
                                onTriggered: EditorState.setTrackChannelMode(index, 3)
                            }
                            ThemedMenuItem {
                                text: ((root.tracks[index].channelMode || 0) === 4 ? "✓  " : "     ") + qsTr("Swap left and right (L ↔ R)")
                                onTriggered: EditorState.setTrackChannelMode(index, 4)
                            }
                        }
                    }
                }
            }

            // Drag affordance — left-aligned reorder grip.
            IconGlyph {
                visible: root.compact // VEDIT: the grip sits in the colour strip on a full header
                anchors.left: parent.left
                anchors.leftMargin: root.colorStripWidth + 4
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                glyph: Theme.icons.gripVertical
                iconSize: root.compact ? 12 : 14
                iconColor: trackDragMouse.hovered || root.draggingTrackFrom === index
                           ? Theme.panelForeground : Theme.mutedForeground

                Behavior on iconColor {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                ThemedToolTip {
                    visible: trackHeaderDrag.containsMouse && root.draggingTrackFrom < 0
                    text: qsTr("Drag the header to reorder this track")
                }

                // Hover affordance only — the drag lives on the whole header
                // row below, so the grab target is not a 14px icon.
                HoverHandler { id: trackDragMouse }
            }

            Row {
                id: trackIconRow
                anchors.right: parent.right
                anchors.rightMargin: root.compact ? 4 : 12
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                // Each toggle's hit area overhangs its glyph by 4 either side, so a 4px
                // gap left the mute and hide targets touching: half the visible gap fired
                // the wrong one, and both silently drop the track from render and export.
                // 12 leaves a 4px dead band between them.
                spacing: root.touchMode ? 12 : (root.compact ? 4 : 8)

                IconGlyph {
                    visible: root.tracks[index].type === "video"
                             || root.tracks[index].type === "audio"
                    glyph: trackLabelRow.trackMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: trackLabelRow.trackMuted ? Theme.destructive : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    // The tooltip above never renders on touch, so the same string has to
                    // reach tooling and screen readers through the accessible name instead.
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: trackLabelRow.trackMuted ? qsTr("Unmute track") : qsTr("Mute track")
                    Accessible.checked: trackLabelRow.trackMuted
                    Accessible.onToggleAction: muteMouse.clicked(null)

                    ThemedToolTip {
                        visible: muteMouse.containsMouse
                        text: trackLabelRow.trackMuted ? qsTr("Unmute track") : qsTr("Mute track")
                    }

                    MouseArea {
                        id: muteMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Haptics.toggle(!trackLabelRow.trackMuted)
                            EditorState.setTrackMuted(index, !trackLabelRow.trackMuted)
                        }
                    }
                }

                // Stacked per-channel waveforms. Offered only where there is something to
                // split: mono and stereo-downmix material has one lane either way, and the
                // channel count is known because an on-screen track has decoded a block.
                IconGlyph {
                    id: channelLanesToggle
                    // trackMaxChannelCount has no notify of its own and the count only lands
                    // once a block has decoded, so depend on `tracks` to re-evaluate then.
                    readonly property int laneChannels: {
                        const dep = root.tracks.length
                        return EditorState.trackMaxChannelCount(index)
                    }
                    readonly property bool lanesOn:
                        root.tracks[index].showChannelWaveforms === true

                    visible: (root.tracks[index].type === "audio"
                              || (root.tracks[index].type === "video"
                                  && trackLabelRow.trackWaveform))
                             && laneChannels > 1
                    glyph: Theme.icons.split
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: lanesOn ? Theme.primary : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    Behavior on iconColor {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    ThemedToolTip {
                        visible: channelLanesMouse.containsMouse
                        text: channelLanesToggle.lanesOn
                              ? qsTr("Show one combined waveform")
                              : qsTr("Show each channel separately (%1)")
                                    .arg(channelLanesToggle.laneChannels)
                    }

                    MouseArea {
                        id: channelLanesMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const on = !channelLanesToggle.lanesOn
                            Haptics.toggle(on)
                            EditorState.setTrackShowChannelWaveforms(index, on)
                            if (on)
                                root.growRowForChannelLanes(
                                    index, channelLanesToggle.laneChannels)
                        }
                    }
                }

                IconGlyph {
                    visible: root.tracks[index].type === "video"
                             || root.tracks[index].type === "text"
                             || root.tracks[index].type === "subtitle"
                             || root.tracks[index].type === "shape"
                    glyph: trackLabelRow.trackHidden ? Theme.icons.eyeOff : Theme.icons.eye
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: trackLabelRow.trackHidden ? Theme.destructive : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: hideMouse.containsMouse
                        text: trackLabelRow.trackHidden ? qsTr("Show track") : qsTr("Hide track")
                    }

                    Accessible.role: Accessible.CheckBox
                    Accessible.name: trackLabelRow.trackHidden ? qsTr("Show track") : qsTr("Hide track")
                    Accessible.checked: trackLabelRow.trackHidden
                    Accessible.onToggleAction: hideMouse.clicked(null)

                    MouseArea {
                        id: hideMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Haptics.toggle(!trackLabelRow.trackHidden)
                            EditorState.setTrackHidden(index, !trackLabelRow.trackHidden)
                        }
                    }
                }

                // VEDIT: 트랙 잠금 — locked rows cannot be grabbed, cut or dropped onto.
                IconGlyph {
                    glyph: trackLabelRow.trackLocked ? Theme.icons.lock : Theme.icons.lockOpen
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: trackLabelRow.trackLocked ? Theme.primary : Theme.mutedForeground
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: lockMouse.containsMouse
                        text: trackLabelRow.trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
                    }

                    Accessible.role: Accessible.CheckBox
                    Accessible.name: trackLabelRow.trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
                    Accessible.checked: trackLabelRow.trackLocked

                    MouseArea {
                        id: lockMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setTrackLocked(index, !trackLabelRow.trackLocked)
                    }
                }

                // VEDIT: 빈틈 닫기 — per track (CapCut keeps it on its main track; here every track
                // has its own switch). On: the track keeps no gaps.
                IconGlyph {
                    visible: !root.tracks[index].isAdjustmentLane
                    glyph: Theme.icons.foldHorizontal
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: trackLabelRow.trackMagnet ? Theme.primary : Theme.mutedForeground
                    opacity: trackLabelRow.trackMagnet ? 1.0 : 0.55
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: magnetMouse.containsMouse
                        text: trackLabelRow.trackMagnet ? qsTr("Close gaps on this track: on (click to turn off)")
                                                        : qsTr("Close gaps on this track: off (click to turn on)")
                    }

                    Accessible.role: Accessible.CheckBox
                    Accessible.name: qsTr("Close gaps on this track")
                    Accessible.checked: trackLabelRow.trackMagnet

                    MouseArea {
                        id: magnetMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.setTrackMagnet(index, !trackLabelRow.trackMagnet)
                    }
                }

                // Cycles the track's clips through filmstrip / filmstrip + waveform bar /
                // waveform.
                IconGlyph {
                    visible: false // VEDIT: moved into the colour strip so every row's icons line up
                    glyph: root.clipDisplayGlyph(trackLabelRow.clipDisplay)
                    iconSize: 13 // VEDIT: 20% smaller
                    iconColor: trackLabelRow.clipDisplay === 0 ? Theme.mutedForeground : Theme.primary
                    anchors.verticalCenter: parent.verticalCenter

                    ThemedToolTip {
                        visible: waveMouse.containsMouse
                        text: qsTr("Clips show: %1 (click to change)")
                                  .arg(root.clipDisplayLabel(trackLabelRow.clipDisplay))
                    }

                    MouseArea {
                        id: waveMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Haptics.press()
                            EditorState.setTrackClipDisplay(
                                index, root.nextClipDisplay(trackLabelRow.clipDisplay))
                        }
                    }
                }
            }

            // Text/subtitle rows are too short for the name band above (it needs both a
            // top-anchored name and a vertically centered icon row without the two
            // overlapping), so the name sits inline here instead, between the grip and the
            // type/toggle icons.
            // VEDIT (조박사님): this row's own size — small / medium / large, stacked
            // vertically right of the colour strip (the corner switch above sets every row).
            Column {
                id: rowSizeColumn
                readonly property int rowIndex: index // the track's, not the size buttons' own index
                visible: !root.compact && !root.tracks[index].isAdjustmentLane
                anchors.left: parent.left
                anchors.leftMargin: root.colorStripWidth + 3
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3 // VEDIT (조박사님): a little more room between them
                z: 5
                Repeater {
                    // 10 % larger (조박사님)
                    model: [{ "size": 1.0, "bar": 7, "label": qsTr("Large") },
                            { "size": 0.75, "bar": 4.5, "label": qsTr("Medium") },
                            { "size": 0.5, "bar": 2.5, "label": qsTr("Small") }]
                    delegate: Item {
                        required property var modelData
                        readonly property bool active: root.isAbsoluteSize(rowSizeColumn.rowIndex, modelData.size)
                        width: 15
                        // 50 % taller hit areas (조박사님: hard to hit, and there is room)
                        height: Math.max(12, Math.min(16.5, (root.trackHeight(rowSizeColumn.rowIndex) - 8) / 3))
                        Rectangle {
                            anchors.centerIn: parent
                            width: 11
                            height: Math.min(parent.height - 1, modelData.bar)
                            radius: 1
                            color: parent.active ? Theme.primary
                                                 : (rowSizeMouse.containsMouse ? Theme.foreground : Theme.mutedForeground)
                        }
                        MouseArea {
                            id: rowSizeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setAbsoluteSize(rowSizeColumn.rowIndex, modelData.size)
                        }
                        ToolTip.visible: rowSizeMouse.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("This track's height") + " — " + modelData.label
                    }
                }
            }

            Text {
                // VEDIT (조박사님): "영상 1" / "소리 1" repeat the strip's V1 / A1 — only a name the
                // user gave the track is shown.
                visible: !root.compact && root.trackHeight(index) < 40 && trackLabelRow.hasCustomName
                anchors.left: parent.left
                anchors.leftMargin: root.colorStripWidth + 8 // VEDIT: right after the colour strip
                anchors.right: trackIconRow.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                text: trackLabelRow.trackDisplayName
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                elide: Text.ElideRight
            }

            // Compact stand-in for the glyph and the name band: sits in the gap
            // between the grip and the mute/hide pair, which is the only free
            // horizontal space the compact header has. Shows the custom name (elided) once
            // one is set — the context menu's Rename is reachable here too (long-press),
            // and a rename that changes nothing visible would look like it silently failed.
            Text {
                visible: root.compact
                anchors.left: parent.left
                anchors.leftMargin: 18 + root.colorStripWidth // VEDIT
                anchors.right: trackIconRow.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: index < root.tracks.length - 1 ? -Theme.trackGap / 2 : 0
                text: trackLabelRow.trackCompactLabel
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                elide: Text.ElideRight
            }

            // Clip display toggle for compact headers. Parked in the bottom-left
            // corner rather than in the icon row, which a video track has no room left
            // in — and only video rows (65px) are tall enough for a second corner.
            IconGlyph {
                visible: root.compact && root.tracks[index].type === "video"
                anchors.left: parent.left
                anchors.leftMargin: 4 + root.colorStripWidth // VEDIT
                anchors.bottom: parent.bottom
                anchors.bottomMargin: (index < root.tracks.length - 1 ? Theme.trackGap : 0) + 4
                glyph: root.clipDisplayGlyph(trackLabelRow.clipDisplay)
                iconSize: Theme.iconSizeMd
                iconColor: trackLabelRow.clipDisplay === 0 ? Theme.mutedForeground : Theme.primary

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    onClicked: {
                        Haptics.press()
                        EditorState.setTrackClipDisplay(
                            index, root.nextClipDisplay(trackLabelRow.clipDisplay))
                    }
                }
            }

            // Track name. Nothing in the header used to say which
            // track this was beyond the type glyph.
            Text {
                anchors.left: parent.left
                anchors.leftMargin: root.colorStripWidth + 8 // VEDIT: right after the colour strip
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingMd
                text: trackLabelRow.trackDisplayName
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                elide: Text.ElideRight
                visible: !root.compact && root.trackHeight(index) >= 40 && trackLabelRow.hasCustomName // VEDIT
            }

            // Track context menu. Right-click on desktop; on touch it is opened from the
            // reorder area below, which sits under the mute/hide toggles so those keep
            // their own presses. Accepting LeftButton here would put this on top of
            // everything and swallow the lot.
            MouseArea {
                id: trackMenuArea
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: trackContextMenu.popup()

                ThemedContextMenu {
                    id: trackContextMenu

                    ThemedMenuItem {
                        text: qsTr("Rename…")
                        icon.name: Theme.icons.pencil
                        onTriggered: {
                            root.pendingRenameTrack = index
                            // Custom name only — pre-filling the type+position fallback
                            // ("Video 1") would persist it as a real name on an unchanged
                            // confirm, then stick after the track is reordered.
                            renameTrackField.text = root.tracks[index].name || ""
                            renameTrackDialog.open()
                        }
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: trackLabelRow.trackMuted ? qsTr("Unmute track")
                                                       : qsTr("Mute track")
                        icon.name: trackLabelRow.trackMuted ? Theme.icons.volumeHigh
                                                            : Theme.icons.volumeOff
                        onTriggered: EditorState.setTrackMuted(index, !trackLabelRow.trackMuted)
                    }
                    ThemedMenuItem {
                        text: trackLabelRow.trackHidden ? qsTr("Show track")
                                                        : qsTr("Hide track")
                        icon.name: trackLabelRow.trackHidden ? Theme.icons.eye
                                                             : Theme.icons.eyeOff
                        onTriggered: EditorState.setTrackHidden(index, !trackLabelRow.trackHidden)
                    }
                    ThemedMenuSeparator {
                        visible: root.tracks[index].type === "video"
                    }
                    // The active entry swaps its icon for a tick, like the header's
                    // Settings menu.
                    ThemedMenuItem {
                        visible: root.tracks[index].type === "video"
                        text: root.clipDisplayLabel(0)
                        icon.name: trackLabelRow.clipDisplay === 0 ? Theme.icons.check
                                                                   : root.clipDisplayGlyph(0)
                        onTriggered: EditorState.setTrackClipDisplay(index, 0)
                    }
                    ThemedMenuItem {
                        visible: root.tracks[index].type === "video"
                        text: root.clipDisplayLabel(1)
                        icon.name: trackLabelRow.clipDisplay === 1 ? Theme.icons.check
                                                                   : root.clipDisplayGlyph(1)
                        onTriggered: EditorState.setTrackClipDisplay(index, 1)
                    }
                    ThemedMenuItem {
                        visible: root.tracks[index].type === "video"
                        text: root.clipDisplayLabel(2)
                        icon.name: trackLabelRow.clipDisplay === 2 ? Theme.icons.check
                                                                   : root.clipDisplayGlyph(2)
                        onTriggered: EditorState.setTrackClipDisplay(index, 2)
                    }
                    ThemedMenuSeparator {}
                    ThemedContextMenu {
                        title: qsTr("Track height")

                        ThemedMenuItem {
                            text: qsTr("Short")
                            icon.name: root.isAbsoluteSize(index, 0.5)
                                       ? Theme.icons.check : Theme.icons.listChevronsDownUp
                            onTriggered: root.setAbsoluteSize(index, 0.5)
                        }
                        ThemedMenuItem {
                            text: qsTr("Tall")
                            icon.name: root.isAbsoluteSize(index, 0.75)
                                       ? Theme.icons.check : Theme.icons.list
                            onTriggered: root.setAbsoluteSize(index, 0.75)
                        }
                        ThemedMenuItem {
                            text: qsTr("Taller")
                            icon.name: root.isAbsoluteSize(index, 1.0)
                                       ? Theme.icons.check : Theme.icons.listChevronsUpDown
                            onTriggered: root.setAbsoluteSize(index, 1.0)
                        }
                        ThemedMenuSeparator {
                            visible: !root.touchMode
                        }
                        ThemedMenuItem {
                            visible: !root.touchMode
                            enabled: false
                            text: qsTr("Scroll over the header to fine-tune")
                            icon.name: Theme.icons.info
                        }
                    }
                    // Desktop fine-tunes a lane by wheeling over its header. Touch has no
                    // wheel, so the same nudge is offered explicitly.
                    ThemedMenuItem {
                        visible: root.touchMode
                        text: qsTr("Taller row")
                        icon.name: Theme.icons.chevronUp
                        enabled: root.tracks[index].heightScale < EditorState.trackHeightScaleMax()
                        onTriggered: EditorState.nudgeTrackHeightScale(index, 1)
                    }
                    ThemedMenuItem {
                        visible: root.touchMode
                        text: qsTr("Shorter row")
                        icon.name: Theme.icons.chevronDown
                        enabled: root.tracks[index].heightScale > EditorState.trackHeightScaleMin()
                        onTriggered: EditorState.nudgeTrackHeightScale(index, -1)
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: qsTr("Delete track")
                        icon.name: Theme.icons.trash
                        onTriggered: {
                            root.pendingDeleteTrack = index
                            confirmDeleteTrack.open()
                        }
                    }
                }
            }

            // DAW-style lane zoom: wheel over this header grows/shrinks only
            // this track, so a music track can be made tall for waveform work
            // without zooming the whole timeline. NoButton keeps clicks, the
            // reorder drag and the context menu working underneath.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                z: 40
                onWheel: (wheel) => {
                    // Modified wheel belongs to the timeline's zoom/pan.
                    if (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                        wheel.accepted = false
                        return
                    }
                    const dy = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                        : wheel.pixelDelta.y
                    if (dy === 0)
                        return
                    EditorState.nudgeTrackHeightScale(index, dy > 0 ? 1 : -1)
                }
            }
        }
    }

    // Insertion line while reordering tracks.
    Rectangle {
        visible: root.draggingTrackSlot >= 0 && root.draggingTrackTo >= 0
                 && root.draggingTrackFrom !== root.draggingTrackTo
        width: parent.width - 8
        height: 2
        radius: 1
        x: 4
        color: Theme.primary
        z: 10
        // Drawn at the insertion boundary itself, in the same frame as the
        // header rows — without -contentY it drifts off the boundary as soon as
        // the tracks are scrolled.
        y: {
            const slot = root.draggingTrackSlot
            if (slot < 0)
                return 0
            if (slot >= root.tracks.length) {
                var last = root.tracks.length - 1
                while (last > 0 && !root.trackOccupiesARow(last))
                    last--
                return root.trackRowTop(last) + root.trackHeight(last)
                       - root.contentY - 1
            }
            return root.trackRowTop(slot) - root.contentY - 1
        }
    }

    // Drag to widen the header column; double-click restores the default.
    MouseArea {
        id: widthHandle
        visible: root.resizable
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        z: 50
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        onPositionChanged: (mouse) => {
            if (pressed)
                EditorState.trackLabelsWidth = x + mouse.x + width / 2
        }
        onDoubleClicked: EditorState.trackLabelsWidth = Theme.trackLabelsWidth

        Rectangle {
            anchors.right: parent.right
            width: 2
            height: parent.height
            color: Theme.primary
            opacity: widthHandle.pressed || widthHandle.containsMouse ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }
    }
}
