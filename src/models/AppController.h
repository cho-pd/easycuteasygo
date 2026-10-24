#pragma once

#include "core/EffectStackStore.h"
#include "core/Project.h"
#include "core/TextAnimationPreset.h"
#include "core/TimelineOps.h"
#include "core/Time.h"
#include "engine/AudioOnsets.h"
#include "engine/SceneDetect.h"
#include "engine/FilmstripTileCache.h"
#include "engine/MediaWaveform.h"
#include "engine/WaveformBlockCache.h"
#include "engine/ProjectBundle.h"
#include "engine/RvmMatter.h"
#include "engine/Sam2Segmenter.h"
#include "ClipListModel.h"
#include "TimelineClipsModel.h"
#include "TimelineModel.h"
#include "models/AssetLibrary.h"
#include "models/BinFolderListModel.h"
#include "models/CloudProviders.h"

#include <QElapsedTimer> // VEDIT
#include <QDateTime> // VEDIT
#include <QAtomicInt>
#include <QCursor>
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QMediaDevices>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QUndoStack>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <QProcess>
#include <QMap>

#include <memory>
#include <optional>

struct EffectTemplateEntry;

class QTimer;
class AddonManager;
class MarketClient;
class JobRegistry;

namespace drift::mcp {
class McpServer;
}
namespace drift {
struct MediaEditSpec;
}

#include "playback/ClipPreviewPlayer.h"
#include "playback/PlaybackEngine.h"

// QML-facing controller over the core project model and undo stack.
namespace drift { class VoiceRecorder; struct LoudnessResult; } // VEDIT

struct AudioSyncJob; // VEDIT: 소리로 맞추기 (AppController.cpp)
class AppController : public QObject
{
    // Segmentation completion is normally reached only through a finished worker, which needs a
    // real backend installed. The test drives it directly instead.
    friend class EditorStateTest;

    Q_OBJECT

    Q_PROPERTY(AssetLibrary *assetLibrary READ assetLibrary CONSTANT)
    Q_PROPERTY(CloudProviders *cloudProviders READ cloudProviders CONSTANT)
    Q_PROPERTY(BinFolderListModel *binFolderModel READ binFolderModel CONSTANT)
    // Which bin folder is currently being viewed; empty = bin root. Transient navigation state —
    // not persisted, not undoable, same treatment as mediaGridMode's touch-only sibling.
    Q_PROPERTY(QString currentBinFolderId READ currentBinFolderId WRITE setCurrentBinFolderId
                   NOTIFY currentBinFolderIdChanged)
    // True while importFolder's off-thread directory walk is running, so the bin can raise its
    // progress overlay over a slow tree (a big hierarchy, or a Flatpak document-portal mount).
    Q_PROPERTY(bool importingFolder READ importingFolder NOTIFY importingFolderChanged)
    Q_PROPERTY(TimelineModel *timelineModel READ timelineModel CONSTANT)
    Q_PROPERTY(ClipListModel *clipListModel READ clipListModel CONSTANT)
    Q_PROPERTY(PlaybackEngine *playback READ playback CONSTANT)
    // Output devices to choose between, each {id, label}; the first entry has an empty id and
    // means "whatever the system default is at the time", which is also the default choice.
    Q_PROPERTY(QVariantList audioOutputDevices READ audioOutputDevices NOTIFY audioOutputDevicesChanged)
    Q_PROPERTY(QString audioOutputDeviceId READ audioOutputDeviceId WRITE setAudioOutputDeviceId
                   NOTIFY audioOutputDeviceIdChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    // A change token for the bindings that read `tracks` only to re-evaluate on an edit. Reading
    // the list for that rebuilt every clip in the project into QVariantMaps, once per binding,
    // per emission — with two dozen such bindings across the desktop and touch shells.
    Q_PROPERTY(quint32 tracksRevision READ tracksRevision NOTIFY tracksChanged)
    // For the bindings that only ask how many tracks there are, or whether there are any.
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
    // "Is the timeline empty?" — asked by the empty-state copy in both shells, which used to
    // answer it by serialising the project and looping over every track.
    Q_PROPERTY(int clipCount READ clipCount NOTIFY tracksChanged)
    // Whether the touch shell's grow/shrink-all-lanes buttons have anywhere left to go, for their
    // enabled state. Properties rather than invokables so QML gets real bindings: an invokable
    // would have to be given a dependency to re-evaluate on, and the only one available is
    // `tracks`, which rebuilds a QVariantList of every clip in the project each time it is read.
    Q_PROPERTY(bool canGrowTrackHeights READ canGrowTrackHeights NOTIFY tracksChanged)
    Q_PROPERTY(bool canShrinkTrackHeights READ canShrinkTrackHeights NOTIFY tracksChanged)
    Q_PROPERTY(double playheadSeconds READ playheadSeconds WRITE setPlayheadSeconds NOTIFY playheadSecondsChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY tracksChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    // VEDIT (조박사님, Vegas volume envelope): the volume keyframe picked on an audio clip's
    // envelope — {clipId, seconds}, empty for none. Delete removes it.
    Q_PROPERTY(QVariantMap selectedVolumeKey READ selectedVolumeKey NOTIFY selectedVolumeKeyChanged)
    // VEDIT (조박사님): a whole track picked from its header's colour strip — Delete (or the
    // toolbar's bin) then removes the track. -1 for none; picking a clip drops it.
    Q_PROPERTY(int selectedTrackRow READ selectedTrackRow NOTIFY selectedTrackRowChanged)
    // VEDIT: 임시 렌더링 stretches for the ruler band: [{start, end, ready}] in seconds.
    Q_PROPERTY(QVariantList renderCacheSegments READ renderCacheSegments NOTIFY renderCacheChanged)
    Q_PROPERTY(bool previewDragActive READ previewDragActive NOTIFY previewDragActiveChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapEnabledChanged)
    Q_PROPERTY(bool rippleEnabled READ rippleEnabled WRITE setRippleEnabled NOTIFY rippleEnabledChanged)
    Q_PROPERTY(bool allowClipOverlap READ allowClipOverlap WRITE setAllowClipOverlap NOTIFY allowClipOverlapChanged)
  // Per-project UI prefs (serialized with the .drift file, not global QSettings).
    Q_PROPERTY(bool mediaGridMode READ mediaGridMode WRITE setMediaGridMode NOTIFY mediaGridModeChanged)
    // "grid" | "list" | "tree". mediaGridMode above is the older two-state view of the same
    // setting, kept because MCP and saved projects already speak it.
    Q_PROPERTY(QString mediaViewMode READ mediaViewMode WRITE setMediaViewMode NOTIFY mediaViewModeChanged)
    // App-wide theme preference, backed by QSettings("ui/darkMode"). Until the user
    // toggles once, darkModeOverridden is false and the UI follows the OS colour
    // scheme live; after that the stored choice wins on every launch.
    Q_PROPERTY(bool darkModeOverridden READ darkModeOverridden NOTIFY darkModePreferenceChanged)
    Q_PROPERTY(bool darkModePreferred READ darkModePreferred NOTIFY darkModePreferenceChanged)
    // Editor workspace arrangement. In the landscape workspace the preview sits in the
    // three-pane top row, so its size is bounded by that row's *height* — a 9:16 canvas
    // ends up postage-stamp small. The portrait workspace gives the preview a full-height
    // column beside the whole editing stack instead. Follows the canvas orientation until
    // the user picks one explicitly, after which the stored choice wins on every launch —
    // the same override rule as the theme above, backed by QSettings("ui/workspaceLayout").
    // The effective layout is resolved in QML so it can track both signals at once.
    Q_PROPERTY(bool projectPortrait READ projectPortrait NOTIFY tracksChanged)
    Q_PROPERTY(bool workspaceLayoutOverridden READ workspaceLayoutOverridden
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(QString workspaceLayoutPreferred READ workspaceLayoutPreferred
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(bool autoKeyEnabled READ autoKeyEnabled WRITE setAutoKeyEnabled NOTIFY autoKeyEnabledChanged)
    // On by default. Worth turning off on a very long timeline: the strip repaints every clip
    // in the project on every edit.
    Q_PROPERTY(bool timelineOverviewVisible READ timelineOverviewVisible
                   WRITE setTimelineOverviewVisible NOTIFY timelineOverviewVisibleChanged)
    // VEDIT: one size for every clip row — 0 small, 1 medium, 2 large (the default). Video and
    // sound rows keep the same proportion at every size.
    Q_PROPERTY(int timelineClipSize READ timelineClipSize WRITE setTimelineClipSize
                   NOTIFY timelineClipSizeChanged)
    Q_PROPERTY(qreal trackLabelsWidth READ trackLabelsWidth
                   WRITE setTrackLabelsWidth NOTIFY trackLabelsWidthChanged)
    // Timeline toolbar layout: action ids shown as buttons, then the ones in its More menu.
    // Empty means the QML defaults; ids are validated on the QML side, which owns the registry.
    Q_PROPERTY(QStringList timelineToolbarItems READ timelineToolbarItems
                   NOTIFY timelineToolbarLayoutChanged)
    Q_PROPERTY(QStringList timelineMenuItems READ timelineMenuItems
                   NOTIFY timelineToolbarLayoutChanged)
    // Opt-in: on launch, restore the last open project (saved .drift or unsaved recovery snapshot).
    Q_PROPERTY(bool reopenLastProject READ reopenLastProject WRITE setReopenLastProject NOTIFY reopenLastProjectChanged)
    // Preview zero-copy import: VAAPI dma-buf on Linux, D3D11 interop on Windows. Takes effect
    // after restart; hidden when this machine has no decode backend for either.
    Q_PROPERTY(bool vaapiZeroCopy READ vaapiZeroCopy WRITE setVaapiZeroCopy NOTIFY vaapiZeroCopyChanged)
    Q_PROPERTY(bool playbackBenchmarkRunning READ playbackBenchmarkRunning NOTIFY playbackBenchmarkRunningChanged)
    Q_PROPERTY(bool vaapiZeroCopySupported READ vaapiZeroCopySupported CONSTANT)
    // Android MediaCodec zero-copy preview. Same shape and the same caveat as the VAAPI pair
    // above: a driver can import the surface and still sample it wrongly, which shows up as a
    // corrupt preview with nothing to catch it — so it is opt-in and takes effect on restart.
    Q_PROPERTY(bool mediaCodecZeroCopy READ mediaCodecZeroCopy WRITE setMediaCodecZeroCopy NOTIFY
                   mediaCodecZeroCopyChanged)
    Q_PROPERTY(bool mediaCodecZeroCopySupported READ mediaCodecZeroCopySupported CONSTANT)
    // Hybrid-graphics Windows laptops: which GPU Drift asks to run on — "auto", "integrated" or
    // "discrete". The driver picks the GPU when it loads, so this takes effect on the next
    // launch. Hidden on single-GPU machines and off Windows.
    Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY preferredGpuChanged)
    Q_PROPERTY(bool gpuPreferenceSupported READ gpuPreferenceSupported CONSTANT)
    Q_PROPERTY(bool invertTimelineScroll READ invertTimelineScroll WRITE setInvertTimelineScroll
                   NOTIFY invertTimelineScrollChanged)
    // Session-only localhost MCP for agents. Off at every launch, unless mcpStartOnLaunch
    // opts back in.
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpRunningChanged)
    Q_PROPERTY(bool mcpRunning READ mcpRunning NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpUrl READ mcpUrl NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpToken READ mcpToken NOTIFY mcpRunningChanged)
    Q_PROPERTY(int mcpPort READ mcpPort NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpError READ mcpError NOTIFY mcpErrorChanged)
    Q_PROPERTY(QString mcpCursorSnippet READ mcpCursorSnippet NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpClaudeCommand READ mcpClaudeCommand NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpStdioSnippet READ mcpStdioSnippet NOTIFY mcpRunningChanged)
    // Persisted opt-in: start the MCP server at launch instead of leaving it off.
    // setMcpEnabled(false) — a manual "turn access off" — clears this, so re-enabling
    // access always starts from an explicit, un-opted-in state rather than quietly
    // carrying an old intent to auto-start past the point the user turned access off.
    Q_PROPERTY(bool mcpStartOnLaunch READ mcpStartOnLaunch WRITE setMcpStartOnLaunch
                   NOTIFY mcpStartOnLaunchChanged)
    // App-wide interface language, QSettings("ui/language"). Empty means follow the OS locale.
    // "en" is the source catalog (no .qm). Other codes match i18n/drift_<code>.qm.
    // needsUiLanguagePrompt is true only on a brand-new install, before the first-launch chooser
    // (or a later language pick from the header / Android Settings) has written ui/languageChosen.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QVariantList uiLanguages READ uiLanguages NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool needsUiLanguagePrompt READ needsUiLanguagePrompt NOTIFY uiLanguageChanged)
    // Extra UI scale on top of the OS display scale. QSettings("ui/scale"), 1.0..2.0 in
    // 0.25 steps. Applied as QT_SCALE_FACTOR before QApplication; a change needs a restart.
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(double appliedUiScale READ appliedUiScale CONSTANT)
    Q_PROPERTY(bool uiScaleNeedsRestart READ uiScaleNeedsRestart NOTIFY uiScaleChanged)
    // The keyframe strip draws every animated property of the selected clip. This is the subset
    // the user has folded away: a view filter only — hiding a curve never changes what renders.
    Q_PROPERTY(QStringList keyframeGraphHiddenProperties READ keyframeGraphHiddenProperties
                   NOTIFY keyframeGraphVisibilityChanged)
    // Detected beats / onsets for the keyframe strip. Transient analysis state — never
    // saved with the project, cleared whenever the underlying audio could have moved.
    Q_PROPERTY(QVariantMap beatAnalysis READ beatAnalysis NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool beatAnalysisRunning READ beatAnalysisRunning NOTIFY beatAnalysisChanged)
    // The grid and the transients are independently shown and snapped to. Both come out of
    // one analysis pass — the tempo is derived from the same onset envelope — so these
    // gate display and snapping, not the DSP.
    Q_PROPERTY(bool beatGridVisible READ beatGridVisible WRITE setBeatGridVisible
                   NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool onsetsVisible READ onsetsVisible WRITE setOnsetsVisible
                   NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool subtitleEditing READ subtitleEditing WRITE setSubtitleEditing NOTIFY subtitleEditingChanged)
    Q_PROPERTY(int selectedSubtitleCue READ selectedSubtitleCue WRITE setSelectedSubtitleCue
                   NOTIFY selectedSubtitleCueChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY undoStackChanged)
    Q_PROPERTY(bool redoAvailable READ redoAvailable NOTIFY undoStackChanged)
    Q_PROPERTY(bool exportInProgress READ exportInProgress NOTIFY exportInProgressChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportProgressChanged)
    // VEDIT: "남은 시간 약 2분 · 지금 34 MB" while rendering, "파일 크기 120 MB · 3분 12초 걸림" after.
    Q_PROPERTY(QString exportStatusText READ exportStatusText NOTIFY exportProgressChanged)
    Q_PROPERTY(bool canShareExport READ canShareExport NOTIFY canShareExportChanged)
    Q_PROPERTY(bool subtitleGenerating READ subtitleGenerating NOTIFY subtitleGeneratingChanged)
    // Id of the asset whose replacement is being probed, empty when idle. Only that one bin row
    // goes busy: the rest of the panel stays usable, and the wait belongs to the row the user
    // right-clicked.
    Q_PROPERTY(QString replacingAssetId READ replacingAssetId NOTIFY replacingAssetIdChanged)
    // True while a bin-row crop/trim is encoding. Progress belongs to MediaPreviewWindow.
    Q_PROPERTY(bool editingAsset READ editingAsset NOTIFY assetEditChanged)
    Q_PROPERTY(double assetEditProgress READ assetEditProgress NOTIFY assetEditChanged)
    Q_PROPERTY(QString assetEditStatus READ assetEditStatus NOTIFY assetEditChanged)
    // The running (or, once it finishes, the last) asset edit job is a frame-rate conversion
    // rather than a crop, so the finish can be worded for it. Also carries its name while it runs,
    // since a conversion has no preview window to show progress in.
    Q_PROPERTY(bool assetEditIsConversion READ assetEditIsConversion NOTIFY assetEditChanged)
    Q_PROPERTY(QString assetEditName READ assetEditName NOTIFY assetEditChanged)
    Q_PROPERTY(double subtitleGenProgress READ subtitleGenProgress NOTIFY subtitleGenProgressChanged)
    Q_PROPERTY(QString subtitleGenStatus READ subtitleGenStatus NOTIFY subtitleGenStatusChanged)
    Q_PROPERTY(bool segmenting READ segmenting NOTIFY segmentingChanged)
    Q_PROPERTY(double segmentProgress READ segmentProgress NOTIFY segmentProgressChanged)
    Q_PROPERTY(QString segmentStatus READ segmentStatus NOTIFY segmentStatusChanged)
    Q_PROPERTY(bool reverseRendering READ reverseRendering NOTIFY reverseRenderingChanged)
    Q_PROPERTY(double reverseRenderProgress READ reverseRenderProgress NOTIFY reverseRenderProgressChanged)
    Q_PROPERTY(QString reverseRenderStatus READ reverseRenderStatus NOTIFY reverseRenderStatusChanged)
    Q_PROPERTY(bool denoising READ denoising NOTIFY denoisingChanged)
    Q_PROPERTY(double denoiseProgress READ denoiseProgress NOTIFY denoiseProgressChanged)
    Q_PROPERTY(QString denoiseStatus READ denoiseStatus NOTIFY denoiseStatusChanged)
    Q_PROPERTY(bool segmentSessionActive READ segmentSessionActive NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool segmentationForTemplate READ segmentationForTemplate NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool segmentEncoding READ segmentEncoding NOTIFY segmentSessionChanged)
    Q_PROPERTY(int segmentRevision READ segmentRevision NOTIFY segmentSessionChanged)
    Q_PROPERTY(QVariantList segmentPoints READ segmentPoints NOTIFY segmentSessionChanged)
    Q_PROPERTY(QString segmentBackend READ segmentBackend NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool segmentBackendUsesPoints READ segmentBackendUsesPoints NOTIFY segmentSessionChanged)
    Q_PROPERTY(QSize segmentFrameSize READ segmentFrameSize NOTIFY segmentSessionChanged)
    // Multicam punching session. The live project is not touched until Save; switches rewrite a
    // staged copy that playback is pointed at so the program monitor shows the mix.
    Q_PROPERTY(bool multicamActive READ multicamActive NOTIFY multicamChanged)
    // One entry per selected camera: {trackIndex, label, clipName, hasClip, active}.
    Q_PROPERTY(QVariantList multicamAngles READ multicamAngles NOTIFY multicamChanged)
    // Index into multicamAngles assigned at the playhead; -1 outside the session range.
    Q_PROPERTY(int multicamActiveAngle READ multicamActiveAngle NOTIFY multicamChanged)
    // Bumped whenever a fresh set of tiles lands; QML appends it as ?rev= to defeat the
    // URL-keyed image cache, the same way the segmentation window does.
    Q_PROPERTY(int multicamRevision READ multicamRevision NOTIFY multicamFramesChanged)
    // Staged program as a single lane: {start, duration, name, angle}. Empty before a session
    // has cameras.
    Q_PROPERTY(QVariantList multicamProgramClips READ multicamProgramClips NOTIFY multicamChanged)
    // There is enough imported video to build a rig from, and no visual clips that building one
    // would disturb. Drives the window's "set this up for me" offer.
    Q_PROPERTY(bool multicamCanSetUp READ multicamCanSetUp NOTIFY multicamChanged)
    Q_PROPERTY(bool speedCurveSessionActive READ speedCurveSessionActive NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QVariantList speedCurvePoints READ speedCurvePoints NOTIFY speedCurveChanged)
    Q_PROPERTY(int speedCurveRevision READ speedCurveRevision NOTIFY speedCurveFrameChanged)
    Q_PROPERTY(QSize speedCurveFrameSize READ speedCurveFrameSize NOTIFY speedCurveFrameChanged)
    Q_PROPERTY(double speedCurveSourceStart READ speedCurveSourceStart NOTIFY speedCurveSessionChanged)
    // Whole media length, not the clip's trimmed span — the filmstrip's frames are sampled across
    // the source file, so placing them needs both.
    Q_PROPERTY(double speedCurveMediaDuration READ speedCurveMediaDuration NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(double speedCurveSourceDuration READ speedCurveSourceDuration NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(double speedCurveRetimedDuration READ speedCurveRetimedDuration NOTIFY speedCurveChanged)
    Q_PROPERTY(double speedCurvePosition READ speedCurvePosition NOTIFY speedCurvePositionChanged)
    // Where the playhead sits along the *source*, 0..1 — the graph's own axis.
    Q_PROPERTY(double speedCurveSourcePosition READ speedCurveSourcePosition NOTIFY speedCurvePositionChanged)
    Q_PROPERTY(bool speedCurvePlaying READ speedCurvePlaying NOTIFY speedCurvePlayingChanged)
    Q_PROPERTY(QString speedCurveClipName READ speedCurveClipName NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QString speedCurveClipPath READ speedCurveClipPath NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QString speedCurveFilmstripPath READ speedCurveFilmstripPath NOTIFY speedCurveSessionChanged)
    // Media-bin preview session driving the phone's preview-and-edit page. The asset is
    // auditioned through its own single-clip player rather than QtMultimedia: a VideoOutput in a
    // secondary window paints black on Android, and this decodes through the same FFmpeg the
    // timeline uses, so whatever the editor plays the preview plays.
    Q_PROPERTY(bool assetPreviewActive READ assetPreviewActive NOTIFY assetPreviewSessionChanged)
    Q_PROPERTY(int assetPreviewRevision READ assetPreviewRevision NOTIFY assetPreviewFrameChanged)
    Q_PROPERTY(QSize assetPreviewFrameSize READ assetPreviewFrameSize NOTIFY assetPreviewFrameChanged)
    Q_PROPERTY(double assetPreviewDuration READ assetPreviewDuration NOTIFY assetPreviewSessionChanged)
    Q_PROPERTY(double assetPreviewPosition READ assetPreviewPosition NOTIFY assetPreviewPositionChanged)
    Q_PROPERTY(bool assetPreviewPlaying READ assetPreviewPlaying NOTIFY assetPreviewPlayingChanged)

    // Custom fade-shape session for FadeCurveWindow. Candidate is auditioned on the live clip
    // until applyFadeCurve commits it (or endFadeCurveSession restores the prior shape).
    Q_PROPERTY(bool fadeCurveSessionActive READ fadeCurveSessionActive NOTIFY fadeCurveSessionChanged)
    Q_PROPERTY(QVariantList fadeCurvePoints READ fadeCurvePoints NOTIFY fadeCurveChanged)
    Q_PROPERTY(QString fadeCurveClipName READ fadeCurveClipName NOTIFY fadeCurveSessionChanged)
    // Cubic handles for FadeCurve::Bezier, as {c1x, c1y, c2x, c2y}. Separate from the point list
    // because the two modes edit different shapes, not two views of one.
    Q_PROPERTY(QVariantList fadeCurveHandles READ fadeCurveHandles NOTIFY fadeCurveChanged)
    // "points" (polyline) or "bezier" (cubic). The editor opens on whichever the clip already
    // uses, so reopening Custom does not silently convert a bezier fade into a polyline.
    Q_PROPERTY(QString fadeCurveMode READ fadeCurveMode NOTIFY fadeCurveChanged)

    // The same editor, scoped to a transition's progress curve rather than a clip's fade. Kept
    // separate from the clip session above because that one also drives animIn/animOut and the
    // linked-partner sync, none of which a transition has.
    Q_PROPERTY(bool transitionCurveSessionActive READ transitionCurveSessionActive NOTIFY transitionCurveSessionChanged)
    Q_PROPERTY(QVariantList transitionCurvePoints READ transitionCurvePoints NOTIFY transitionCurveChanged)
    Q_PROPERTY(QString transitionCurveName READ transitionCurveName NOTIFY transitionCurveSessionChanged)
    Q_PROPERTY(QVariantList transitionCurveHandles READ transitionCurveHandles NOTIFY transitionCurveChanged)
    Q_PROPERTY(QString transitionCurveMode READ transitionCurveMode NOTIFY transitionCurveChanged)
    Q_PROPERTY(bool faceDetecting READ faceDetecting NOTIFY faceDetectingChanged)
    Q_PROPERTY(double faceDetectProgress READ faceDetectProgress NOTIFY faceDetectProgressChanged)
    Q_PROPERTY(QString faceDetectStatus READ faceDetectStatus NOTIFY faceDetectStatusChanged)
    // Detected shots for the clip named by sceneClipId. Analysis state, not project state:
    // it describes the source media, so it is cached on disk rather than saved (see
    // engine/SceneDetect.h) and is never pushed through the undo stack.
    Q_PROPERTY(QVariantList scenes READ scenes NOTIFY scenesChanged)
    Q_PROPERTY(QString sceneClipId READ sceneClipId NOTIFY scenesChanged)
    // Source file the live analysis describes, so the panel can ask for thumbnails.
    Q_PROPERTY(QString sceneClipPath READ sceneClipPath NOTIFY scenesChanged)
    Q_PROPERTY(int sceneClipRotationCorrection READ sceneClipRotationCorrection NOTIFY scenesChanged)
    Q_PROPERTY(bool sceneDetecting READ sceneDetecting NOTIFY sceneDetectingChanged)
    Q_PROPERTY(double sceneDetectProgress READ sceneDetectProgress NOTIFY sceneDetectProgressChanged)
    Q_PROPERTY(QString sceneDetectStatus READ sceneDetectStatus NOTIFY sceneDetectStatusChanged)
    Q_PROPERTY(int selectedTrack READ selectedTrack NOTIFY selectionChanged)
    Q_PROPERTY(int selectedClip READ selectedClip NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
    // Change token and size for the selection, so a clip delegate can ask "am I selected?" and
    // "is this a multi-selection?" without the whole selection being rebuilt into QVariantMaps
    // once per delegate — which made a marquee over N clips cost O(N^2) per mouse move.
    Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedClipData READ selectedClipData NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantList selectedClipEffects READ selectedClipEffects NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantList selectedClipAudioEffects READ selectedClipAudioEffects NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantMap selectedTransitionData READ selectedTransitionData NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(int selectedTransitionTrack READ selectedTransitionTrack NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(int selectedTransitionLeftClip READ selectedTransitionLeftClip NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(bool guidesEnabled READ guidesEnabled WRITE setGuidesEnabled NOTIFY guidesChanged)
    Q_PROPERTY(QString guideType READ guideType WRITE setGuideType NOTIFY guidesChanged)
    Q_PROPERTY(QVariantMap background READ background NOTIFY backgroundChanged)
    Q_PROPERTY(bool canvasCropMode READ canvasCropMode WRITE setCanvasCropMode NOTIFY canvasCropModeChanged)
    Q_PROPERTY(bool maskEditMode READ maskEditMode WRITE setMaskEditMode NOTIFY maskEditModeChanged)
    // Whether the preview should be showing mask handles right now. Selecting a mask clip is
    // itself a request to edit it, so the toolbar toggle is only needed to keep the handles up
    // while some *other* clip is selected.
    Q_PROPERTY(bool maskEditActive READ maskEditActive NOTIFY maskEditActiveChanged)
    Q_PROPERTY(bool inlineTextEditing READ inlineTextEditing NOTIFY inlineTextEditingChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    Q_PROPERTY(bool workAreaActive READ workAreaActive NOTIFY workAreaChanged)
    Q_PROPERTY(double workAreaInSeconds READ workAreaInSeconds NOTIFY workAreaChanged)
    Q_PROPERTY(double workAreaOutSeconds READ workAreaOutSeconds NOTIFY workAreaChanged)
    Q_PROPERTY(bool loopWorkAreaEnabled READ loopWorkAreaEnabled WRITE setLoopWorkAreaEnabled
                   NOTIFY loopWorkAreaEnabledChanged)
    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectNameChanged)
    Q_PROPERTY(QVariantMap projectMetadata READ projectMetadata NOTIFY projectMetadataChanged)
    Q_PROPERTY(bool packaging READ packaging NOTIFY packagingChanged)
    Q_PROPERTY(double packageProgress READ packageProgress NOTIFY packageProgressChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
    // Severity of lastMessage: "info" | "success" | "warning" | "error". Exists so
    // the QML toast host does not have to guess from the message wording — it used
    // to regex the prose, and none of the real failure strings matched, so a
    // corrupt-project open rendered as a neutral info toast.
    Q_PROPERTY(QString lastMessageSeverity READ lastMessageSeverity NOTIFY lastMessageChanged)
    // True from the moment loadProject()/loadProjectJson() is called — by the header,
    // the start screen, an external open, or a startup restore — until projectLoadFinished
    // fires. Tracked here rather than by each QML call site so a load kicked off from C++
    // (consumeStartupProject, restoreLastSessionIfEnabled) is just as visible as one QML
    // started itself; nothing else may replace the document while this is true.
    Q_PROPERTY(bool projectLoadPending READ projectLoadPending NOTIFY projectLoadPendingChanged)
    Q_PROPERTY(int draggingAssetIndex READ draggingAssetIndex WRITE setDraggingAssetIndex NOTIFY draggingAssetIndexChanged)
    // Set by MediaPreviewWindow.qml/AndroidMediaPreview.qml while open, so the bin grid can defer
    // rebuilding its delegate array (and the scroll-position flicker that causes) until the
    // window closes rather than on every metadata change (rotate, trim, a probe landing) it emits.
    Q_PROPERTY(bool assetPreviewWindowOpen READ assetPreviewWindowOpen WRITE setAssetPreviewWindowOpen NOTIFY assetPreviewWindowOpenChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectPathChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantMap recoveryInfo READ recoveryInfo NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(bool separateAudioAvailable READ canSeparateAudioSelection NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool restoreAudioAvailable READ canRestoreAudioSelection NOTIFY editCapabilitiesChanged) // VEDIT
    Q_PROPERTY(bool loudnessBusy READ loudnessBusy NOTIFY loudnessBusyChanged) // VEDIT
    // VEDIT: true while 조용한 부분 자르기 / 음악 줄이기 listen to the audio; the window blocks input.
    Q_PROPERTY(bool audioToolBusy READ audioToolBusy NOTIFY audioToolBusyChanged)
    Q_PROPERTY(bool voiceRecording READ voiceRecording NOTIFY voiceRecordingChanged) // VEDIT
    Q_PROPERTY(double voiceRecordingSeconds READ voiceRecordingSeconds NOTIFY voiceLevelChanged) // VEDIT
    Q_PROPERTY(double voiceLevel READ voiceLevel NOTIFY voiceLevelChanged) // VEDIT
    Q_PROPERTY(bool makeCompositeAvailable READ canMakeCompositeFromSelection NOTIFY editCapabilitiesChanged)
    // VEDIT: 소리로 맞추기 — two or more selected clips with sound.
    Q_PROPERTY(bool syncByAudioAvailable READ canSyncSelectionByAudio NOTIFY editCapabilitiesChanged)
    // While the sound is being compared: the progress window and its 0–1 reading.
    Q_PROPERTY(bool audioSyncBusy READ audioSyncBusy NOTIFY audioSyncChanged)
    Q_PROPERTY(double audioSyncProgress READ audioSyncProgress NOTIFY audioSyncChanged)
    // Open composite tabs as [{id, name}], main timeline excluded; "" is the main timeline's id.
    Q_PROPERTY(QVariantList sequenceTabs READ sequenceTabs NOTIFY sequenceTabsChanged)
    Q_PROPERTY(QString activeSequenceId READ activeSequenceId NOTIFY sequenceTabsChanged)
    Q_PROPERTY(bool unlinkAvailable READ canUnlinkSelection NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool mergeAvailable READ canMergeSelection NOTIFY editCapabilitiesChanged)
    // False until the user picks a launch layout (or decides later via first-clip setup / load).
    Q_PROPERTY(bool projectLayoutChosen READ projectLayoutChosen NOTIFY projectLayoutChosenChanged)

public:
    // VEDIT: overlap mode (Vegas-style auto dissolve) is on by default. Upstream test suites that
    // assume Drift's default flip this off before creating controllers.
    static inline bool s_defaultAllowClipOverlap = true;
    // VEDIT: CapCut-style — deleting or trimming closes the gap it leaves. Same test escape hatch.
    static inline bool s_defaultRippleEnabled = true;
    // VEDIT: CapCut's main-track magnet — with 빈틈 닫기 on, the main track (the bottom-most video
    // track) never keeps a gap: after every edit its clips sit end to end from 0 (a dissolve keeps
    // its overlap). Tests that place clips with gaps on purpose turn it off.
    static inline bool s_mainTrackMagnet = true;
    // VEDIT: tests of the trim/delete gap closing written before 빈틈 닫기 went per track: every
    // track closes gaps as if its switch were on.
    static inline bool s_allTracksMagnet = false;
    // VEDIT: an overlap always becomes a curved crossfade, even with the overlap switch off.
    // Upstream tests of stacking (overlap without a fade) switch it off.
    static inline bool s_alwaysCrossfadeOverlaps = true;
    // VEDIT: a track left with no clips by an edit disappears (CapCut). Tests of upstream
    // behaviour switch it off.
    static inline bool s_autoRemoveEmptyTracks = true;

    explicit AppController(AssetLibrary *assetLibrary, QObject *parent = nullptr);
    ~AppController() override;

    AssetLibrary *assetLibrary() const { return m_assetLibrary; }
    CloudProviders *cloudProviders() const { return m_cloud; }
    BinFolderListModel *binFolderModel() { return &m_binFolderModel; }
    QString currentBinFolderId() const { return m_currentBinFolderId; }
    void setCurrentBinFolderId(const QString &folderId);
    TimelineModel *timelineModel() { return &m_timelineModel; }
    ClipListModel *clipListModel() { return &m_clipListModel; }
    PlaybackEngine *playback() { return &m_playback; }
    QVariantList audioOutputDevices() const;
    QString audioOutputDeviceId() const { return m_audioOutputDeviceId; }
    void setAudioOutputDeviceId(const QString &id);
    // Handing out a mutable pointer is the point past which this object can no longer know what
    // happened to the project, so the derived caches are dropped here rather than trusted. Two
    // bool writes, and nothing in src/ takes this overload — it exists for tests and for code
    // that edits the project behind the controller's back, which is exactly the case that would
    // otherwise read a stale tracks() list.
    drift::Project *project()
    {
        m_tracksCacheValid = false;
        m_durationCacheValid = false;
        return &m_project;
    }
    const drift::Project *project() const { return &m_project; }

    QVariantList tracks() const;
    // The clips of one track, as a role model. This is where the timeline delegate's data lives;
    // `tracks` above carries only what the panel's JS helpers need to lay a row out. Index slots
    // are never reused or freed, so a model handed to a Repeater stays valid for the session.
    Q_INVOKABLE QObject *clipsModel(int trackIndex) const;
    quint32 tracksRevision() const { return m_tracksRevision; }
    int trackCount() const { return m_project.tracks().size(); }
    int clipCount() const;
    double playheadSeconds() const;
    double durationSeconds() const;
    // {start, duration} covering every clip on a video or audio track, for the subtitle lane's
    // voice waveform. In C++ because the QML that needed it was reading the whole `tracks` graph.
    Q_INVOKABLE QVariantMap mediaExtentSeconds() const;
    bool playing() const { return m_playing; }
    bool previewDragActive() const { return m_previewDragActive; }
    bool snapEnabled() const { return m_snapEnabled; }
    bool rippleEnabled() const { return m_rippleEnabled; }
    bool allowClipOverlap() const { return m_allowClipOverlap; }
    bool darkModeOverridden() const { return m_darkModeOverridden; }
    bool darkModePreferred() const { return m_darkModePreferred; }
    bool projectPortrait() const { return m_project.height() > m_project.width(); }
    bool workspaceLayoutOverridden() const { return m_workspaceLayoutOverridden; }
    QString workspaceLayoutPreferred() const { return m_workspaceLayoutPreferred; }
    // "portrait" / "landscape"; anything else is treated as landscape.
    Q_INVOKABLE void setWorkspaceLayoutPreference(const QString &layout);
    // Back to following the canvas orientation.
    Q_INVOKABLE void clearWorkspaceLayoutPreference();
    bool mediaGridMode() const { return m_mediaViewMode == QLatin1String("grid"); }
    QString mediaViewMode() const { return m_mediaViewMode; }
    bool autoKeyEnabled() const { return m_autoKeyEnabled; }
    bool timelineOverviewVisible() const { return m_timelineOverviewVisible; }
    void setTimelineOverviewVisible(bool visible);
    qreal trackLabelsWidth() const { return m_trackLabelsWidth; }
    int timelineClipSize() const { return m_timelineClipSize; } // VEDIT
    bool loudnessBusy() const { return m_loudnessBusy; } // VEDIT
    bool audioToolBusy() const { return m_audioToolBusy; } // VEDIT
    bool voiceRecording() const; // VEDIT
    double voiceRecordingSeconds() const; // VEDIT
    double voiceLevel() const; // VEDIT
    void setTimelineClipSize(int size); // VEDIT
    void setTrackLabelsWidth(qreal width);
    QStringList timelineToolbarItems() const { return m_timelineToolbarItems; }
    QStringList timelineMenuItems() const { return m_timelineMenuItems; }
    Q_INVOKABLE void setTimelineToolbarLayout(const QStringList &toolbarItems,
                                              const QStringList &menuItems);
    // Every clip in the project as a flat run of numbers — lane, clip-type code, start seconds,
    // duration seconds — for the overview strip's canvas. Flat because the strip repaints off
    // it: a list of numbers costs a fraction of what converting the clip graph to JS does.
    Q_INVOKABLE QVariantList timelineOverviewBlocks() const;
    // How many bands the strip splits its height into. Adjustment lanes are drawn inside their
    // parent's row on the timeline and get no band of their own here.
    Q_INVOKABLE int timelineOverviewLaneCount() const;
    bool reopenLastProject() const { return m_reopenLastProject; }
    bool vaapiZeroCopy() const { return m_vaapiZeroCopy; }
    bool vaapiZeroCopySupported() const;
    bool mediaCodecZeroCopy() const { return m_mediaCodecZeroCopy; }
    bool mediaCodecZeroCopySupported() const;
    QString preferredGpu() const { return m_preferredGpu; }
    bool gpuPreferenceSupported() const;
    bool invertTimelineScroll() const { return m_invertTimelineScroll; }
    QString uiLanguage() const { return m_uiLanguage; }
    QVariantList uiLanguages() const;
    bool needsUiLanguagePrompt() const { return m_needsUiLanguagePrompt; }
    double uiScale() const { return m_uiScale; }
    double appliedUiScale() const;
    bool uiScaleNeedsRestart() const;
    // Snaps to 1.0, 1.25, 1.5, 1.75, or 2.0. Safe before any AppController exists.
    static double storedUiScale();
    // Writes QT_SCALE_FACTOR from ui/scale unless the environment already set one.
    // Call once before QApplication; organization/application names must already be set.
    static void applyStoredUiScale();
    // Installs the .qm for ui/language (or the system locale). Call once after QApplication
    // is named, and again from setUiLanguage. Safe before any AppController exists.
    static void installUiTranslators();
    QStringList keyframeGraphHiddenProperties() const { return m_keyframeGraphHiddenProperties; }
    bool subtitleEditing() const { return m_subtitleEditing; }
    int selectedSubtitleCue() const { return m_selectedSubtitleCue; }
    bool undoAvailable() const { return m_undoStack.canUndo(); }
    bool redoAvailable() const { return m_undoStack.canRedo(); }
    bool exportInProgress() const { return m_exportInProgress; }
    double exportProgress() const;
    QString exportStatusText() const { return m_exportStatusText; } // VEDIT
    static QString veditDurationText(qint64 ms); // VEDIT
    static QString veditSizeText(qint64 bytes); // VEDIT
    bool canShareExport() const;
    bool subtitleGenerating() const { return m_subtitleGenerating; }
    QString replacingAssetId() const { return m_replacingAssetId; }
    bool editingAsset() const { return m_editingAsset; }
    double assetEditProgress() const { return m_assetEditProgress; }
    QString assetEditStatus() const { return m_assetEditStatus; }
    bool assetEditIsConversion() const { return m_assetEditIsConversion; }
    QString assetEditName() const { return m_assetEditKeepName; }
    double subtitleGenProgress() const { return m_subtitleGenProgress; }
    QString subtitleGenStatus() const { return m_subtitleGenStatus; }
    bool segmenting() const { return m_segmenting; }
    double segmentProgress() const { return m_segmentProgress; }
    QString segmentStatus() const { return m_segmentStatus; }
    bool reverseRendering() const { return m_reverseRendering; }
    double reverseRenderProgress() const { return m_reverseProgress; }
    QString reverseRenderStatus() const { return m_reverseStatus; }
    bool denoising() const { return m_denoising; }
    double denoiseProgress() const { return m_denoiseProgress; }
    QString denoiseStatus() const { return m_denoiseStatus; }
    bool segmentSessionActive() const { return m_segSessionActive; }
    bool segmentationForTemplate() const { return m_segForTemplate; }
    bool segmentEncoding() const { return m_segEncoding; }
    int segmentRevision() const { return m_segRevision; }
    QVariantList segmentPoints() const { return m_segPoints; }
    QString segmentBackend() const { return m_segBackend; }
    // SAM2 is prompted; RVM finds people on its own and has nothing to click.
    bool segmentBackendUsesPoints() const { return m_segBackend == QLatin1String("sam2"); }
    QSize segmentFrameSize() const { return m_segFrame.size(); }
    bool faceDetecting() const { return m_faceDetecting; }
    double faceDetectProgress() const { return m_faceDetectProgress; }
    QString faceDetectStatus() const { return m_faceDetectStatus; }
    QVariantList scenes() const { return m_scenes; }
    QString sceneClipId() const { return m_sceneClipId; }
    QString sceneClipPath() const { return m_sceneClipPath; }
    int sceneClipRotationCorrection() const { return m_sceneClipRotationCorrection; }
    bool sceneDetecting() const { return m_sceneDetecting; }
    double sceneDetectProgress() const { return m_sceneDetectProgress; }
    QString sceneDetectStatus() const { return m_sceneDetectStatus; }
    int selectedTrack() const { return m_selectedTrack; }
    int selectedClip() const { return m_selectedClip; }
    QVariantList selection() const;
    int selectionRevision() const { return m_selectionRevision; }
    int selectionCount() const { return m_selection.size(); }
    QVariantMap selectedClipData() const;
    QVariantList selectedClipEffects() const;
    QVariantList selectedClipAudioEffects() const;
    QVariantMap selectedTransitionData() const;
    int selectedTransitionTrack() const { return m_selectedTransitionTrack; }
    int selectedTransitionLeftClip() const { return m_selectedTransitionLeftClip; }
    bool guidesEnabled() const { return m_guidesEnabled; }
    QString guideType() const { return m_guideType; }
    QVariantMap background() const;
    QVariantList actions() const;
    QVariantList bookmarks() const;
    bool workAreaActive() const { return m_project.hasWorkArea(); }
    double workAreaInSeconds() const;
    double workAreaOutSeconds() const;
    bool loopWorkAreaEnabled() const { return m_loopWorkAreaEnabled; }
    void setLoopWorkAreaEnabled(bool enabled);
    QString projectName() const;
    QString lastMessage() const { return m_lastMessage; }
    QString lastMessageSeverity() const { return m_lastMessageSeverity; }
    bool projectLoadPending() const { return m_projectLoadPending; }
    int draggingAssetIndex() const { return m_draggingAssetIndex; }
    void setDraggingAssetIndex(int index);
    bool assetPreviewWindowOpen() const { return m_assetPreviewWindowOpen; }
    void setAssetPreviewWindowOpen(bool open);
    bool hasUnsavedChanges() const { return m_dirty; }
    QString currentProjectPath() const { return m_currentProjectPath; }
    bool recoveryAvailable() const { return m_recoveryAvailable; }
    QVariantMap recoveryInfo() const { return m_recoveryInfo; }
    QVariantList recentProjects() const;

    void setPlayheadSeconds(double seconds);
    void setPlaying(bool playing);
    void setSnapEnabled(bool enabled);
    void setRippleEnabled(bool enabled);
    void setAllowClipOverlap(bool enabled);
    Q_INVOKABLE void setDarkModePreference(bool enabled);
    Q_INVOKABLE void clearDarkModePreference();
    void setMediaGridMode(bool enabled);
    void setMediaViewMode(const QString &mode);
    void setAutoKeyEnabled(bool enabled);
    void setReopenLastProject(bool enabled);
    void setVaapiZeroCopy(bool enabled);
    void setMediaCodecZeroCopy(bool enabled);
    void setPreferredGpu(const QString &id);
    void setInvertTimelineScroll(bool enabled);
    Q_INVOKABLE void setMcpEnabled(bool enabled);
    // Headless wires transports onto the server itself, which the on/off switch above
    // does not expose.
    drift::mcp::McpServer *mcpServer() const { return m_mcp.get(); }
    bool mcpEnabled() const { return mcpRunning(); }
    bool mcpRunning() const;
    QString mcpUrl() const;
    QString mcpToken() const;
    int mcpPort() const;
    QString mcpError() const;
    QString mcpCursorSnippet() const;
    QString mcpClaudeCommand() const;
    QString mcpStdioSnippet() const;
    Q_INVOKABLE void copyMcpCursorSnippet();
    Q_INVOKABLE void copyMcpClaudeCommand();
    Q_INVOKABLE void copyMcpStdioSnippet();
    Q_INVOKABLE void copyMcpAgentGuide();
    Q_INVOKABLE void rotateMcpToken();
    bool mcpStartOnLaunch() const { return m_mcpStartOnLaunch; }
    Q_INVOKABLE void setMcpStartOnLaunch(bool enabled);
    // Starts the server if mcpStartOnLaunch is set. GUI-only — called once from
    // Main.qml's startup sequence; headless mode never calls this, since it configures
    // and starts the server itself from --mcp-port/--mcp-token/--mcp-stdio.
    Q_INVOKABLE void applyMcpStartOnLaunch();
    QString mcpAgentGuide() const;
    Q_INVOKABLE QVariantMap debugInfo() const;
    Q_INVOKABLE QString debugInfoText() const;
    Q_INVOKABLE void copyDebugInfo();

    // Playback diagnostics. The environment and counter half is cheap enough to call whenever
    // the dialog opens; the benchmark decodes for a couple of seconds and so runs off the GUI
    // thread and answers with playbackBenchmarkFinished.
    bool playbackBenchmarkRunning() const { return m_benchmarkRunning.load(); }
    Q_INVOKABLE QVariantMap playbackDiagnostics() const;
    Q_INVOKABLE void startPlaybackBenchmark();
    // One paste for a bug report: host facts and codec support followed by what playback is
    // actually doing. Split across two clipboard copies, reporters send whichever tab they
    // happened to have open, which is rarely the one that explains the problem.
    //
    // `benchmarkInfo` carries only the sweep's own sections — the live rows are collected here,
    // beside the host facts. Taking them from the dialog instead meant the two halves of a
    // pasted report described different moments: the rows were captured when the dialog opened
    // and never refreshed, so a report could say NVDEC in one section and Direct3D 11 in the
    // other, for the same machine, in the same paste.
    Q_INVOKABLE void copyDiagnosticsReport(const QVariantMap &benchmarkInfo);

    // MCP helpers (GUI thread). Used by src/mcp, not QML.
    QPair<int, int> mcpLocateClip(const QString &id) const;
    QString mcpClipId(int trackIndex, int clipIndex) const;
    QVariantMap mcpCompactClip(int trackIndex, int clipIndex, bool includeCanvas = true) const;
    struct McpInspectOptions {
        bool clips = false;
        bool detail = false;
        bool cues = false;
        bool verbose = false;
        int since = -1;
        int track = -1;
        QString clip;
    };
    QJsonObject mcpInspect(const McpInspectOptions &options) const;
    QJsonObject mcpInspect(bool includeClips, int sinceRevision = -1, bool detail = false,
                           bool includeCues = false) const
    {
        return mcpInspect(McpInspectOptions{includeClips, detail, includeCues, false, sinceRevision});
    }
    int mcpRevision() const { return m_mcpEditRevision; }
    bool mcpSetClipCanvas(int trackIndex, int clipIndex, const QVariantMap &patch);
    QJsonObject mcpCaptureFrame(double atSeconds, bool full);

    // Perception for agents: a labelled contact sheet, a text profile of change over time, and a
    // waveform rendered as an image. All block on the mcpCaptureFrame pattern.
    struct McpFrameSheetRequest {
        double start = -1.0;
        double end = -1.0;
        QList<double> at;
        QString sample = QStringLiteral("changes");
        int n = 12;
        int cols = 0;
        int tileWidth = 0;
        int minChange = 12;
        bool label = true;
        bool toPath = false;
        int track = -1;
        int clip = -1;
    };
    QJsonObject mcpFrameSheet(const McpFrameSheetRequest &request);

    struct McpActivityRequest {
        double start = -1.0;
        double end = -1.0;
        int samples = 200;
        int peaks = 8;
        bool audio = true;
        int track = -1;
        int clip = -1;
    };
    QJsonObject mcpActivity(const McpActivityRequest &request);

    QJsonObject mcpWaveformImage(const QString &mode, int trackIndex, int clipIndex,
                                 const QString &assetId, double startSeconds, double durSeconds,
                                 int width, int height, bool spectrogram,
                                 int summaryBuckets, bool words = true) const;
    bool mcpSetWorkArea(double inSeconds, double outSeconds);

    // Audio for agents. All of these block: the QML-facing waveform getters return empty on the
    // first call and repaint on a signal, which works for a binding and not at all for a caller
    // that gets one reply. These decode/mix inline instead, on the mcpCaptureFrame pattern.
    QJsonObject mcpWaveformForClip(int trackIndex, int clipIndex, int buckets) const;
    QJsonObject mcpWaveformForAsset(const QString &assetId, double startSeconds,
                                    double durSeconds, int buckets) const;
    QJsonObject mcpWaveformForTimeline(double startSeconds, double durSeconds, int buckets) const;
    QJsonObject mcpDetectBeats(double startSeconds, double durSeconds, bool force);
    QJsonObject mcpBeatPayload() const;
    QJsonObject mcpAudioSummary() const;
    // Grid times from the current analysis. `unit` is beat, bar or onset; `minStrength` filters
    // onsets only. Empty when nothing has been analysed yet.
    QList<double> mcpBeatTimes(const QString &unit, double minStrength) const;
    // --- scene toolbox ---
    QJsonObject mcpDetectScenes(int trackIndex, int clipIndex, double threshold, double minScene,
                                bool withObjects);
    // The live analysis, filtered and shaped for MCP. Times are reported in both source and
    // timeline space so an agent never has to redo the trim/speed/reverse mapping itself.
    QJsonObject mcpListScenes(const QString &label, double minScore, const QString &sort,
                              int limit, int trackIndex = -1, int clipIndex = -1) const;
    // Rows for a clip's scene analysis: the live one when it is the last scanned clip, else the
    // on-disk cache. Empty when it was never scanned.
    QVariantList mcpSceneRows(int trackIndex, int clipIndex, const drift::Clip **clip) const;
    QJsonObject mcpDescribeClip(int topCount, int trackIndex = -1, int clipIndex = -1) const;
    QJsonObject mcpFindScenes(const QString &label, double minScore, int trackIndex,
                              int limit) const;
    // Timeline seconds of every detected boundary inside the clip that was analysed.
    QList<double> mcpSceneCutTimes(double minScore, const QString &label) const;
    int mcpBookmarkScenes(double minScore, const QString &label, const QString &labelPrefix);
    // Which model addons are installed, so an agent can say what to install rather than
    // retrying blindly.
    QJsonObject mcpAiCapabilities() const;
    // Transcript-first editing. Transcripts live on the asset (Project::transcript) in source time.
    QJsonObject mcpTranscribe(const QStringList &assetIds, const QJsonObject &options);
    QJsonObject mcpGetTranscript(const QString &assetId, int trackIndex, int clipIndex,
                                 double startSeconds, double endSeconds, const QJsonObject &options) const;
    QJsonObject mcpDiarize(const QString &assetId, const QJsonObject &options);
    QJsonObject mcpKeepRanges(int trackIndex, int clipIndex, const QJsonArray &ranges, double padding,
                              double declick, bool ripple);
    QJsonObject mcpAssemble(const QJsonArray &edl, int trackIndex, bool atGiven, double atSeconds,
                            double padding, double declick);
    QJsonObject mcpCutWords(int trackIndex, int clipIndex, const QJsonObject &args);
    QJsonObject mcpTtsGenerate(const QJsonObject &args);
    QJsonObject mcpSfxGenerate(const QJsonObject &args);
    QJsonObject mcpListVoices(const QJsonObject &args) const;
    QJsonObject mcpCloudProviderStatus() const;
    QJsonObject mcpGetJob(const QString &id) const;
    QJsonObject mcpCancelJob(const QString &id);
    JobRegistry *jobRegistry() const { return m_jobs; }

    int mcpBookmarkBeats(double startSeconds, double durSeconds, const QString &unit,
                         double minStrength, const QString &labelPrefix);
    QJsonObject mcpSetBeatLayers(bool grid, bool onsets);
    QJsonObject mcpSetClipVolume(int trackIndex, int clipIndex, double value, bool atGiven,
                                 double atSeconds);
    void mcpRememberExportSettings(const QVariantMap &settings);
    QVariantMap mcpLastExportSettings() const;
    void mcpBeginBatch();
    void mcpEndBatch(const QString &text, bool pushUndo);
    QJsonObject mcpListHistory(int limit = 20) const;
    QJsonObject mcpUndoTo(int index, const QString &hash);
    QJsonObject mcpTakeSnapshot(const QString &label);
    QJsonObject mcpListSnapshots() const;
    QJsonObject mcpRestoreSnapshot(const QString &hash);
    QJsonObject mcpDetectSilence(int trackIndex, int clipIndex, double startSeconds,
                                 double durSeconds, double threshold, double minDuration,
                                 double padding, const QString &method = QStringLiteral("energy")) const;
    QJsonObject mcpRemoveSilence(int trackIndex, int clipIndex, double threshold,
                                 double minDuration, double padding, double declick = 0.03,
                                 const QString &method = QStringLiteral("energy"));
    QJsonObject mcpAnalyzeLoudness(int trackIndex, int clipIndex, double startSeconds,
                                   double durSeconds) const;
    QJsonObject mcpNormalizeVolume(int trackIndex, int clipIndex, double targetLufs);
    QJsonObject mcpDuckUnder(int musicTrack, int musicClip, int overTrack,
                             const QStringList &overClips, double amount, double attack,
                             double release);
    QJsonObject mcpListFaceTrack(int trackIndex, int clipIndex) const;
    QJsonObject mcpAutoReframe(int trackIndex, int clipIndex, double aspect, const QString &mode);
    QJsonObject mcpListAddons() const;
    QJsonObject mcpInstallAddon(const QString &id);
    QJsonObject mcpCancelAddonInstall(const QString &id);
    QJsonObject mcpSetAcceleration(const QString &variant);
    void setAddonManager(AddonManager *manager) { m_addonManager = manager; }
    void setMarketClient(MarketClient *client) { m_marketClient = client; }
    MarketClient *marketClient() const { return m_marketClient; }
    AddonManager *addonManager() const { return m_addonManager; }
    void setUiLanguage(const QString &code);
    // First-launch chooser: persist the pick and never ask again. Settings uses setUiLanguage.
    Q_INVOKABLE void chooseUiLanguage(const QString &code);
    void setUiScale(double scale);
    // Strip chip click — folds `prop`'s curve away, or brings it back. Purely a view filter: the
    // chip stays put either way, and the animation keeps playing while it is hidden.
    Q_INVOKABLE void toggleKeyframeGraphPropertyVisible(const QString &prop);
    // Editing a property's value/diamond/interpolation un-hides its curve, so the thing just
    // edited is the thing on screen.
    Q_INVOKABLE void showKeyframeGraphProperty(const QString &prop);
    void setSubtitleEditing(bool editing);
    void setSelectedSubtitleCue(int index);
    void setProjectName(const QString &name);
    void setGuidesEnabled(bool enabled);
    void setGuideType(const QString &type);

    Q_INVOKABLE void addClipFromAsset(int assetIndex);
    // Multi-select "Add to timeline": each asset lands on its own kind-appropriate default
    // track (creating one if needed, exactly like addClipFromAsset), placed back to back in
    // the given order starting at the playhead — not all stacked on top of each other at the
    // same start time. One undo step for the whole batch.
    Q_INVOKABLE void addClipsFromAssets(const QStringList &assetIds);
    // VEDIT (조박사님): several bin items dropped on the timeline together go in one after another
    // from `atSeconds`, in the order they were shot (the time in the file name, else the file's
    // own date), as one undo step.
    Q_INVOKABLE void addClipsFromAssetsAt(const QStringList &assetIds, double atSeconds);
    Q_INVOKABLE QStringList sortAssetIdsByShotTime(const QStringList &assetIds) const;
    // VEDIT: the selection a bin drag carries when the card grabbed was one of several picked.
    Q_INVOKABLE void setDraggingAssetGroup(const QStringList &assetIds) { m_draggingAssetGroup = assetIds; }
    Q_INVOKABLE QStringList draggingAssetGroup() const { return m_draggingAssetGroup; }
    // VEDIT: 사진 슬라이드쇼 in one press — the photos among these assets go on the main track in
    // order, `secondsEach` long, overlapping half a second so each change is a curved dissolve,
    // alternating "slowly closer" and "slowly farther". One undo step; returns photos placed.
    Q_INVOKABLE int makeSlideshow(const QStringList &assetIds, double secondsEach = 3.0);
    // VEDIT: CapCut's 소리 추출 — the sound of a video in the bin goes on a new sound track at the
    // playhead, without its picture (a grandchild's song as background music). One undo step.
    Q_INVOKABLE bool addSoundFromVideo(const QString &assetId);
    Q_INVOKABLE void addClipFromAssetAt(int assetIndex, int trackIndex, double atSeconds);
    Q_INVOKABLE void addClipFromAssetOnNewTrack(int assetIndex, double atSeconds);
    // Same, but the new track goes at insertIndex rather than always on top, so
    // a timeline drop can create a lane below the tracks as well as above them.
    Q_INVOKABLE void addClipFromAssetOnNewTrackAt(int assetIndex, int insertIndex, double atSeconds);
    Q_INVOKABLE int clipCountForAsset(int assetIndex) const;
    Q_INVOKABLE bool removeAsset(int assetIndex);
    // Multi-select bulk removal, one undo step for the whole batch. Like removeAsset, refuses
    // the whole batch if any id is still referenced by a clip (see clipCountForAsset) — the
    // guard is enforced here, not just by the QML confirmation flow, so a caller that skips
    // that flow can't orphan a timeline clip. Ids that no longer resolve are skipped.
    // Returns how many were actually removed.
    Q_INVOKABLE int removeAssets(const QStringList &assetIds);

    // Explicit destructive variant used only after user confirmation. Removes every
    // timeline clip referencing the selected assets, cleans transitions that reference
    // those clips, then removes the assets from the project. The source files on disk
    // are never touched. The whole operation is recorded as one undo step.
    Q_INVOKABLE int removeAssetsAndClips(const QStringList &assetIds);
    // Bin label only — does not rename the file on disk or rewrite clip names.
    Q_INVOKABLE bool renameAsset(int assetIndex, const QString &name);
    // Bin-preview orientation for a video asset (absolute 0/90/180/270; -1 = the file's own tag),
    // as one undoable edit — AssetLibrary::setAssetRotation on its own leaves no undo entry.
    Q_INVOKABLE bool setAssetRotation(int assetIndex, int degrees);
    // Bin folder CRUD. parentId empty = bin root; nesting is arbitrary depth.
    Q_INVOKABLE QString createBinFolder(const QString &name, const QString &parentId);
    Q_INVOKABLE bool renameBinFolder(const QString &folderId, const QString &name);
    // Reparents the folder itself, keeping its own assets and subfolders — they stay pointed
    // at it, so they move along without being touched individually. Refuses moving a folder
    // into itself or into one of its own descendants.
    Q_INVOKABLE bool moveBinFolder(const QString &folderId, const QString &newParentId);
    // Moves the folder's direct children (assets and subfolders) up to its own parent, then
    // removes it. Never blocks and never recurses into deleting contents.
    Q_INVOKABLE bool deleteBinFolder(const QString &folderId);
    Q_INVOKABLE bool moveAssetToFolder(int assetIndex, const QString &folderId);
    // Multi-select bulk move, one undo step for the whole batch. Returns how many were moved.
    Q_INVOKABLE int moveAssetsToFolder(const QStringList &assetIds, const QString &folderId);
    // Imports a whole directory: mirrors its subfolder tree into the bin (one new bin folder per
    // filesystem folder, including empty ones) and imports every media file into the bin folder
    // matching its containing directory. `folderUrl` must be a local (file://) directory. The
    // directory walk runs off-thread, so this returns as soon as it starts — false means the URL
    // didn't resolve to a readable directory or an import was already running, and the outcome
    // arrives as folderImportFinished. Stops after a fixed number of files so a folder picked by
    // mistake can't queue thousands of probes. Not undoable — like a plain media import, "undo"
    // is deleting the folder by hand — but still marks the project dirty, so autosave and the
    // unsaved-changes prompt cover the hierarchy it creates.
    Q_INVOKABLE bool importFolder(const QUrl &folderUrl);
    bool importingFolder() const { return m_importingFolder; }
    // Points an existing bin row at a different file, keeping every clip that uses it where it
    // is — its position, trim, effects and transitions all survive. Asynchronous: true only means
    // the probe started, and the outcome arrives as assetReplaceFinished.
    Q_INVOKABLE bool replaceAssetSource(int assetIndex, const QUrl &url);
    // Writes an image asset (a freeze frame, typically) out to `url`. The format follows the
    // destination's extension, so the picker's name filter never has to be reported back.
    Q_INVOKABLE bool exportAssetImage(int assetIndex, const QUrl &url);
    // Rewrites the bin row: trim [inSeconds, outSeconds] and crop in display-normalized 0..1.
    // outSeconds < 0 means through the end. The original name is kept. Asynchronous; the
    // outcome arrives as assetEditFinished, and the row is then rebound via replace.
    Q_INVOKABLE bool saveAssetEdit(int assetIndex, double inSeconds, double outSeconds,
                                   double cropX, double cropY, double cropW, double cropH);
    Q_INVOKABLE void cancelAssetEdit();
    // "Convert to edit-friendly format": re-encodes each video asset onto a constant, standard
    // frame rate and swaps it in, one at a time, the same undoable way a crop does, marking the
    // asset edit-friendly. The original file is left where it was.
    Q_INVOKABLE void convertAssetsToConstantFrameRate(const QStringList &assetIds);
    Q_INVOKABLE bool trackAcceptsAsset(int trackIndex, int assetIndex) const;
    Q_INVOKABLE QString trackTypeForAsset(int assetIndex) const;
    // The same two questions asked of a bare media kind ("video", "audio", "image", ...) rather
    // than of a bin row. A file dragged in from the file manager has no row yet — it is not
    // imported until it is dropped — but its extension already says which track it belongs on,
    // which is all the drag preview needs to promise a landing spot.
    Q_INVOKABLE bool trackAcceptsKind(int trackIndex, const QString &mediaKind) const;
    Q_INVOKABLE QString trackTypeForKind(const QString &mediaKind) const;
    // presetId applies a built-in style pack on create; empty keeps the default text style.
    // VEDIT: CapCut's 글자 템플릿, the simple way — ready-made words with a Korean look
    // ({id, text, preset}). addQuickText puts one at the playhead, already written.
    Q_INVOKABLE QVariantList quickTextTemplates() const;
    Q_INVOKABLE bool addQuickText(const QString &templateId);
    Q_INVOKABLE void addTextClip(const QString &text, double atSeconds,
                                 const QString &presetId = QString());
    Q_INVOKABLE void addSubtitleClip(double atSeconds);
    // Import a SubRip (.srt) file as a new subtitle clip at the playhead (or atSeconds).
    Q_INVOKABLE bool importSubtitleFile(const QUrl &url, double atSeconds = -1.0);
    // Replace cues on an existing subtitle clip from a .srt file.
    Q_INVOKABLE bool importSubtitleFileIntoClip(int trackIndex, int clipIndex, const QUrl &url);
    // Export a subtitle clip's cues to a .srt file. Timestamps are clip-local unless timelineTimes,
    // which offsets them by the clip's start so they line up with the exported video.
    Q_INVOKABLE bool exportSubtitleFile(int trackIndex, int clipIndex, const QUrl &url,
                                        bool timelineTimes = false);
    // maxWordsPerCue caps words per caption; 0 keeps the recommended (character-width) packing.
    Q_INVOKABLE void generateSubtitlesForClip(int trackIndex, int clipIndex,
                                              const QString &language = QString(),
                                              int maxWordsPerCue = 0);
    // Captions every selected video/audio clip into one subtitle clip (falls back to the focused
    // clip). Returns false when the selection cannot be transcribed, e.g. clips overlap in time.
    Q_INVOKABLE bool generateSubtitlesForSelection(const QString &language = QString(),
                                                   int maxWordsPerCue = 0);
    // VEDIT: every speaking clip on the timeline (videos, their sound, voice recordings).
    Q_INVOKABLE bool generateSubtitlesForWholeVideo(const QString &language = QString(),
                                                    int maxWordsPerCue = 0);
    // Same, for the given (track, clip) pairs instead of the selection.
    bool generateSubtitlesForClips(const QList<QPair<int, int>> &pairs, const QString &language = QString(),
                                   int maxWordsPerCue = 0);
    // Captions every video/audio clip on the track within [startSec, endSec) into one subtitle clip.
    bool generateSubtitlesForRange(int trackIndex, double startSec, double endSec,
                                   const QString &language = QString(), int maxWordsPerCue = 0);
    Q_INVOKABLE void cancelSubtitleGeneration();
    Q_INVOKABLE QVariantList whisperLanguages();
    // points: [{x, y, include}] with x/y normalized to the source frame.
    // outputMode: "clips" (foreground + background on two new tracks) or "mask" (in place).
    // Which cutout models are installed: "sam2" (click to pick anything) and/or "rvm" (people,
    // automatic). Empty when neither is.
    Q_INVOKABLE QStringList segmentationBackends();
    // Installed RVM model variants, best first: "mobilenetv3", "resnet50".
    Q_INVOKABLE QStringList rvmQualities();
    Q_INVOKABLE void setSegmentationBackend(const QString &backend, const QString &quality = {});
    Q_INVOKABLE void segmentClip(int trackIndex, int clipIndex, const QVariantList &points,
                                 const QString &outputMode, const QString &backend = {},
                                 const QString &quality = {});
    Q_INVOKABLE void cancelSegmentation();
    Q_INVOKABLE bool segmentationAvailable();
    Q_INVOKABLE QString segmentationModelVariant();
    // Interactive prompting session driving the segmentation window. beginSegmentationSession
    // encodes the reference frame off the GUI thread; point edits after that only re-run the
    // cheap decoder.
    Q_INVOKABLE void beginSegmentationSession(int trackIndex, int clipIndex, double seconds,
                                              bool forTemplate = false);
    Q_INVOKABLE void endSegmentationSession();
    void openSegmentationForTemplate(int trackIndex, int clipIndex);

    // Starts a punching session from the current video selection (two or more clips on
    // distinct tracks). Returns false when there is nothing to punch and no empty-timeline
    // setup to offer; the window should stay closed.
    Q_INVOKABLE bool beginMulticamSession();
    Q_INVOKABLE void endMulticamSession();
    bool multicamActive() const { return m_multicamActive; }
    QVariantList multicamAngles() const;
    int multicamActiveAngle() const;
    int multicamRevision() const { return m_multicamRevision; }
    QVariantList multicamProgramClips() const;
    bool multicamCanSetUp() const;
    // One video track per imported camera, stacked from the top, extras muted. One undoable
    // edit; if a session is already open it then snapshots those clips as the cameras.
    Q_INVOKABLE void setUpMulticamFromAssets();
    // Punch `angleIndex` at the playhead on the staged copy. The live project is untouched.
    Q_INVOKABLE void switchMulticamAngle(int angleIndex);
    Q_INVOKABLE void saveMulticamAsSeparateTracks();
    Q_INVOKABLE void saveMulticamCombined();

    // Speed-curve editing session driving SpeedCurveWindow. The curve is held here as a
    // candidate and auditioned through a private single-clip player; the project is not touched
    // until applySpeedCurve mints the retimed copy.
    Q_INVOKABLE void beginSpeedCurveSession(int trackIndex, int clipIndex);
    Q_INVOKABLE void endSpeedCurveSession();
    bool speedCurveSessionActive() const { return m_speedCurveActive; }
    QVariantList speedCurvePoints() const;
    Q_INVOKABLE void setSpeedCurvePoints(const QVariantList &points);
    int speedCurveRevision() const { return m_speedCurveRevision; }
    QSize speedCurveFrameSize() const { return m_speedCurvePlayer.frameSize(); }
    double speedCurveSourceStart() const;
    double speedCurveMediaDuration() const;
    double speedCurveSourceDuration() const;
    double speedCurveRetimedDuration() const;
    double speedCurvePosition() const;
    bool speedCurvePlaying() const { return m_speedCurvePlayer.isPlaying(); }
    QString speedCurveClipName() const { return m_speedCurveClip.name; }
    QString speedCurveClipPath() const { return m_speedCurveClip.path; }
    QString speedCurveFilmstripPath() const { return m_speedCurveClip.filmstripPath; }
    Q_INVOKABLE void playSpeedCurvePreview();
    Q_INVOKABLE void pauseSpeedCurvePreview();
    Q_INVOKABLE void seekSpeedCurvePreview(double seconds);
    double speedCurveSourcePosition() const;
    // Seeks by graph position rather than by retimed time, so clicking the strip lands on the
    // frame under the cursor.
    Q_INVOKABLE void seekSpeedCurvePreviewAtSource(double position);
    Q_INVOKABLE void applySpeedCurve();
    Q_INVOKABLE void clearClipSpeedCurve(int trackIndex, int clipIndex);
    // VEDIT: CapCut's one-click curve presets ("montage", "hero", "bullet", "jumpcut",
    // "flashin", "flashout"; "none" clears), applied to the clip in place. With ripple on, the
    // clips after it on the same track follow the clip's new length.
    Q_INVOKABLE void applySpeedCurvePreset(int trackIndex, int clipIndex, const QString &presetId);
    // VEDIT: CapCut-style crop — insets 0..1 from each edge of the source frame.
    Q_INVOKABLE void setClipCrop(int trackIndex, int clipIndex, double l, double t, double r, double b);
    // VEDIT: the music / sound-effect library shipped in bundled-addons (kind "music" or "sfx").
    // Each pack root has library.json: {"items":[{"file","title","category","seconds","author","license"}]}.
    Q_INVOKABLE QVariantList bundledAudioLibrary(const QString &kind) const;
    // Imports the file (once) and drops it on the timeline at the playhead.
    Q_INVOKABLE bool addBundledAudio(const QString &path);
    // VEDIT: the finished-export dialog's [폴더 열기] / [바로 보기] on desktop.
    Q_INVOKABLE bool hasLastExport() const;
    Q_INVOKABLE void revealLastExport();
    // VEDIT (조박사님): the 일반 tab's file details for a clip — [{label, value}] rows: name,
    // folder, size, dates, length, picture size, frame rate, codecs, sound. Empty without a file.
    Q_INVOKABLE QVariantList clipFileDetails(int trackIndex, int clipIndex) const;
    // Opens the file's folder with the file picked (Explorer on Windows).
    Q_INVOKABLE void revealClipFile(int trackIndex, int clipIndex) const;
    // VEDIT (조박사님): while an edge is dragged, the preview shows the frame at that edge, as
    // a playhead drag would. The clip is trimmed in place for the picture only; endTrimEdge() puts
    // it and the playhead back before the real trim is committed. side: -1 left, 1 right.
    Q_INVOKABLE void showTrimEdge(int trackIndex, int clipIndex, int side, double seconds);
    // VEDIT (조박사님, Vegas): drag an end of the overlap between two clips. The left end moves the
    // incoming clip's head (earlier footage of it), the right end the outgoing clip's tail (later
    // footage of it); the far ends of both clips stay put. Stops where a clip's footage runs out.
    // side: -1 left end, 1 right end. Inside a preview drag (beginPreviewDrag / commitPreviewDrag).
    Q_INVOKABLE void previewOverlapEdge(int trackIndex, int leftClipIndex, int rightClipIndex, int side,
                                        double seconds);
    Q_INVOKABLE void endTrimEdge();
    Q_INVOKABLE void openLastExport();
    Q_INVOKABLE void previewSetClipCrop(int trackIndex, int clipIndex, double l, double t, double r, double b);
    Q_INVOKABLE QString clipSpeedCurvePreset(int trackIndex, int clipIndex) const;

    // Media-bin preview session. beginAssetPreview auditions the bin row; the page owns the
    // trim and crop values and hands them to saveAssetEdit itself.
    Q_INVOKABLE void beginAssetPreview(int assetIndex);
    Q_INVOKABLE void endAssetPreview();
    bool assetPreviewActive() const { return m_assetPreviewActive; }
    int assetPreviewRevision() const { return m_assetPreviewRevision; }
    QSize assetPreviewFrameSize() const { return m_assetPreviewPlayer.frameSize(); }
    double assetPreviewDuration() const;
    double assetPreviewPosition() const;
    bool assetPreviewPlaying() const { return m_assetPreviewPlayer.isPlaying(); }
    Q_INVOKABLE void playAssetPreview();
    Q_INVOKABLE void pauseAssetPreview();
    Q_INVOKABLE void seekAssetPreview(double seconds);

    Q_INVOKABLE void beginFadeCurveSession(int trackIndex, int clipIndex);
    Q_INVOKABLE void endFadeCurveSession();
    bool fadeCurveSessionActive() const { return m_fadeCurveActive; }
    QVariantList fadeCurvePoints() const;
    Q_INVOKABLE void setFadeCurvePoints(const QVariantList &points);
    QString fadeCurveClipName() const { return m_fadeCurveClipName; }
    Q_INVOKABLE void applyFadeCurve();
    Q_INVOKABLE void resetFadeCurvePreset(const QString &preset);
    QVariantList fadeCurveHandles() const;
    QString fadeCurveMode() const;
    Q_INVOKABLE void setFadeCurveHandles(double c1x, double c1y, double c2x, double c2y);

    // VEDIT: Vegas fade types — the outgoing side's fade-out and the incoming side's fade-in.
    // Curves: fast (equalPower) | linear | slow | smooth | sharp.
    Q_INVOKABLE void setTransitionFadeTypes(int trackIndex, const QString &transitionId, const QString &outCurve,
                                            const QString &inCurve);
    // VEDIT: one edge's fade curve ("in" | "out") of a clip.
    Q_INVOKABLE void setClipFadeEdgeCurve(int trackIndex, int clipIndex, const QString &edge, const QString &curve);
    // VEDIT: Vegas's event level, dragged on the line along a clip's top (one undo step).
    Q_INVOKABLE void setClipLevel(int trackIndex, int clipIndex, double level);
    // VEDIT: the same while the line is being dragged — the picture and the sound follow live;
    // commitPreviewDrag() makes it one undo step.
    Q_INVOKABLE void previewSetClipLevel(int trackIndex, int clipIndex, double level);
    // VEDIT: stereo sides — 0 follow the track (clip) / as recorded (track), 1 L on both, 2 R on
    // both, 3 L/R mixed. Setting the track clears its clips' own choices (the whole track follows).
    Q_INVOKABLE void setClipChannelMode(int trackIndex, int clipIndex, int mode);
    Q_INVOKABLE void setTrackChannelMode(int trackIndex, int mode);
    Q_INVOKABLE void setTransitionEasing(int trackIndex, const QString &transitionId,
                                         const QString &curve);
    Q_INVOKABLE void beginTransitionCurveSession(int trackIndex, const QString &transitionId);
    Q_INVOKABLE void endTransitionCurveSession();
    bool transitionCurveSessionActive() const { return m_transitionCurveActive; }
    QVariantList transitionCurvePoints() const;
    QString transitionCurveName() const { return m_transitionCurveName; }
    Q_INVOKABLE void setTransitionCurvePoints(const QVariantList &points);
    Q_INVOKABLE void applyTransitionCurve();
    Q_INVOKABLE void resetTransitionCurvePreset(const QString &preset);
    QVariantList transitionCurveHandles() const;
    QString transitionCurveMode() const;
    Q_INVOKABLE void setTransitionCurveHandles(double c1x, double c1y, double c2x, double c2y);
    Q_INVOKABLE void setSegmentationFrame(double seconds);
    Q_INVOKABLE void addSegmentationPoint(double x, double y, bool include);
    Q_INVOKABLE void removeSegmentationPoint(int index);
    Q_INVOKABLE void clearSegmentationPoints();
    Q_INVOKABLE void runSegmentationSession(const QString &outputMode);

    // Noise removal (DeepFilterNet3). previewDenoise renders a short window either side of the
    // model — original and cleaned — so the denoise window can A/B them before anything is
    // committed; applyDenoise renders the whole clip and adds it to the timeline.
    Q_INVOKABLE bool denoiseAvailable();
    Q_INVOKABLE void previewDenoise(int trackIndex, int clipIndex, double atSeconds);
    Q_INVOKABLE void applyDenoise(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelDenoise();

    // Bakes the clip's face landmarks to a sidecar so the face warp effects have something to
    // follow. Runs off the GUI thread; the result lands on the clip through the undo stack.
    Q_INVOKABLE void detectFacesForClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelFaceDetection();
    Q_INVOKABLE void clearFaceTrack(int trackIndex, int clipIndex);
    Q_INVOKABLE bool faceDetectionAvailable();

    // Estimates the clip's depth for the depth effects as a JobRegistry "depth" job targeting the
    // clip id. Returns the job id, or empty when it could not start (the reason goes to
    // lastMessage). The sidecar lands on the clip through the undo stack.
    Q_INVOKABLE QString estimateDepthForClip(int trackIndex, int clipIndex, bool highQuality = false);
    Q_INVOKABLE void cancelDepthEstimation(const QString &clipId);
    Q_INVOKABLE void clearDepth(int trackIndex, int clipIndex);
    Q_INVOKABLE bool depthAvailable();
    // {active, progress, status, error} of the newest depth job for a clip; empty if there is none.
    Q_INVOKABLE QVariantMap depthJob(const QString &clipId) const;
    // Normalised depth (0 far, 1 near) under a point of the clip's frame, given in 0..1 frame
    // coordinates, at a timeline time (negative: the playhead). -1 when the clip has no depth.
    Q_INVOKABLE double sampleDepthAt(int trackIndex, int clipIndex, double nx, double ny,
                                     double atSeconds = -1.0);

    // Finds the shot boundaries in a clip's source range. Runs off the GUI thread; the
    // result lands in `scenes` and in the on-disk cache, never in the project. A cached
    // analysis for the same clip and settings is published immediately without rescanning.
    // minSceneSeconds <= 0 means "leave the engine default alone".
    Q_INVOKABLE void detectScenesForClip(int trackIndex, int clipIndex, bool withObjects,
                                         double minSceneSeconds = 0.0);
    Q_INVOKABLE void cancelSceneDetection();
    Q_INVOKABLE void stabilizeClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelClipStabilization(int trackIndex, int clipIndex);
    Q_INVOKABLE void removeClipStabilization(int trackIndex, int clipIndex);
    Q_INVOKABLE void setClipStabilizeSmoothing(int trackIndex, int clipIndex, int value);
    Q_INVOKABLE void setClipStabilizeTripod(int trackIndex, int clipIndex, bool enabled);
    Q_INVOKABLE void setClipStabilizeMode(int trackIndex, int clipIndex, const QString &mode);
    // Whether the optional object-labelling pass can run. False until the object-model
    // addon is installed, which is what the panel's toggle is gated on.
    Q_INVOKABLE bool objectDetectionAvailable() const;
    Q_INVOKABLE void clearScenes();
    // Move the playhead to a detected scene. Scenes are in source time; this maps back
    // through the clip's trim and speed to reach the right timeline position.
    Q_INVOKABLE void seekToScene(int sceneIndex);
    // VEDIT: CapCut's 장면 나누기 — after Find scenes, cuts the scanned clip at every shot change
    // so each shot is its own clip. One undo step; returns how many cuts were made.
    Q_INVOKABLE int splitAtScenes();
    // Sensitivity, persisted in QSettings so a scan does not forget it between sessions.
    Q_INVOKABLE double sceneThreshold() const;
    Q_INVOKABLE void setSceneThreshold(double threshold);
    // shapeKind/shapeId is a catalog id from builtinShapes(), which is not always a ShapeKind name:
    // "circle" and "ellipse" are the same kind with different default aspects.
    Q_INVOKABLE void addShapeClip(const QString &shapeKind, double atSeconds);
    Q_INVOKABLE void addShapeClipAt(const QString &shapeId, int trackIndex, double atSeconds);
    Q_INVOKABLE void addAdjustmentClip(double atSeconds = -1.0, double durationSeconds = -1.0);
    Q_INVOKABLE void addAdjustmentClipAt(int trackIndex, double atSeconds = -1.0, double durationSeconds = -1.0);
    Q_INVOKABLE void addAdjustmentClipWithEffect(const QString &effectId, int trackIndex = -1, double atSeconds = -1.0, double durationSeconds = -1.0);

    // Adjustment tracks and lanes. `kind` is "videoEffects" | "audioEffects" | "mask".
    //
    // A standalone track applies to everything composited below it; a lane is nested in one
    // track and applies only to that track's clips. Which of the two you get is the whole
    // difference between the two placements, so they are separate calls rather than a flag.
    Q_INVOKABLE void addAdjustmentTrack(const QString &kind);
    // Index of a lane on `parentTrackIndex` able to hold `kind` over [atSeconds, +durationSeconds),
    // creating one when every existing lane is occupied there. Returns -1 if the parent cannot
    // take a lane. Note this shifts track indices when it inserts.
    Q_INVOKABLE int ensureAdjustmentLane(int parentTrackIndex, const QString &kind,
                                         double atSeconds = -1.0, double durationSeconds = -1.0);
    // Move an adjustment clip between the two placements. Both unlink it first: a pinned
    // adjustment belongs to its clip's track, so re-scoping it would leave the link meaningless.
    // `atSeconds` < 0 keeps the adjustment where it is on the timeline; a drag passes the
    // position it was released at.
    Q_INVOKABLE void moveAdjustmentToLane(int fromTrack, int fromClip, int parentTrackIndex,
                                          double atSeconds = -1.0);
    Q_INVOKABLE void moveAdjustmentToOwnTrack(int fromTrack, int fromClip,
                                              double atSeconds = -1.0);
    // Release an adjustment from the clip it is pinned to, leaving it where it is. Its edges
    // become draggable and it stops following the clip.
    Q_INVOKABLE void unlinkAdjustment(int trackIndex, int clipIndex);
    Q_INVOKABLE void relinkAdjustment(int trackIndex, int clipIndex, int mediaTrack, int mediaClip);
    Q_INVOKABLE void addStickerClip(const QString &stickerId, double atSeconds,
                                    int trackIndex = -1);
    Q_INVOKABLE QVariantList builtinStickers() const;
    Q_INVOKABLE QVariantList builtinStickerCategories() const;
    // The full emoji set behind the sticker packs; empty until the pack carrying the font is
    // installed.
    Q_INVOKABLE QVariantList emojiCatalog() const;
    Q_INVOKABLE QStringList emojiGroups() const;
    Q_INVOKABLE QString emojiFontFamily() const;
    Q_INVOKABLE void addEmojiClip(const QString &emoji, const QString &name, double atSeconds,
                                  int trackIndex = -1);
    Q_INVOKABLE QVariantList builtinShapes() const;
    Q_INVOKABLE QVariantList builtinShapeCategories() const;
    Q_INVOKABLE QVariantList previewClipsAtPlayhead() const;
    Q_INVOKABLE void beginPreviewDrag(const QString &undoText = {});
    Q_INVOKABLE void previewSetClipPosition(int trackIndex, int clipIndex, double xPixels, double yPixels);
    Q_INVOKABLE void previewSetClipSize(int trackIndex, int clipIndex, double widthPixels, double heightPixels);
    Q_INVOKABLE void previewSetClipRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                        double widthPixels, double heightPixels);
    // Text resizes scale the glyphs along with the box, so the size rides with the rect.
    Q_INVOKABLE void previewSetTextRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                        double widthPixels, double heightPixels, int pixelSize);
    Q_INVOKABLE void previewSetClipRotation(int trackIndex, int clipIndex, double degrees);
    Q_INVOKABLE void previewSetClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                            double atSeconds, double value);
    Q_INVOKABLE void previewSetEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                           const QString &key, double value);
    Q_INVOKABLE void previewSetClipSpeed(int trackIndex, int clipIndex, double speed);
    Q_INVOKABLE void previewSetClipMask(int trackIndex, int clipIndex, const QVariantMap &mask);
    Q_INVOKABLE void previewSetClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds);
    Q_INVOKABLE void commitPreviewDrag();
    Q_INVOKABLE void cancelPreviewDrag();
    Q_INVOKABLE int projectWidth() const;
    Q_INVOKABLE int projectHeight() const;
    Q_INVOKABLE int projectFps() const;
    Q_INVOKABLE void setProjectResolution(int width, int height);
    Q_INVOKABLE void setProjectFps(int fps);
    Q_INVOKABLE void setProjectSetup(int width, int height, int fps);
    Q_INVOKABLE void applyCanvasCrop(double x, double y, double width, double height);
    bool canvasCropMode() const { return m_canvasCropMode; }
    bool maskEditMode() const { return m_maskEditMode; }
    void setMaskEditMode(bool active);
    bool maskEditActive() const;
    void setCanvasCropMode(bool active);
    Q_INVOKABLE void setBackground(const QVariantMap &background);
    Q_INVOKABLE bool timelineHasVisualClips() const;
    Q_INVOKABLE bool shouldConfigureProjectForAsset(int assetIndex) const;
    Q_INVOKABLE QVariantMap suggestedProjectSetupForAsset(int assetIndex) const;
    bool projectLayoutChosen() const { return m_projectLayoutChosen; }
    Q_INVOKABLE void markProjectLayoutChosen();
    Q_INVOKABLE void selectClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void addToSelection(int trackIndex, int clipIndex);
    Q_INVOKABLE void setSelection(const QVariantList &pairs);
    Q_INVOKABLE void selectAllClips();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariantMap clipAt(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantMap activeVideoClipAtPlayhead() const;
    Q_INVOKABLE QVariantMap activeAudioClipAtPlayhead() const;
    Q_INVOKABLE double sourceTimeAtPlayhead() const;
    Q_INVOKABLE double sourceTimeForClip(const QVariantMap &clip) const;
    Q_INVOKABLE void deleteSelectedClip();
    Q_INVOKABLE void duplicateSelectedClip();
    Q_INVOKABLE void moveClip(int trackIndex, int clipIndex, double newStart);
    Q_INVOKABLE void moveClipToTrack(int trackIndex, int clipIndex, int newTrackIndex, double newStart);
    // VEDIT: the timeline's drop. `newTrackAt` >= 0 first makes a track of the clip's kind at that
    // index (dragged above or below every row); `copy` (Ctrl held) leaves the originals where they
    // were and drops copies. One undo step either way.
    Q_INVOKABLE void dropClip(int trackIndex, int clipIndex, int toTrack, double newStart,
                              int newTrackAt, bool copy);
    // VEDIT (조박사님): 끼워 넣기. On a track with 빈틈 메움 on, a clip dropped with its head on the
    // seam before `beforeClipId` goes in there: what follows moves right by its length, and the
    // place it left closes up. Empty `beforeClipId` puts it after the last clip. One undo step.
    Q_INVOKABLE void insertClipBefore(int trackIndex, int clipIndex, int toTrack, const QString &beforeClipId);
    Q_INVOKABLE bool trackInsertsOnDrop(int trackIndex) const { return trackRipples(trackIndex); }
    // Shifts every clip on `trackIndex` starting at/after `gapStartSeconds` left by the
    // width of the gap immediately following that position, closing it. Linked partner
    // clips on other tracks (e.g. a companion audio clip) follow along to stay in sync.
    Q_INVOKABLE void closeGap(int trackIndex, double gapStartSeconds);
    Q_INVOKABLE void alignSelectedClipLeft();
    Q_INVOKABLE void alignSelectedClipRight();
    // Mirrors drift::Haptics::TrimState (Haptics.h), which has documented since it was written
    // that it mirrors this enum. It does now — before, it mirrored nothing.
    enum TrimOutcome {
        TrimNone = 0,
        TrimMoved = 1,
        TrimSnapped = 2,
        TrimBlocked = 3,
    };
    Q_ENUM(TrimOutcome)

    Q_INVOKABLE void splitSelectedClipLeft();
    Q_INVOKABLE void splitSelectedClipRight();
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void splitClipAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void splitClipLeftAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void splitClipRightAt(int trackIndex, int clipIndex, double seconds);
    // What a trim would do, without doing it. The drag previews through these: applying the
    // edit on every pointer sample meant rebuilding the whole timeline model each time, which on
    // a heavy project cost far more than the edit itself. The commit on release runs the very
    // same computation, so the clip lands exactly where the preview showed it.
    //
    // Returns { ok, changed, outcome, start, duration, inPoint, outPoint } in seconds, or
    // { ok: false } for an out-of-range clip.
    Q_INVOKABLE QVariantMap previewTrimLeft(int trackIndex, int clipIndex, double newStart) const;
    Q_INVOKABLE QVariantMap previewTrimRight(int trackIndex, int clipIndex, double newEnd) const;

    // Opens and closes a trim drag. Scoping the gesture lets a repeated pointer position be
    // rejected before any work is done, and lets the caller find out whether the edge ever
    // actually moved — a press and release with no movement must not leave an undo step behind.
    Q_INVOKABLE void beginTrimGesture(int trackIndex, int clipIndex, int side);
    Q_INVOKABLE void endTrimGesture();
    // The whole release in one call: the edit, closing the gesture, and the undo commit. Done
    // separately from QML they sent three "the timeline changed" notifications -- one for the
    // edit, one for the undo push, one from finishEdit -- and each rebuilt the entire timeline
    // model. `side` < 0 is the left edge. `seconds` < 0 means the gesture never moved the edge,
    // so there is nothing to commit.
    Q_INVOKABLE void commitTrim(int trackIndex, int clipIndex, int side, double seconds);
    Q_INVOKABLE bool trimGestureChangedProject() const { return m_trimGestureChanged; }

    // One step of a live trim drag, in timeline seconds. Returns a TrimOutcome rather than void:
    // snapping and every limit that can stop this edge live inside these, and the one the user
    // most needs told — the source running out — has no cue on screen at all. Haptics::trimStep
    // has always been handed this return value; until the enum existed it was reading undefined.
    Q_INVOKABLE int trimClipLeft(int trackIndex, int clipIndex, double newStart);
    Q_INVOKABLE int trimClipRight(int trackIndex, int clipIndex, double newEnd);
    Q_INVOKABLE void setClipTrim(int trackIndex, int clipIndex, double inPoint, double outPoint);
    Q_INVOKABLE void setClipStart(int trackIndex, int clipIndex, double start);
    Q_INVOKABLE void setClipDuration(int trackIndex, int clipIndex, double duration);
    // VEDIT: CapCut's 사진 길이 한 번에 — every photo on the timeline gets this length, and what
    // follows each one on its track moves with it so cuts, gaps and dissolves keep their shape.
    // Locked tracks are left alone. One undo step; returns how many photos changed.
    Q_INVOKABLE int setAllPhotoDurations(double seconds);
    Q_INVOKABLE void setClipTextContent(int trackIndex, int clipIndex, const QString &text);
    // Display label on the timeline / inspector. Does not rename the source file.
    Q_INVOKABLE void setClipName(int trackIndex, int clipIndex, const QString &name);
    // Live text edits (preview drag) keep the canvas and properties panel in sync
    // while typing; commitTextEdit trims and pushes undo.
    Q_INVOKABLE void previewSetClipTextContent(int trackIndex, int clipIndex, const QString &text);
    Q_INVOKABLE void commitTextEdit(int trackIndex, int clipIndex, const QString &text);
    // In-place text editing on the preview: hide the clip's baked raster while the
    // QML inline editor is shown, then restore it. Commit via commitTextEdit.
    Q_INVOKABLE void beginTextEdit(int trackIndex, int clipIndex);
    Q_INVOKABLE void endTextEdit();
    bool inlineTextEditing() const { return m_inlineTextEditing; }
    Q_INVOKABLE void setSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues);
    Q_INVOKABLE void previewSetSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues);
    Q_INVOKABLE double subtitleLocalPlayheadSeconds(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void upsertSubtitleCueAtPlayhead(int trackIndex, int clipIndex, const QString &text);
    Q_INVOKABLE void seekToSubtitleCue(int trackIndex, int clipIndex, int cueIndex);
    Q_INVOKABLE void setTextStyle(int trackIndex, int clipIndex, const QVariantMap &style);
    // VEDIT: CapCut 글자 › 기본 — every basic setting of a text (or caption) as one flat map:
    // text, fontFamily, pixelSize, bold, italic, underline, color, letterSpacing, lineHeight,
    // align, valign, curve, stroke*/bg*/glow*/shadow* (On, Color, Width/Opacity/Radius/Blur/
    // Distance/Angle/Padding), opacity, posX/posY (from the picture's centre, up is +), rotation.
    Q_INVOKABLE QVariantMap textBasics(int trackIndex, int clipIndex) const;
    // Any subset of the keys above (plus textCase: upper/lower/title) in ONE undo step. A text's
    // box then hugs its words again, kept on the same centre — CapCut's text box.
    Q_INVOKABLE void setTextBasics(int trackIndex, int clipIndex, const QVariantMap &patch);
    // CapCut's 사전 설정 스타일 row: ready looks ({id, label, fill, stroke, bg, glow, shadow}).
    Q_INVOKABLE QVariantList textLookPresets() const;
    Q_INVOKABLE void applyReadyTextLook(int trackIndex, int clipIndex, const QString &lookId);
    // left / hcenter / right / top / vcenter / bottom of the picture (5% margin).
    Q_INVOKABLE void alignTextOnCanvas(int trackIndex, int clipIndex, const QString &where);
    // VEDIT: CapCut 말풍선 / 편집효과 — the galleries ({id, label}; thumbnails are
    // image://textstyle/<id>) and putting one on a text (or caption). "bubble-none" / "art-none"
    // take it off. One undo step each; the text's box re-hugs the words and bubble.
    Q_INVOKABLE QVariantList textBubbles() const;
    Q_INVOKABLE QVariantList textWordArts() const;
    Q_INVOKABLE void applyTextBubble(int trackIndex, int clipIndex, const QString &bubbleId);
    Q_INVOKABLE void applyTextWordArt(int trackIndex, int clipIndex, const QString &artId);
    // CapCut's 기본 텍스트: plain white words in the middle at the playhead, ready to type over.
    Q_INVOKABLE void addDefaultText();
    // VEDIT: the timeline tells how many pixels one second takes at its zoom; snapping then
    // reaches about 18 px either way (never less than 1 frame at 60 fps, never more than 1.5 s).
    Q_INVOKABLE void setSnapPixelsPerSecond(double pxPerSecond);
    Q_INVOKABLE double snapReachSeconds() const;
    // The shading stack of a text, subtitle or shape clip: layers[0] is drawn first. Each call
    // is one undo step; the preview variants coalesce into one through begin/commitPreviewDrag.
    Q_INVOKABLE QString addStyleLayer(int trackIndex, int clipIndex, const QString &kind, int atIndex = -1);
    Q_INVOKABLE bool removeStyleLayer(int trackIndex, int clipIndex, const QString &layerId);
    Q_INVOKABLE QString duplicateStyleLayer(int trackIndex, int clipIndex, const QString &layerId);
    Q_INVOKABLE bool moveStyleLayer(int trackIndex, int clipIndex, const QString &layerId, int toIndex);
    Q_INVOKABLE void setStyleLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch);
    Q_INVOKABLE void previewSetStyleLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch);
    // The text-only spellings, kept for the MCP tools and scripts that use them.
    Q_INVOKABLE QString addTextLayer(int trackIndex, int clipIndex, const QString &kind, int atIndex = -1);
    Q_INVOKABLE bool removeTextLayer(int trackIndex, int clipIndex, const QString &layerId);
    Q_INVOKABLE QString duplicateTextLayer(int trackIndex, int clipIndex, const QString &layerId);
    Q_INVOKABLE bool moveTextLayer(int trackIndex, int clipIndex, const QString &layerId, int toIndex);
    Q_INVOKABLE void setTextLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch);
    Q_INVOKABLE void previewSetTextLayer(int trackIndex, int clipIndex, const QString &layerId, const QVariantMap &patch);
    // In / Out / Loop animation slots: {preset, params, duration, stagger, unit, order, ease, …}.
    Q_INVOKABLE void setTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot, const QVariantMap &patch);
    Q_INVOKABLE void previewSetTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot, const QVariantMap &patch);
    Q_INVOKABLE void clearTextAnimationSlot(int trackIndex, int clipIndex, const QString &slot);
    Q_INVOKABLE QVariantList textAnimationPresets(const QString &slot = {}) const;
    Q_INVOKABLE QVariantList textAnimationCategories(const QString &slot) const;
    // Where to seek to preview the slot: the window start (In), before the exit (Out), the playhead (Loop).
    Q_INVOKABLE double textAnimationSlotStartSeconds(int trackIndex, int clipIndex, const QString &slot) const;
    // Looks: one-click recipes that rewrite the layer stack from a few params.
    Q_INVOKABLE QVariantList textLooks() const;
    Q_INVOKABLE void applyTextLook(int trackIndex, int clipIndex, const QString &lookId, const QVariantMap &params = {});
    Q_INVOKABLE void setTextLookParam(int trackIndex, int clipIndex, const QString &key, const QVariant &value);
    Q_INVOKABLE void previewSetTextLookParam(int trackIndex, int clipIndex, const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantList textPaintEffects() const;
    Q_INVOKABLE QVariantList textGradientPresets() const;
    // Copies this caption clip's style (not its keyframes) to every other subtitle clip on the
    // track ("track") or in the project ("project"). Returns how many changed; one undo step.
    Q_INVOKABLE int applyTextStyleToCaptions(int trackIndex, int clipIndex, const QString &scope = QStringLiteral("track"));
    // Animation presets imported from Lottie (After Effects text animators) or exported earlier.
    // Returns {ok, id, error, unsupported:[…]}; the preset lands in the slot's "Imported" category.
    Q_INVOKABLE QVariantMap importTextAnimationPreset(const QUrl &fileUrl, const QString &slot = {});
    Q_INVOKABLE bool renameUserTextAnimationPreset(const QString &presetId, const QString &label);
    Q_INVOKABLE bool deleteUserTextAnimationPreset(const QString &presetId);
    Q_INVOKABLE bool exportUserTextAnimationPreset(const QString &presetId, const QUrl &fileUrl);
    // Human label for a text keyframe property ("Shadow · Blur"); empty for non-text props.
    Q_INVOKABLE QString keyframePropertyLabel(int trackIndex, int clipIndex, const QString &prop) const;
    Q_INVOKABLE void applyTextPreset(int trackIndex, int clipIndex, const QString &presetId);
    Q_INVOKABLE QVariantList textPresets() const;
    // Style packs the user saved from the inspector. Kept out of textPresets() so the built-in
    // catalog (and the MCP list it feeds) stays a stable, shippable set.
    Q_INVOKABLE QVariantList userTextPresets() const;
    Q_INVOKABLE QString saveTextStyleAsPreset(int trackIndex, int clipIndex, const QString &label);
    Q_INVOKABLE bool renameUserTextPreset(const QString &presetId, const QString &label);
    Q_INVOKABLE bool deleteUserTextPreset(const QString &presetId);
    Q_INVOKABLE bool exportUserTextPreset(const QString &presetId, const QUrl &fileUrl);
    Q_INVOKABLE bool importUserTextPreset(const QUrl &fileUrl);
    Q_INVOKABLE QVariantList fontCatalog() const;
    Q_INVOKABLE QVariantList fontCategories() const;
    Q_INVOKABLE void setClipBlendMode(int trackIndex, int clipIndex, const QString &mode);
    Q_INVOKABLE void setClipSpeed(int trackIndex, int clipIndex, double speed);
    bool applyClipSpeed(int trackIndex, int clipIndex, double speed); // VEDIT: no undo step
    Q_INVOKABLE void setClipReverse(int trackIndex, int clipIndex, bool reverse);
    // Turns reverse on for a video clip and renders the proxy that makes it play back smoothly.
    // Reverse itself applies immediately; cancelling the render leaves the clip reversed on the
    // slow live-decode path, which is what clipHasReverseProxy reports.
    // Entry point for the Reverse control. Clips a proxy would do nothing for (audio, or one
    // already covered by a render) reverse straight away; anything else emits
    // reverseConfirmRequested so the dialog can ask before starting a render.
    Q_INVOKABLE void requestClipReverse(int trackIndex, int clipIndex);
    Q_INVOKABLE void applyClipReverse(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelReverseRender();
    Q_INVOKABLE bool clipHasReverseProxy(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void setClipFlip(int trackIndex, int clipIndex, bool flipH, bool flipV);
    // Stereo balance, -1..+1. previewSet* coalesces a slider drag into one undo entry the way
    // previewSetClipSpeed does; setClipPan is the one-shot for typing or resetting to centre.
    Q_INVOKABLE void previewSetClipPan(int trackIndex, int clipIndex, double pan);
    Q_INVOKABLE void setClipPan(int trackIndex, int clipIndex, double pan);
    Q_INVOKABLE void setClipRotationSnap(int trackIndex, int clipIndex, double degrees);
    // Discrete, lossless orientation fix (absolute 0/90/180/270, as the inspector shows it) —
    // distinct from the free decorative "Angle" above. Re-fits the clip's box for the new
    // orientation and decodes losslessly; see drift::Clip::rotationCorrection.
    Q_INVOKABLE void setClipOrientation(int trackIndex, int clipIndex, int degrees);
    Q_INVOKABLE bool canMergeSelection() const;
    Q_INVOKABLE void mergeSelectedClips();
    // True when the track holds two or more subtitle clips.
    Q_INVOKABLE bool canMergeAllSubtitlesOnTrack(int trackIndex) const;
    // Merge every subtitle clip on the track into one, regardless of the selection.
    Q_INVOKABLE void mergeAllSubtitlesOnTrack(int trackIndex);
    Q_INVOKABLE bool canSeparateAudioSelection() const;
    Q_INVOKABLE void separateAudioFromSelection();
    // VEDIT: CapCut's 오디오 복구 — undo a separation: the split-off audio clips go away and the
    // video plays its own sound again.
    Q_INVOKABLE bool canRestoreAudioSelection() const;
    // VEDIT: CapCut's 음량 정규화 — measure the clip in the background and set its level so it
    // plays at a comfortable loudness without clipping. One undo step.
    Q_INVOKABLE void matchClipLoudness(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 무음 제거 — cut the pauses out of the clip and close the gaps. Uses the
    // bundled voice-activity model when it is there, the level otherwise. Returns pauses cut.
    Q_INVOKABLE int removeSilentParts(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 더킹 — lower this (music) clip wherever a clip on another track talks over
    // it. Returns how many stretches were lowered.
    Q_INVOKABLE int duckMusicUnderVoices(int trackIndex, int clipIndex);
    // VEDIT: 음악을 영상 길이에 맞추기 — a music clip that runs past the last picture is cut where
    // the pictures end, and its last two seconds fade out. One undo step; false if nothing to do.
    Q_INVOKABLE bool fitMusicToVideo(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 음성 강화 — a voice tone (less boom, more presence) and a gentle compressor so
    // quiet words come up, added together. One undo step; false if the clip already has them.
    Q_INVOKABLE bool enhanceVoice(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 녹음. Start plays the timeline from the playhead with the speakers muted so
    // the picture can be narrated; stop puts the recording on a new sound track where it began.
    Q_INVOKABLE bool startVoiceRecording();
    // VEDIT: CapCut's 3-2-1 before recording — counts down a second at a time (voiceCountdown
    // shows 3, 2, 1) and then starts. Pressing it again during the count cancels.
    Q_INVOKABLE void requestVoiceRecording(int seconds = 3);
    Q_PROPERTY(int voiceCountdown READ voiceCountdown NOTIFY voiceCountdownChanged)
    int voiceCountdown() const { return m_voiceCountdown; }
    Q_INVOKABLE void stopVoiceRecording();
    Q_INVOKABLE void cancelVoiceRecording();
    // VEDIT: places a finished recording (or any sound file) on a new track at `atSeconds`.
    Q_INVOKABLE bool placeRecordedAudio(const QString &path, double atSeconds);
    // VEDIT: imports a sound file and waits (up to 15 s) for its length to be read, so the clip
    // made from it is as long as the sound — not the 5 s stand-in. Library index, or -1.
    int importAudioAndWait(const QString &path);
    // VEDIT: 빈틈 메움 after a clip changed length in place (duration, speed): with ripple on, the
    // clips after it on its track move by the change in its end, linked sound following.
    void rippleTrackAfterResize(int trackIndex, const QString &clipId, drift::TimeUs oldStart,
                                drift::TimeUs oldEnd);
    // VEDIT: CapCut's 글자 읽어주기 — speaks a text clip's words with a Windows voice (offline) and
    // puts the sound on a new track where the text starts. False with a message when it can't.
    Q_INVOKABLE bool readTextAloud(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 화면 캡처 — the picture at the playhead, at full project size, saved as a JPEG
    // straight into Pictures\Easy Cut, Easy Go! (no file dialog). Returns the saved path, or "".
    Q_INVOKABLE QString saveFrameAsPhoto();
    // VEDIT: 내보내기 예상 크기 in MB for the whole timeline. With a fixed bitrate it is that rate
    // times the length; with quality (CRF) encoding it is a rough guess from the picture size.
    // rangeSeconds > 0: only that much is exported (the work area).
    Q_INVOKABLE double estimatedExportMegabytes(int videoKbps, int audioKbps, int outputHeight,
                                                bool fixedBitrate, double rangeSeconds = -1.0) const;
    // VEDIT: CapCut's 자동 보정 — looks at the clip's middle picture on its own and sets 노출, 대비
    // and 채도 so a dark, flat or dull shot comes out bright and clear. One undo step.
    Q_INVOKABLE bool autoAdjustClip(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 색상 맞춤 — the clip's brightness, contrast, colour and colour cast set to look
    // like the scene just before it (the nearest picture ending before it starts, else the next).
    Q_INVOKABLE bool matchClipColor(int trackIndex, int clipIndex);
    // VEDIT: 조정 [처음대로] — takes the clip's own adjustments (밝기 … 가장자리 어둡게) off again.
    // Filters and the background key stay. One undo step; false if there was nothing to remove.
    Q_INVOKABLE bool resetClipAdjust(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 자동 배경 제거 in one press — the bundled 사람만 남기기 model keeps the people
    // in a video clip and makes the rest see-through (a matte on the clip itself; progress shows as
    // segmenting / segmentProgress). False, with a message, when it cannot start.
    Q_INVOKABLE bool keepOnlyPeople(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 바꾸기 — puts another photo or video in this clip's place: same spot and
    // length on the timeline, same effects, filter, adjustments, animation and transition; the new
    // picture is fitted to the screen. A video shorter than the clip shortens it. One undo step.
    Q_INVOKABLE bool replaceClipMedia(int trackIndex, int clipIndex, const QUrl &url);
    // VEDIT: 드래그 없이 순서 바꾸기 — swaps the clip with the one before (direction -1) or after
    // (+1) on its track. The pair keeps the span it had (and its overlap, so a dissolve stays a
    // dissolve), so nothing else moves. One undo step; false at the ends of the track.
    Q_INVOKABLE bool moveClipInOrder(int trackIndex, int clipIndex, int direction);
    // VEDIT: 드래그 없이 줄 옮기기 — the clip goes to the nearest track above (direction -1) or
    // below (+1) that takes its kind and is free over its time, or onto a new track there; same
    // time. One undo step.
    Q_INVOKABLE bool moveClipToNeighbourTrack(int trackIndex, int clipIndex, int direction);
    // The settings autoAdjustClip would choose for this picture: exposure (stops), contrast,
    // saturation — all neutral (0, 1, 1) for a picture that already looks fine.
    static QVariantMap autoAdjustValuesFor(const QImage &picture);
    static QString s_framePhotoDirOverride; // VEDIT: tests write somewhere temporary
    // VEDIT: the same for a caption clip — every line is spoken and placed, in order, on one new
    // sound track at that line's time (a line that runs long pushes the next one later). One undo
    // step; returns how many lines were read.
    Q_INVOKABLE int readCaptionsAloud(int trackIndex, int clipIndex);
    // VEDIT: CapCut keeps a caption to one short line. Lines longer than `maxChars` are broken at
    // the space nearest their middle (after punctuation when there is some), repeatedly, and the
    // time is shared out by length. Auto captions get this as they arrive; this button does it for
    // any caption clip. One undo step; returns how many lines were added.
    Q_INVOKABLE int splitLongCaptions(int trackIndex, int clipIndex, int maxChars = 24);
    // VEDIT: 자막 위치 — puts a caption's band at the top, middle or bottom of the screen ("top" /
    // "middle" / "bottom"), text aligned to match. One undo step; false for other clips.
    Q_INVOKABLE bool setCaptionPlacement(int trackIndex, int clipIndex, const QString &where);
    static QList<drift::SubtitleCue> splitLongCues(const QList<drift::SubtitleCue> &cues, int maxChars = 24);
    Q_INVOKABLE void restoreAudioFromSelection();

    // Composite clips: a nested timeline played as one clip. Making one moves the selection (and
    // its linked partners) into a new sequence and leaves a composite clip in its place.
    Q_INVOKABLE bool canMakeCompositeFromSelection() const;
    Q_INVOKABLE void makeCompositeFromSelection();
    QVariantList sequenceTabs() const;
    // Every composite in the project as [{id, name}], open or not, for the timeline's switcher.
    Q_INVOKABLE QVariantList compositeSequences() const;
    QString activeSequenceId() const { return m_project.activeSequenceId(); }
    // Opens (or switches to) a composite's tab. "" switches to the main timeline.
    Q_INVOKABLE void openSequence(const QString &sequenceId);
    Q_INVOKABLE void openCompositeClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void openCompositeAsset(const QString &assetId);
    Q_INVOKABLE void closeSequenceTab(const QString &sequenceId);
    // Renders the composite clip's range to a new video file and swaps the clip (and any audio
    // separated from it) over to that file. Runs in the background on the export slot.
    Q_INVOKABLE bool flattenComposite(int trackIndex, int clipIndex);
    // VEDIT: 합성 해제 — the reverse of makeCompositeFromSelection: the composite's own tracks and
    // clips come back onto the timeline where the composite stood (only the part of it in use,
    // if it was trimmed), and the composite clip goes. One undo step. False when there is
    // nothing to bring back (or the composite is sped up, which has no clip-for-clip reverse).
    Q_INVOKABLE bool ungroupComposite(int trackIndex, int clipIndex);
    // VEDIT: 소리로 맞추기 (CapCut's audio sync) — the selected clips with sound (picked by a
    // marquee or with Ctrl) are lined up by their sound: the one that starts earliest stays, the
    // others move so the same moment is heard at the same time. A clip that would land on another
    // clip in its own row gets a fresh row just below. Listening runs in the background;
    // the move is one undo step. Clips whose sound does not match are left where they are.
    bool canSyncSelectionByAudio() const;
    Q_INVOKABLE void syncSelectionByAudio();
    // The same, waiting for the answer (tests). Returns how many clips moved; -1 if it could not
    // run at all.
    int syncSelectionByAudioNow();
    bool audioSyncBusy() const { return m_audioSyncBusy; }
    double audioSyncProgress() const { return m_audioSyncProgress; }
    Q_INVOKABLE void cancelAudioSync();
    Q_INVOKABLE void separateAllAudioTracks(int trackIndex, int clipIndex);
    Q_INVOKABLE void separateAllAudioTracksFromSelection();
    Q_INVOKABLE QVariantList clipAudioStreams(int trackIndex, int clipIndex) const;
    Q_INVOKABLE int clipAudioStreamCount(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void setClipAudioStreamIndex(int trackIndex, int clipIndex, int streamIndex);
    Q_INVOKABLE bool canUnlinkSelection() const;
    Q_INVOKABLE void unlinkSelectedClips();
    Q_INVOKABLE void setClipMask(int trackIndex, int clipIndex, const QVariantMap &mask);

    // The mask shapes the assets panel offers as cards: {id, label} per entry. A "media" mask is
    // not here — it needs a file, so the panel asks for one and calls addMediaMaskToClip.
    Q_INVOKABLE QVariantList maskCatalog() const;
    // The mask as an SVG "d" string on the 0..100 grid MasksTab.qml scales from, for the
    // asset cards. Serialized from the same drift::maskPath the compositor rasterizes.
    Q_INVOKABLE QString maskShapeSvgPath(const QString &shape) const;
    // Pin a new mask to a clip, stacking on any already there rather than replacing them, and
    // select the mask adjustment it created. Ignores clips that cannot carry a mask.
    Q_INVOKABLE void addMaskToClip(int trackIndex, int clipIndex, const QString &shape);
    // A mask clip on one of `trackIndex`'s lanes, pinned to nothing: it masks whatever that track
    // shows over its span. This is what dropping a mask on empty track space means.
    Q_INVOKABLE void addMaskLaneClip(int trackIndex, const QString &shape, double atSeconds = -1.0,
                                     double durationSeconds = -1.0);
    // Pin an image or video as a raster mask, the way a segmentation matte is pinned.
    Q_INVOKABLE void addMediaMaskToClip(int trackIndex, int clipIndex, const QUrl &url);
    // Freeform vertex editing, driven by the preview overlay. `pointIndex` is the insertion slot,
    // so passing the index after an edge's first vertex splits that edge.
    Q_INVOKABLE void insertMaskPoint(int trackIndex, int clipIndex, int pointIndex, double x,
                                     double y);
    Q_INVOKABLE void removeMaskPoint(int trackIndex, int clipIndex, int pointIndex);
    // Everything the preview's mask editor needs, resolved in one call so QML cannot get the
    // three lookups out of step: the host clip's rect at the playhead, and the mask layers on
    // that track covering it. Empty when the selection names no maskable track.
    Q_INVOKABLE QVariantMap maskEditorState() const;
    // What the preview's depth handles need, as one snapshot: the selected media clip's frame on
    // the canvas and every enabled depth effect with a handle (relight, depth of field) on its
    // stack, with parameters resolved at the playhead. Empty when there is nothing to show.
    Q_INVOKABLE QVariantMap depthEffectEditorState() const;
    // Partial patch: only the keys present are applied, like setTextStyle.
    Q_INVOKABLE void setShapeStyle(int trackIndex, int clipIndex, const QVariantMap &style);

    // Vector (Lottie / SVG) clips. `source` is the document text itself or a path to a .json/.svg
    // file; opts keys: kind (lottie|svg, else detected), fit, loop, offset (seconds), slots
    // ({id: value}), name, duration (seconds; default = the animation's own length, 5 s for a
    // still). Every reply is {ok, error?} plus the inspect summary of the document.
    Q_INVOKABLE bool vectorSupportAvailable() const;
    Q_INVOKABLE QVariantMap addVectorClip(const QString &source, int trackIndex, double atSeconds,
                                          const QVariantMap &opts = {});
    Q_INVOKABLE QVariantMap setVectorSource(int trackIndex, int clipIndex, const QString &source,
                                            const QVariantMap &opts = {});
    Q_INVOKABLE QString setVectorOptions(int trackIndex, int clipIndex, const QVariantMap &opts);
    // A null/invalid value removes the override. Returns an error string, empty on success.
    Q_INVOKABLE QString setVectorSlot(int trackIndex, int clipIndex, const QString &name,
                                      const QVariant &value);
    // Declared slots with the clip's current overrides: [{id, type, value?}].
    Q_INVOKABLE QVariantList vectorSlots(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantMap inspectVector(const QString &source, const QString &kind = {}) const;
    Q_INVOKABLE QVariantMap inspectVectorClip(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QString vectorSourceText(int trackIndex, int clipIndex) const;
    // 3D model (.glb) clips. opts keys: animation (index), loop, offset (seconds), name, duration
    // (seconds; default = the first animation's length, 5 s for a static model), and the pose /
    // light statics (scale, depth, rotX, rotY, rotZ, lightYaw, lightPitch, lightIntensity,
    // ambient) as plain values — keyframe them through setClipKeyframe with "model3d.<key>".
    Q_INVOKABLE QVariantMap addModel3dClip(const QString &path, int trackIndex, double atSeconds,
                                           const QVariantMap &opts = {});
    Q_INVOKABLE QVariantMap setModel3dSource(int trackIndex, int clipIndex, const QString &path,
                                             const QVariantMap &opts = {});
    // Returns an error string, empty on success.
    Q_INVOKABLE QString setModel3dOptions(int trackIndex, int clipIndex, const QVariantMap &opts);
    Q_INVOKABLE QVariantMap inspectModel3d(const QString &path) const;
    Q_INVOKABLE QVariantMap inspectModel3dClip(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void setClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds);
    Q_INVOKABLE void setClipFadeCurve(int trackIndex, int clipIndex, const QString &curve);
    // which: "animIn" | "animOut". Partial patch: kind / duration / curve (or legacy ease).
    Q_INVOKABLE void setClipAnimation(int trackIndex, int clipIndex, const QString &which,
                                      const QVariantMap &patch);
    // VEDIT: CapCut's 콤보 — a whole-clip movement; it takes the place of 들어오기/나가기 (and they of
    // it). Empty id removes it.
    Q_INVOKABLE void setClipAnimCombo(int trackIndex, int clipIndex, const QString &comboId);
    // VEDIT: the pose of a 100×100 picture `t` (0..1) of the way through a 2 s clip with this
    // 들어오기 / 나가기 kind or 콤보 id — for the moving tiles. {opacity, dx, dy, scale, scaleX,
    // scaleY, rotation}.
    Q_INVOKABLE QVariantMap clipAnimPreview(const QString &slot, const QString &id, double t) const;
    // VEDIT: CapCut plays what was just picked — the selected transition with half a second either
    // side, or a clip's 들어오기 / 나가기 / 콤보 — then the playhead goes back where it was.
    Q_INVOKABLE void previewSelectedTransition();
    Q_INVOKABLE void previewClipAnimation(int trackIndex, int clipIndex, const QString &slot);
    void previewSpan(drift::TimeUs fromUs, drift::TimeUs toUs);
    // linkedAudio: also add the same transition between the two clips' linked audio, when they sit
    // next to each other on one audio track.
    // VEDIT (조박사님, Vegas): the way the UI adds a transition — only where the two clips overlap,
    // and exactly as long as the overlap. Clips that merely touch have no room for one: refused
    // with a hint to overlap them.
    Q_INVOKABLE bool addTransitionOnOverlap(int trackIndex, int clipIndex, const QString &kind);
    // VEDIT: this transition at every overlap on the track, each as long as its own overlap.
    Q_INVOKABLE int applyTransitionToAllOverlaps(int trackIndex, const QString &kind);
    Q_INVOKABLE void addTransition(int trackIndex, int clipIndex, const QString &kind, double durationSeconds,
                                   bool linkedAudio = true);
    Q_INVOKABLE void removeTransition(int trackIndex, const QString &transitionId);
    // VEDIT: CapCut's 모두에 적용 — the same transition at every cut on the track (photo
    // slideshows). One undo step; returns how many cuts got it.
    Q_INVOKABLE int applyTransitionToAllCuts(int trackIndex, const QString &kind, double durationSeconds);
    // VEDIT: CapCut's click (or "+") on a transition card — with a transition selected it is
    // replaced; otherwise the join nearest the playhead gets it (on the selected clip's track, else
    // a 빈틈 닫기 video track, else any video track). False when there is no join to put it on.
    Q_INVOKABLE bool addTransitionAtPlayhead(const QString &kind);
    Q_INVOKABLE void setTransitionDuration(int trackIndex, const QString &transitionId, double durationSeconds);
    Q_INVOKABLE void setTransitionKind(int trackIndex, const QString &transitionId, const QString &kind);
    Q_INVOKABLE bool setTransitionParam(int trackIndex, const QString &transitionId, const QString &key,
                                        double value);
    Q_INVOKABLE void previewSetTransitionParam(int trackIndex, const QString &transitionId,
                                               const QString &key, double value);
    Q_INVOKABLE QVariantMap transitionBetweenClips(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantList transitionKinds() const;
    // The kinds that make sense on this track: on an audio track only those with an audible
    // curve (crossfade, dip); everything elsewhere.
    Q_INVOKABLE QVariantList transitionKindsForTrack(int trackIndex) const;
    Q_INVOKABLE QVariantList transitionCategories() const;
    Q_INVOKABLE void selectTransition(int trackIndex, int leftClipIndex);
    Q_INVOKABLE void clearTransitionSelection();
    // A colour property ("text.color") fans out to its four channel tracks (<prop>.r/g/b/a) as
    // one undo step. Clears every channel when the colour is invalid.
    Q_INVOKABLE void setClipColorKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                          double atSeconds, const QColor &color);
    Q_INVOKABLE void setClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds,
                                     double value);
    Q_INVOKABLE void removeClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds);
    Q_INVOKABLE void previewMoveClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                             double fromSeconds, double toSeconds, double value);
    Q_INVOKABLE QVariantList clipKeyframes(int trackIndex, int clipIndex, const QString &prop) const;
    // Every property of the clip that carries an animation, in strip order.
    Q_INVOKABLE QStringList clipAnimatedProperties(int trackIndex, int clipIndex) const;
    // VEDIT (CapCut): the clip's keyframe moments, every animated property together, for the ◆
    // marks on the timeline clip — timeline seconds, ascending, one per moment.
    Q_INVOKABLE QVariantList clipKeyframeMarks(int trackIndex, int clipIndex) const;
    // VEDIT: drag a ◆ on the clip: every property's keyframe at `fromSeconds` moves to
    // `toSeconds` (kept inside the clip). One undo step.
    Q_INVOKABLE void moveClipKeyframesAt(int trackIndex, int clipIndex, double fromSeconds, double toSeconds);
    // VEDIT (조박사님, CapCut keyframes on the clip): one ◆ holds every value at that moment.
    // Add: position, size, rotation, opacity (and whatever else is already animated) are keyed at
    // `seconds` with the values they have there. Delete: every keyframe at that moment goes.
    Q_INVOKABLE void addClipKeyframesAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void deleteClipKeyframesAt(int trackIndex, int clipIndex, double seconds);
    // VEDIT (CapCut ◇ for a value made of several properties, e.g. 크기 = width, height, x, y):
    // state {animated, atPlayhead, prev, next} over all of them, and one-step add/remove at the
    // playhead.
    Q_INVOKABLE QVariantMap clipPropsKeyState(int trackIndex, int clipIndex, const QStringList &props) const;
    Q_INVOKABLE void toggleClipPropsKeyframe(int trackIndex, int clipIndex, const QStringList &props);
    // Whether a property's keyframes drive it. Switched off, the keys are kept but the property
    // holds its first key's value — the inspector row's label is what toggles this.
    Q_INVOKABLE bool clipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                  const QString &prop) const;
    Q_INVOKABLE void setClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                     const QString &prop, bool enabled);
    Q_INVOKABLE void toggleClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                        const QString &prop);
    Q_INVOKABLE double propertyValueAt(int trackIndex, int clipIndex, const QString &prop,
                                       double atSeconds, double fallback) const;
    // What the property evaluates to with no keyframes at all — the same implicit defaults
    // the compositor uses, so an unkeyed curve is drawn where the clip actually sits.
    Q_INVOKABLE double propertyBaseValue(int trackIndex, int clipIndex, const QString &prop,
                                         double fallback = 0.0) const;
    // Tangent handles for the curve editor. dx/dy arrive in seconds / property units, relative
    // to the key. `preview` variants coalesce a drag into one undo entry via begin/commitPreviewDrag.
    Q_INVOKABLE void setKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                         double atSeconds, double inDx, double inDy, double outDx,
                                         double outDy, bool corner);
    Q_INVOKABLE void previewSetKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                                double atSeconds, double inDx, double inDy,
                                                double outDx, double outDy, bool corner);
    Q_INVOKABLE void setKeyframeHold(int trackIndex, int clipIndex, const QString &prop,
                                     double atSeconds, bool hold);
    Q_INVOKABLE void setKeyframeInterpolation(int trackIndex, int clipIndex, const QString &prop,
                                              const QString &mode);
    Q_INVOKABLE void resetClipTransform(int trackIndex, int clipIndex);
    // VEDIT (조박사님): 속성 모두 지우기 — the clip back to how it came in: effects and
    // adjustments, keyframes, position/size/rotation/opacity, speed (its length follows), volume,
    // level, sound channels, fades, animations, masks, crop and sound effects. The media, its
    // place and its trim stay. One undo step.
    Q_INVOKABLE void clearClipAttributes(int trackIndex, int clipIndex);
    // VEDIT (Vegas volume envelope).
    QVariantMap selectedVolumeKey() const { return m_selectedVolumeKey; }
    // Picks the envelope point at `seconds` (and moves the playhead there, so the audio panel's
    // volume edits that point).
    Q_INVOKABLE void selectVolumeKey(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void clearVolumeKeySelection();
    int selectedTrackRow() const { return m_selectedTrackRow; } // VEDIT
    Q_INVOKABLE void selectTrackRow(int trackIndex); // VEDIT
    Q_INVOKABLE void clearTrackRowSelection(); // VEDIT
    // Double-click on the envelope: a point at `seconds` with `gain` (1.0 = 0 dB), selected.
    Q_INVOKABLE void addVolumeKey(int trackIndex, int clipIndex, double seconds, double gain);
    Q_INVOKABLE void deleteSelectedVolumeKey();
    Q_INVOKABLE QVariantList effectCatalog() const;
    Q_INVOKABLE QVariantList effectCategories() const;
    Q_INVOKABLE QVariantList effectTemplateCatalog() const;
    Q_INVOKABLE QVariantList effectTemplateCategories() const;
    Q_INVOKABLE void addEffect(int trackIndex, int clipIndex, const QString &effectId);
    // VEDIT: CapCut-style "Adjust" — one slider per colour control, straight on the selected clip.
    // Values live on the clip's effect host like any other effect (so the pro Effects view shows
    // them); a slider at its default removes nothing, it just leaves the effect neutral.
    Q_INVOKABLE QVariantMap clipAdjustValues(int trackIndex, int clipIndex);
    // VEDIT (CapCut ◇ on every 조정 slider): {animated, atPlayhead, prev, next} for one adjust
    // value (prev/next: timeline seconds of the neighbouring keyframes, -1 for none).
    Q_INVOKABLE QVariantMap clipAdjustKeyState(int trackIndex, int clipIndex, const QString &effectId,
                                               const QString &key);
    // ◇: a keyframe at the playhead with the value shown there, or — if there is one — take it out.
    Q_INVOKABLE void toggleClipAdjustKeyframe(int trackIndex, int clipIndex, const QString &effectId,
                                              const QString &key);
    // VEDIT (CapCut's ◇ at the top of the 조정 panel): a keyframe at the playhead for every value
    // listed ([{effect, key}]) — those already keyed there are left as they are. One undo step.
    Q_INVOKABLE void keyframeAllClipAdjust(int trackIndex, int clipIndex, const QVariantList &controls);
    // VEDIT: CapCut's 필터 — one look per clip, built from the bundled effects and scaled by
    // `strength` (0..1). "none" takes it off. One undo step.
    Q_INVOKABLE void applyClipFilter(int trackIndex, int clipIndex, const QString &filterId,
                                     double strength);
    // {"id": ..., "strength": ...} of the clip's look, id "none" when it has none.
    Q_INVOKABLE QVariantMap clipFilter(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 모두 적용 for a look — this clip's filter (and strength) on every photo and
    // video in the project, so the whole film has one colour. One undo step; returns clips changed.
    Q_INVOKABLE int applyFilterToAllPictures(int trackIndex, int clipIndex);
    // VEDIT: the same for 조정 — this clip's own adjustments (밝기, 대비, 노출 …, not the filter)
    // replace those of every other photo and video. One undo step; returns clips changed.
    Q_INVOKABLE int applyAdjustToAllPictures(int trackIndex, int clipIndex);
    // VEDIT: 모든 사진에 이 움직임 — this clip's motion (천천히 다가가기 …) on every other photo,
    // "slowly closer / farther" alternating like a slideshow. One undo step; returns photos changed.
    Q_INVOKABLE int applyMotionToAllPhotos(int trackIndex, int clipIndex);
    // VEDIT: CapCut's 콤보 애니메이션 — slow motion across the whole clip, the photo-slideshow
    // look: "zoomIn", "zoomOut", "panRight", "panLeft", or "none" to hold still. Writes two
    // position/size keys (clip start and end) from the clip's current framing. One undo step.
    Q_INVOKABLE void applyClipMotion(int trackIndex, int clipIndex, const QString &motionId);
    // VEDIT: CapCut's 화면 속 화면 in one press — the picture at a third of the screen's size in a
    // corner ("topLeft", "topRight", "bottomLeft", "bottomRight"), "center", or back to "full"
    // (fitted to the screen). Any motion on the clip is taken off. One undo step.
    Q_INVOKABLE void placeClipOnScreen(int trackIndex, int clipIndex, const QString &where);
    // VEDIT: 초록 배경 지우기 with a colour picker. beginChromaPick() arms the preview; a click there
    // calls pickChromaKeyAt (canvas fraction) which reads that point of the frame as it looks
    // without any keying and keys that colour out of the selected clip.
    Q_PROPERTY(bool chromaPickActive READ chromaPickActive NOTIFY chromaPickActiveChanged)
    bool chromaPickActive() const { return m_chromaPickActive; }
    Q_INVOKABLE void beginChromaPick();
    Q_INVOKABLE void cancelChromaPick();
    Q_INVOKABLE void pickChromaKeyAt(double nx, double ny);
    // Keys `hex` out of the clip (adding the key if it has none). False for a grey with no hue.
    Q_INVOKABLE bool applyChromaKeyColor(int trackIndex, int clipIndex, const QString &hex);
    Q_INVOKABLE void setClipAdjust(int trackIndex, int clipIndex, const QString &effectId,
                                   const QString &key, double value);
    Q_INVOKABLE void applyEffectTemplate(int trackIndex, int clipIndex, const QString &templateId);
    Q_INVOKABLE void removeEffect(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void setEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled);
    Q_INVOKABLE void moveEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex);
    Q_INVOKABLE bool setEffectParam(int trackIndex, int clipIndex, int effectIndex, const QString &key,
                                    double value);
    Q_INVOKABLE bool setEffectColorParam(int trackIndex, int clipIndex, int effectIndex,
                                         const QString &key, const QString &value);
    // File-path params (model3d .glb). Same commit-once path as colour — no preview stream.
    // Takes a QUrl like replaceAssetSource / importSubtitleFile so the portal and native
    // dialogs hand us a real local path without QML having to call toLocalFile().
    Q_INVOKABLE bool setEffectStringParam(int trackIndex, int clipIndex, int effectIndex,
                                          const QString &key, const QUrl &url);
    // Clip params ("clip" type, e.g. the clip Behind Subject sits in). Empty clears the choice
    // back to automatic.
    Q_INVOKABLE bool setEffectClipParam(int trackIndex, int clipIndex, int effectIndex,
                                        const QString &key, const QString &clipId);
    // What a clip param can point at: every video or image clip on a visible track beneath the
    // clip that overlaps it in time, nearest track first. [{id, name, track, clip, hasDepth}]
    Q_INVOKABLE QVariantList effectClipCandidates(int trackIndex, int clipIndex) const;
    // The clip a clip param resolves to right now — the chosen one, or with none chosen the
    // nearest video or image clip beneath at the playhead, which is what the renderer uses.
    // {explicit, id, name, track, clip, hasDepth}; empty when there is none.
    Q_INVOKABLE QVariantMap effectClipTarget(int trackIndex, int clipIndex, int effectIndex,
                                             const QString &key) const;
    Q_INVOKABLE QVariantList audioEffectCatalog() const;
    Q_INVOKABLE QVariantList audioEffectCategories() const;
    Q_INVOKABLE void addAudioEffect(int trackIndex, int clipIndex, const QString &effectId);
    Q_INVOKABLE void removeAudioEffect(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void setAudioEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled);
    Q_INVOKABLE void moveAudioEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex);
    Q_INVOKABLE bool setAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                         const QString &key, double value);
    Q_INVOKABLE void previewSetAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                                const QString &key, double value);

    // Effect stacks travel as JSON on the system clipboard, so a copy also crosses to a second
    // running instance. -1 for both indices means the whole clip.
    Q_INVOKABLE void copyEffectToClipboard(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void copyAudioEffectToClipboard(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void copyClipEffectsToClipboard(int trackIndex, int clipIndex);
    // Reading the clipboard is a synchronous round-trip to whichever process owns the selection,
    // so callers ask this when a menu opens rather than binding it.
    Q_INVOKABLE bool clipboardHasEffects() const;
    Q_INVOKABLE void pasteEffectsFromClipboard(int trackIndex, int clipIndex);

    // VEDIT: 특성 복사 — keeps every attribute of one clip (effects from its linked hosts too)
    // for 특성 붙여넣기, apart from the clip clipboard that 복사 fills.
    Q_INVOKABLE void copyAttributes(int trackIndex, int clipIndex);
    // VEDIT: the track the user last clicked (a clip on it, its header or its empty space) —
    // where a paste goes when more than one track has room.
    Q_INVOKABLE void noteTrackClicked(int trackIndex);
    // VEDIT (조박사님): a video clip's effects show as "FX" in its own band, not as a lane. These
    // reach the adjustment holding them: select it (its effects open in the inspector), or
    // 효과 줄 분리 — unlink it so it becomes a lane of its own that no longer follows the clip.
    Q_INVOKABLE bool selectEffectHost(int trackIndex, int clipIndex);
    Q_INVOKABLE bool separateEffectLane(int trackIndex, int clipIndex);
    // VEDIT: whether a lane has anything to draw as a strip — a lane holding only effect stacks
    // pinned to video clips is shown in those clips' bands instead.
    bool laneShownAsStrip(int laneIndex) const;
    Q_INVOKABLE bool canPasteAttributes() const;
    Q_INVOKABLE QVariantMap clipboardAttributes() const;
    Q_INVOKABLE void requestPasteAttributes();
    Q_INVOKABLE void pasteAttributes(const QVariantMap &options);

    Q_INVOKABLE QVariantList userEffectPresets() const;
    Q_INVOKABLE QString saveEffectAsPreset(int trackIndex, int clipIndex, int effectIndex,
                                           const QString &label);
    Q_INVOKABLE QString saveAudioEffectAsPreset(int trackIndex, int clipIndex, int effectIndex,
                                                const QString &label);
    Q_INVOKABLE QString saveClipEffectsAsPreset(int trackIndex, int clipIndex,
                                                const QString &label);
    Q_INVOKABLE void applyEffectPreset(int trackIndex, int clipIndex, const QString &presetId);
    Q_INVOKABLE bool renameUserEffectPreset(const QString &presetId, const QString &label);
    Q_INVOKABLE bool deleteUserEffectPreset(const QString &presetId);
    Q_INVOKABLE bool exportUserEffectPreset(const QString &presetId, const QUrl &fileUrl);
    Q_INVOKABLE bool importUserEffectPreset(const QUrl &fileUrl);
    Q_INVOKABLE void setTrackMuted(int trackIndex, bool muted);
    Q_INVOKABLE void setTrackHidden(int trackIndex, bool hidden);
    // VEDIT: CapCut's 트랙 잠금 — a locked track's clips cannot be selected, moved, cut or trimmed,
    // and nothing can be dropped onto it.
    Q_INVOKABLE void setTrackLocked(int trackIndex, bool locked);
    // VEDIT: 빈틈 닫기, per track (CapCut has it on its main track only; here any track can).
    Q_INVOKABLE void setTrackMagnet(int trackIndex, bool on);
    bool trackRipples(int trackIndex) const;
    bool trackIsLocked(int trackIndex) const;
    // Empty name clears the custom label, falling back to the type+position display
    // ("Video 1") again.
    Q_INVOKABLE bool renameTrack(int trackIndex, const QString &name);
    Q_INVOKABLE bool trackMuted(int trackIndex) const;
    Q_INVOKABLE bool trackHidden(int trackIndex) const;
    // 0 = filmstrip only, 1 = filmstrip + waveform bar, 2 = waveform only (Track::ClipDisplay).
    Q_INVOKABLE void setTrackClipDisplay(int trackIndex, int mode);
    Q_INVOKABLE int trackClipDisplay(int trackIndex) const;
    Q_INVOKABLE void setTrackShowChannelWaveforms(int trackIndex, bool show);
    Q_INVOKABLE bool trackShowChannelWaveforms(int trackIndex) const;
    // Per-track row height multiplier (DAW-style lane resize). Clamped to
    // trackHeightScaleMin()..trackHeightScaleMax().
    Q_INVOKABLE void setTrackHeightScale(int trackIndex, double scale);
    // VEDIT (조박사님): the corner size switch sets every row alike — each row's own size goes.
    Q_INVOKABLE void resetTrackHeightScales();
    Q_INVOKABLE double trackHeightScale(int trackIndex) const;
    Q_INVOKABLE void nudgeTrackHeightScale(int trackIndex, int steps);
    // The touch timeline has no per-lane resize handle and no room in the tool strip for a control
    // per lane, so its buttons move the whole stack. One tracksChanged for the sweep rather than
    // one per track — each emission relays the entire track list into QML.
    Q_INVOKABLE void nudgeAllTrackHeightScales(int steps);
    bool canGrowTrackHeights() const;
    bool canShrinkTrackHeights() const;
    // The row height the timeline should give a track, in pixels, including any nested
    // adjustment lanes drawn inside it. A lane returns 0: it has no row of its own, it is drawn
    // as a strip across the top of its parent's.
    //
    // This lives here rather than in QML because the rule stopped being a one-liner and was
    // duplicated verbatim in TimelinePanel, TrackHeaderColumn and AndroidTimeline — three copies
    // that had to agree or the headers would drift out of line with the rows.
    //
    // `metrics` carries Theme's row heights, so the numbers stay defined in one place there:
    // keys "video", "audio", "text", "subtitle", "shape", "adjustment" and "lane". Named rather
    // than positional because seven interchangeable doubles are silently mis-orderable.
    Q_INVOKABLE int trackRowHeight(int trackIndex, const QVariantMap &metrics) const;

    // How many nested lanes a track is carrying, so the delegate knows how many strips to draw.
    Q_INVOKABLE int adjustmentLaneCount(int trackIndex) const;
    // Track indices of those lanes, topmost first.
    Q_INVOKABLE QVariantList adjustmentLanes(int trackIndex) const;

    Q_INVOKABLE double trackHeightScaleMin() const { return 0.5; } // VEDIT: per-track "Small" under the corner's "Large"
    Q_INVOKABLE double trackHeightScaleMax() const { return 4.0; }
    Q_INVOKABLE void moveTrack(int fromIndex, int toIndex);
    Q_INVOKABLE void addTrack(const QString &type);
    // VEDIT (조박사님): right-click on a track's empty space → a new track of the same kind right
    // there (pictures above it, sounds below it).
    Q_INVOKABLE void addTrackNear(int trackIndex);
    Q_INVOKABLE void removeTrack(int trackIndex);
    Q_INVOKABLE void addBookmark(double seconds, const QString &label);
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE void updateBookmark(int index, double seconds, const QString &label);
    Q_INVOKABLE void goToBookmark(int index);
    // Seek to the next/previous bookmark by time (wraps). No-op when empty.
    Q_INVOKABLE void goToNextBookmark();
    Q_INVOKABLE void goToPreviousBookmark();
    // Seek to the next/previous cut point — any clip edge on any track, plus the two ends
    // of the timeline. Unlike the bookmark pair these clamp rather than wrap, so holding the
    // key walks to the first or last cut and stops there.
    Q_INVOKABLE void goToNextEdit();
    Q_INVOKABLE void goToPreviousEdit();
    // Add at the playhead, or remove the nearest bookmark when one already sits
    // within the snap threshold — same key for mark and unmark.
    Q_INVOKABLE void toggleBookmarkAtPlayhead();
    Q_INVOKABLE void removeBookmarkNearPlayhead();
    Q_INVOKABLE void markWorkAreaIn();
    Q_INVOKABLE void markWorkAreaOut();
    Q_INVOKABLE void goToWorkAreaIn();
    Q_INVOKABLE void goToWorkAreaOut();
    Q_INVOKABLE void clearWorkArea();
    Q_INVOKABLE void toggleLoopWorkArea();
    Q_INVOKABLE void freezeFrameAtPlayhead();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void pasteAtPlayhead();
    Q_INVOKABLE void nudgeSelection(double deltaSeconds);
    Q_INVOKABLE bool selectionContains(int trackIndex, int clipIndex) const;
    Q_INVOKABLE double selectionEarliestStartSeconds() const;
    // Premiere-style trim pointer. side: -1=start, 0=off, 1=end.
    // heightPx scales the cursor to the hovered clip/track height.
    // The trim pointer is an application-wide override cursor, but every clip delegate has an
    // opinion about it. `owner` says who is asking: a clear from anyone but the current owner is
    // ignored, so a neighbouring clip's hover ending cannot drop the cursor of the clip actually
    // being trimmed.
    Q_INVOKABLE void setTimelineTrimCursor(int side, int heightPx = 0, int owner = 0);
    Q_INVOKABLE int acquireTrimCursorToken() { return ++m_trimCursorTokenSeq; }
    Q_INVOKABLE QString shortcutFor(const QString &actionId) const;
    // Returns an empty string on success, or the label of the action already bound to
    // `keys` when the binding is refused. Qt resolves an ambiguous application
    // shortcut by firing *neither* action, so a silent double-binding would break
    // both with no indication anywhere.
    Q_INVOKABLE QString setShortcut(const QString &actionId, const QString &keys);
    // Restores every default binding. Backspace clears a binding and persists the
    // empty string, so without this there was no route back from having cleared one.
    Q_INVOKABLE void resetShortcuts();
    // Canonical chord string for a key event (Ctrl+Shift+Left, …). Same names the
    // Shortcut items and the shortcut-capture field use, so a rebound chord still
    // matches when it is dispatched from Keys.onPressed rather than Shortcut {}.
    Q_INVOKABLE QString shortcutChord(int key, int modifiers) const;
    // The action bound to this arrow-key chord, or an empty string when the key is
    // not an arrow or nothing claims it. ApplicationShortcut matching for Left/Right
    // is unreliable on some Wayland compositors (Hyprland / Omarchy), and would also
    // steal the keys from text fields, so the editor resolves those chords itself
    // after the focused item. Main.qml dispatches the result through the same
    // function the Shortcut items use — a few ids are QML state with no
    // triggerAction branch, and calling triggerAction directly would do nothing.
    Q_INVOKABLE QString actionForArrowChord(int key, int modifiers) const;
    Q_INVOKABLE void triggerAction(const QString &actionId);
    // Per-tab favorites in the assets panel (effects, sounds, shapes, stickers, transitions, templates).
    Q_INVOKABLE bool isAssetFavorite(const QString &tabId, const QString &itemId) const;
    Q_INVOKABLE void toggleAssetFavorite(const QString &tabId, const QString &itemId);
    Q_INVOKABLE void togglePlayback();
    // Frame-accurate transport. Stepping quantizes to the project's frame grid first: the playhead
    // can sit between frames after a scrub, and adding a frame duration to that would carry the
    // off-grid offset forever.
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE void jumpSeconds(double seconds);
    // The transport's jump buttons pick their amount from the modifiers held at click time, and
    // AbstractButton::clicked carries none.
    Q_INVOKABLE int keyboardModifiers() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // `excludeClipId` keeps a clip from snapping to its own edges while it is being dragged —
    // the model still has it at its old position, so those two targets sit right under the
    // pointer at the start of every move.
    Q_INVOKABLE double snapTime(double seconds, const QString &excludeClipId = {}) const;
    Q_INVOKABLE QVariantList waveformPeaks(const QString &path) const;
    // Whole-file peaks sliced to a source window, for a dialog whose x axis is a clip's trimmed
    // range rather than the whole file. Shares the dense cache and the waveformReady signal with
    // waveformPeaks() — unlike waveformPeaksRange(), which is block-backed for the timeline.
    Q_INVOKABLE QVariantList waveformPeaksForSourceRange(const QString &path, double startSeconds,
                                                         double durSeconds) const;
    // Peaks for just the source window [startSeconds, startSeconds + durSeconds), reduced to
    // `buckets` values, so a clip only ever asks for as many peaks as it has visible pixels.
    Q_INVOKABLE QVariantList waveformPeaksRange(const QString &path, double startSeconds,
                                                double durSeconds, int buckets,
                                                int audioStreamIndex = 0) const;
    // Every channel of the stream over the same window, for stacked per-channel lanes.
    // Channel-major and flat — peaks[c * buckets + b] — so one array crosses into QML rather
    // than one per channel, and the paint loop indexes it without unpacking.
    // { "channels": int, "buckets": int, "names": QStringList, "peaks": QVariantList }.
    // channels == 0 means nothing has decoded for this stream yet, not mono: until a block
    // lands the caller keeps drawing the merged lane.
    Q_INVOKABLE QVariantMap waveformChannelPeaksRange(const QString &path, double startSeconds,
                                                      double durSeconds, int buckets,
                                                      int audioStreamIndex = 0) const;
    // Channels the decoder found for a source, 0 until a block has landed. Answered from the
    // block cache — never probes the file, so it is safe from a binding or a menu.
    Q_INVOKABLE int waveformChannelCount(const QString &path, int audioStreamIndex = 0) const;
    // VEDIT: "mono" | "stereo" | "left" | "right" | "multi" | "" (not decoded yet).
    Q_INVOKABLE QString waveformChannelLayout(const QString &path, int audioStreamIndex = 0) const;
    // Widest channel count over a track's clips, for sizing the row when the lanes turn on.
    Q_INVOKABLE int trackMaxChannelCount(int trackIndex) const;
    // title / author / description / createdAt / modifiedAt, for the properties dialog.
    QVariantMap projectMetadata() const;
    Q_INVOKABLE void setProjectMetadata(const QString &title, const QString &author,
                                        const QString &description);
    bool packaging() const { return m_packaging; }
    double packageProgress() const { return m_packageProgress; }
    Q_INVOKABLE QVariantList subtitleWaveformPeaks(double startSeconds, double durSeconds,
                                                   int sampleCount = 240) const;
    // Beat / onset detection over the mixed timeline audio in [startSeconds, +durSeconds).
    // Explicitly triggered from the keyframe strip: it renders the mix, so it must never
    // fire off a mere selection change. Result lands in `beatAnalysis`.
    Q_INVOKABLE void analyzeBeats(double startSeconds, double durSeconds);
    // VEDIT: CapCut's 비트 맞추기. Finds the beats of this music clip (in the background), shows
    // them on the ruler, and then cuts the photos on the main track on the beat.
    Q_INVOKABLE void matchPhotosToMusicBeats(int trackIndex, int clipIndex);
    // The cutting step on its own, from beat times in timeline seconds. Photos run about three
    // seconds, each ending on a beat; what follows on the track moves with them. One undo step.
    // Returns how many photos changed.
    Q_INVOKABLE int fitPhotosToBeatTimes(const QVariantList &beats);
    Q_INVOKABLE void clearBeatAnalysis();
    QVariantMap beatAnalysis() const { return m_beatAnalysis; }
    bool beatAnalysisRunning() const { return m_beatAnalysisRunning; }
    bool beatGridVisible() const { return m_beatGridVisible; }
    void setBeatGridVisible(bool visible);
    bool onsetsVisible() const { return m_onsetsVisible; }
    void setOnsetsVisible(bool visible);
    // Writes a .drift bundle keeping each asset's current storage mode, so a referencing project
    // stays instant to save and a packaged one stays self-contained.
    Q_INVOKABLE void saveProject(const QUrl &url);
    // Save As: the same write, but the copy gets its own project id and takes its title from the
    // chosen file name, and the open document only adopts that identity once the write lands. The
    // file it was opened from is never touched, so the original stays as it was on disk and the
    // session carries on in the duplicate — which is the point of the command.
    Q_INVOKABLE void saveProjectAs(const QUrl &url);
    // Same container, every source asset embedded. Runs off the GUI thread — it copies the media.
    Q_INVOKABLE void packageProject(const QUrl &url);
    // Export-only: the raw document JSON, no container and no media. Leaves the open project's
    // path, dirty flag and recents alone — the .drift stays the project of record.
    Q_INVOKABLE void saveProjectJson(const QUrl &url);
    // Inverse of saveProjectJson. Replaces the open timeline from that document; media stays as
    // referenced paths. Does not become the project of record (no recents, empty path, dirty) so
    // Save cannot overwrite the .json with a .drift bundle. loadProject routes here when the file
    // is JSON, so a dropped / CLI / MCP path works without a second entry point.
    Q_INVOKABLE void loadProjectJson(const QUrl &url);
    // Imports an Adobe Premiere Pro project (.prproj) or Final Cut Pro XML (.xml),
    // mapping sequences, video/audio tracks, clips, in/out trimming, and media assets.
    Q_INVOKABLE void loadPremiereProject(const QUrl &url);
    // Unpacks and imports a Motion Graphics Template (.mogrt), extracting assets and mapping
    // editable text, colors, and media overlays onto the timeline and media library.
    Q_INVOKABLE void importMogrt(const QUrl &url);
    // Imports a Kdenlive (.kdenlive) or Shotcut MLT (.mlt) project, mapping
    // multitrack playlists, video/audio cuts, title text clips, and bin folders.
    Q_INVOKABLE void loadKdenliveProject(const QUrl &url);
    // Imports a DaVinci Resolve project (.drp) or Final Cut Pro X XML (.fcpxml).
    Q_INVOKABLE void loadResolveProject(const QUrl &url);
    // Imports a CMX 3600 Edit Decision List (.edl).
    Q_INVOKABLE void loadEdlTimeline(const QUrl &url);
    // Imports an OpenTimelineIO (.otio) sequence.
    Q_INVOKABLE void loadOtioTimeline(const QUrl &url);
    Q_INVOKABLE void cancelPackage();
    Q_INVOKABLE void loadProject(const QUrl &url);
    // silent skips the "New project" status message — used by Close Project, which
    // reuses this reset but reports its own "Project closed" message instead; setting
    // lastMessage twice would queue two toasts, since each change is its own toast.
    Q_INVOKABLE void newProject(bool silent = false);
    Q_INVOKABLE void openRecentProject(const QString &path);
    Q_INVOKABLE void clearRecentProjects();
    // Removes one path from the recents list without deleting the file on disk.
    Q_INVOKABLE void removeRecentProject(const QString &path);
    Q_INVOKABLE void restoreAutosave();
    // VEDIT: CapCut keeps every project as a draft without asking. When on (the default), a dirty
    // project is saved to its file quietly every autosave tick; a project that was never saved
    // gets a file under Documents/Easy Cut, Easy Go! named after today. Returns true if it saved.
    Q_INVOKABLE bool autoSaveNow();
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveEnabledChanged)
    // VEDIT: light preview copies made automatically for big videos.
    Q_PROPERTY(bool autoProxies READ autoProxies WRITE setAutoProxies NOTIFY autoProxiesChanged)
    bool autoProxies() const;
    void setAutoProxies(bool on);
    bool autoSaveEnabled() const { return m_autoSaveEnabled; }
    void setAutoSaveEnabled(bool on);
    // Where never-saved projects go; tests point it at a temporary folder.
    static inline QString s_autoSaveDirOverride;
    Q_INVOKABLE void discardAutosave();
    // Clears dirty + recovery without mutating the timeline. Used when the user
    // chooses Don't Save before quitting so the next launch does not offer restore.
    Q_INVOKABLE void discardUnsavedChanges();
    // When reopenLastProject is on: restore recovery silently, else load lastSessionPath.
    // Returns true if a restore/load was started (caller should skip RecoveryDialog).
    Q_INVOKABLE bool restoreLastSessionIfEnabled();
    // The autosave timer and aboutToQuit cover desktop, but Android never emits aboutToQuit when
    // the OS reclaims a backgrounded process — and backgrounding is how a phone app normally ends.
    // The shell calls this on the way out so the floor is the last edit, not the last 15s tick.
    Q_INVOKABLE void flushRecoverySnapshot();
    // Drops every cache that exists only to make the next composite faster — decoder workers,
    // still images, rasterised text, uploaded textures and the FBO pool. All of it is rebuilt on
    // demand, and a backgrounded app that hangs on to it is the one the OS picks to kill first.
    // Only for the leaving-foreground handler: calling it while active throws away exactly what
    // the current composite is about to reuse.
    Q_INVOKABLE void releaseTransientCaches();
    // First non-flag positional argument as a local file URL (paths, file://, portal URIs).
    static QUrl startupProjectUrlFromArguments(const QStringList &args);
    // argv / QFileOpenEvent. Queued until consumeStartupProject(); after that, emits
    // externalProjectOpenRequested so QML can confirm unsaved work.
    void queueExternalProject(const QUrl &url);
    // Load a queued startup document. True if a load started (skip recovery / last session).
    Q_INVOKABLE bool consumeStartupProject();
    Q_INVOKABLE QVariantList exportPresets() const; // legacy scale ids/labels
    Q_INVOKABLE QVariantList exportScaleOptions() const;
    // Frame rate choices; the "project" entry is labelled with the current project fps.
    Q_INVOKABLE QVariantList exportFrameRateOptions() const;
    Q_INVOKABLE QVariantList exportVideoCodecs() const;
    Q_INVOKABLE QVariantList exportAudioCodecs() const;
    Q_INVOKABLE bool exportGifAvailable() const;
    Q_INVOKABLE QVariantMap exportDefaultSettings() const;
    // Last dialog choices (scale + encode). Empty until the user has exported once.
    Q_INVOKABLE QVariantMap lastExportSettings() const;
    // Directory of the last successful save-picker choice; empty if never set or gone.
    Q_INVOKABLE QString lastExportFolder() const;
    Q_INVOKABLE QString exportPreferredContainer(const QString &videoCodecId,
                                                const QString &audioCodecId) const;
    Q_INVOKABLE QString exportPreferredAudioOnlyContainer(const QString &audioCodecId) const;
    Q_INVOKABLE QStringList exportSaveFilters(const QString &container, bool audioOnly = false) const;
    Q_INVOKABLE QString exportDefaultSuffix(const QString &container, bool audioOnly = false) const;
    Q_INVOKABLE void exportProject(const QUrl &outputUrl);
    Q_INVOKABLE void exportWithPreset(const QUrl &outputUrl, const QString &presetId);
    // `rememberChoice` is what keeps an agent export out of the export dialog's memory: the GUI
    // stores what the user picked, an MCP export keeps its own (see mcpRememberExportSettings), and
    // neither reaches into the other's.
    Q_INVOKABLE void exportWithSettings(const QUrl &outputUrl, const QVariantMap &settings,
                                        bool rememberChoice = true);
    Q_INVOKABLE void cancelExport();
    // Copies the finished export into the shared media collection and hands it to the system share
    // sheet. Deferred to this point rather than done as part of the export because it is a second
    // full copy of the video, and most exports are never shared. Android only; false/no-op elsewhere.
    Q_INVOKABLE void shareLastExport();
    // Same publish-to-gallery step as shareLastExport, handed to a player instead of a share
    // sheet. Shares the m_sharingExport guard, so the two cannot run the copy twice at once.
    Q_INVOKABLE void playLastExport();
    // Copies a file into the shared media collection (Movies/Music/Pictures under "Drift") so it
    // outlives the app's own storage. Marketplace downloads land in AppDataLocation, which is gone
    // on uninstall or a "clear data" and invisible to every file manager — for something the user
    // spent quota on, that is a file they can lose without ever having seen it.
    //
    // Asynchronous: this is a second full copy of the media, and doing it inline is an ANR on
    // anything long. Reports through savedToGallery. Android only; a no-op elsewhere, where the
    // download already went somewhere the user picked.
    Q_INVOKABLE void saveToGallery(const QString &filePath, const QString &displayName);
    Q_INVOKABLE QUrl fileUrl(const QString &path) const;
    Q_INVOKABLE QString imageUrl(const QString &path) const;
    // Same as imageUrl but requests a single frame of a filmstrip strip (see DriftImageProvider).
    Q_INVOKABLE QString filmstripFrameUrl(const QString &path, int frame, int count) const;
    // Sharp on-demand frame for one filmstrip tile — see FilmstripTileCache. Empty until the
    // decode lands, at which point filmstripTileReady() fires for that source.
    Q_INVOKABLE QString filmstripTileUrl(const QString &path, int level, double index,
                                         int rotationCorrection = 0) const;

signals:
    void userTextAnimationPresetsChanged();

    // A text clip was added with no text; the preview should open its inline
    // editor so the user can type straight onto the canvas.
    void inlineTextEditRequested(int trackIndex, int clipIndex);
    void inlineTextEditingChanged();
    void externalProjectOpenRequested(const QUrl &url);
    void tracksChanged();
    // A drag wrote these properties in place without announcing tracksChanged; the full refresh
    // follows on commit. Keys are the propertyValueAt names ("x", "fx.2.light1_x"); one ending
    // in ".*" covers every key under that prefix.
    void clipPropertiesPreviewed(int trackIndex, int clipIndex, const QStringList &keys);
    void previewDragActiveChanged();
    void playheadSecondsChanged();
    void playingChanged();
    void renderCacheChanged(); // VEDIT
    void selectedVolumeKeyChanged(); // VEDIT
    void selectedTrackRowChanged(); // VEDIT
    void audioOutputDevicesChanged();
    void audioOutputDeviceIdChanged();
    void snapEnabledChanged();
    void rippleEnabledChanged();
    void allowClipOverlapChanged();
    void darkModePreferenceChanged();
    void workspaceLayoutPreferenceChanged();
    void mediaGridModeChanged();
    void mediaViewModeChanged();
    void autoKeyEnabledChanged();
    void timelineOverviewVisibleChanged();
    void trackLabelsWidthChanged();
    void timelineToolbarLayoutChanged();
    void reopenLastProjectChanged();
    void vaapiZeroCopyChanged();
    void mediaCodecZeroCopyChanged();
    void preferredGpuChanged();
    // Carries the finished benchmark, merged into whatever the dialog already collected.
    void playbackBenchmarkFinished(const QVariantMap &info);
    void playbackBenchmarkRunningChanged();
    void invertTimelineScrollChanged();
    void mcpRunningChanged();
    void mcpErrorChanged();
    void mcpStartOnLaunchChanged();
    void uiLanguageChanged();
    void uiScaleChanged();
    void keyframeGraphVisibilityChanged();
    void subtitleEditingChanged();
    void selectedSubtitleCueChanged();
    void undoStackChanged();
    void exportInProgressChanged();
    void exportProgressChanged();
    void canShareExportChanged();
    // `location` is a human-readable folder ("Movies/Drift"), empty when ok is false.
    void savedToGallery(const QString &displayName, bool ok, const QString &location,
                        const QString &error);
    void subtitleGeneratingChanged();
    void subtitleGenProgressChanged();
    void subtitleGenStatusChanged();
    void subtitleGenerationFinished(bool ok, const QString &message);
    void segmentingChanged();
    void segmentProgressChanged();
    void segmentStatusChanged();
    void reverseRenderingChanged();
    void reverseRenderProgressChanged();
    void reverseRenderStatusChanged();
    void reverseRenderFinished(bool ok, const QString &message);
    void reverseConfirmRequested(int trackIndex, int clipIndex, double seconds);
    void segmentationFinished(bool ok, const QString &message);
    void denoisingChanged();
    void denoiseProgressChanged();
    void denoiseStatusChanged();
    void denoisePreviewReady(const QString &originalPath, const QString &cleanPath);
    void denoiseFinished(bool ok, const QString &message);
    void segmentSessionChanged();
    void openSegmentationWindowRequested(int trackIndex, int clipIndex, double startSeconds,
                                         double durationSeconds);
    void multicamChanged();
    void multicamFramesChanged();
    // Raised by the "multicam" shortcut/action. QML owns the window, as with the file actions.
    void openMulticamWindowRequested();
    void speedCurveSessionChanged();
    void assetPreviewSessionChanged();
    void assetPreviewFrameChanged();
    void assetPreviewPositionChanged();
    void assetPreviewPlayingChanged();
    void speedCurveChanged();
    void speedCurveFrameChanged();
    void speedCurvePositionChanged();
    void speedCurvePlayingChanged();
    void speedCurveApplied();
    void fadeCurveSessionChanged();
    void fadeCurveChanged();
    void fadeCurveApplied();
    void transitionCurveSessionChanged();
    void transitionCurveChanged();
    void transitionCurveApplied();
    void faceDetectingChanged();
    void depthJobChanged(const QString &clipId);
    void faceDetectProgressChanged();
    void faceDetectStatusChanged();
    void faceDetectionFinished(bool ok, const QString &message);
    void scenesChanged();
    void sceneDetectingChanged();
    void sceneDetectProgressChanged();
    void sceneDetectStatusChanged();
    void sceneDetectionFinished(bool ok, const QString &message);
    void currentBinFolderIdChanged();
    void selectionChanged();
    void editCapabilitiesChanged();
    void audioSyncChanged(); // VEDIT: 소리로 맞추기 progress
    void timelineClipSizeChanged(); // VEDIT
    void autoSaveEnabledChanged(); // VEDIT
    void autoProxiesChanged(); // VEDIT
    void loudnessBusyChanged(); // VEDIT
    void audioToolBusyChanged(); // VEDIT
    void voiceRecordingChanged(); // VEDIT
    void voiceCountdownChanged(); // VEDIT
    void chromaPickActiveChanged(); // VEDIT
    void voiceLevelChanged(); // VEDIT
    void clipLoudnessMatched(bool ok); // VEDIT: for tests
    void sequenceTabsChanged();
    void selectedClipDataChanged();
    void selectedTransitionDataChanged();
    void bookmarksChanged();
    void workAreaChanged();
    void loopWorkAreaEnabledChanged();
    void projectNameChanged();
    void projectMetadataChanged();
    void packagingChanged();
    void packageProgressChanged();
    void packageFinished(bool ok, const QString &message);
    // Save completion when saveProject took the Android streaming path (see saveProject). Never
    // emitted on desktop or for a plain, synchronous save — setLastMessage already covers those.
    void projectSaved(bool ok);
    // Addons the freshly opened project needs but that are not installed. Each entry is
    // id / name / version / kinds, for MissingAddonsDialog.
    void missingAddons(const QVariantList &addons);
    // Terminal result of loadProject()/loadProjectJson(): exactly one per call that
    // reaches a load generation still current when it finishes. A bundle with embedded
    // media raises the "Unpacking project media…" lastMessage first and this only once
    // extraction and apply are done — QML waiting to know whether an open landed (e.g.
    // to dismiss a "pick a project" screen) needs this rather than lastMessageChanged,
    // which fires for that progress message too.
    void projectLoadFinished(bool ok, const QString &message);
    void projectLoadPendingChanged();
    void lastMessageChanged();
    void draggingAssetIndexChanged();
    void assetPreviewWindowOpenChanged();
    void exportFinished(bool success);
    void projectMutated();
    void waveformReady(const QString &path);
    // A block of the timeline waveform landed. Separate from waveformReady so the dialogs,
    // which want the whole file, don't re-fetch (and blank) on every block.
    void waveformRangeReady(const QString &path);
    void filmstripTileReady(const QString &path);
    void subtitleWaveformReady(double startSeconds, double durSeconds, int sampleCount);
    void beatAnalysisChanged();
    void guidesChanged();
    void shortcutsChanged();
    void assetFavoritesChanged();
    void userTextPresetsChanged();
    void userEffectPresetsChanged();
    void canvasCropModeChanged();
    void maskEditModeChanged();
    void maskEditActiveChanged();
    void backgroundChanged();
    void dirtyChanged();
    void currentProjectPathChanged();
    void recoveryChanged();
    void recentProjectsChanged();
    void projectLayoutChosenChanged();
    // The document has been swapped wholesale (New Project, or opening another one). The
    // auxiliary windows edit one clip each, so they have nothing left to act on and close.
    void projectReset();
    void transformBlocked(const QString &reason);
    // Outcome of replaceAssetSource. `message` is a ready-to-show reason on failure and the new
    // media's name on success. `adjustedClips` counts clips whose source range no longer fitted
    // the replacement and was pulled back to it.
    void assetReplaceFinished(bool ok, const QString &message, int adjustedClips);
    void importingFolderChanged();
    // Outcome of importFolder: how many bin folders and assets it actually created, how many
    // files it passed over as unrecognized, and whether the walk stopped at the file limit with
    // more still on disk.
    void folderImportFinished(int folders, int files, int skipped, bool truncated);
    void replacingAssetIdChanged();
    void assetEditChanged();
    void assetEditFinished(bool ok, const QString &message);
    // File actions from the shortcut layer — QML owns dialogs and unsaved prompts.
    void newProjectRequested();
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void openPasteAttributesRequested();

protected:
    // Every path that changes the timeline model goes through this instead of a bare
    // `emit tracksChanged()`. The cache has to be dropped *before* the signal goes out: whether
    // a slot connected in the constructor runs ahead of QML's binding re-evaluation is not a
    // guarantee worth depending on, and getting it wrong would serve QML one stale rebuild per
    // emission — intermittent, and indistinguishable from a model bug.
    // The trim math, factored out so the live preview and the commit cannot drift apart: both
    // call this, one discards the result and one writes it back. `clip` is the clip as it would
    // be after the trim; `changed` is false when the edge had nowhere to go.
    struct TrimComputation {
        bool ok = false;
        bool changed = false;
        int outcome = 0;
        drift::Clip clip;
    };
    TrimComputation computeTrimLeft(int trackIndex, int clipIndex, double newStart) const;
    // VEDIT: see showTrimEdge().
    QString m_trimEdgeClipId;
    drift::Clip m_trimEdgeOriginal;
    drift::TimeUs m_trimEdgePlayheadUs = -1;
    TrimComputation computeTrimRight(int trackIndex, int clipIndex, double newEnd) const;
    int applyTrim(int trackIndex, int clipIndex, const TrimComputation &computed);
    static QVariantMap trimPreviewToMap(const TrimComputation &computed);

    void notifyTracksChanged();

    // Refills the per-track clip models from the project. Called by notifyTracksChanged() before
    // the signal goes out, so a binding that wakes on tracksChanged already sees the new rows.
    void syncClipModels();
    TimelineClipsModel::Row clipRow(const drift::Clip &clip, const drift::Clip *videoEffectHost,
                                    const drift::Clip *audioEffectHost) const;
    // Holds tracksChanged for the length of an operation that touches the project several times.
    // The caches are still dropped immediately, so anything reading tracks() inside the batch
    // sees fresh data -- only the notification waits, and only one goes out.
    void beginTracksBatch() { ++m_tracksBatchDepth; }
    void endTracksBatch();
    int m_tracksBatchDepth = 0;
    bool m_tracksBatchPending = false;
    // Same rule as notifyTracksChanged(): bump the revision before the signal goes out, so no
    // reader can observe the old value on the new selection.
    void notifySelectionChanged();

    void pushProjectEdit(const drift::Project &before, const QString &text);

    // Lifts one effect, one audio effect, or the whole stack off a clip. Every copy and
    // save-as-preset entry point funnels through this, so all of them produce one payload shape.
    drift::EffectStackPreset effectStackFor(int trackIndex, int clipIndex, int effectIndex,
                                            int audioEffectIndex) const;
    // Rescales, rebuilds against the catalog, then appends. Shared by paste and preset-apply.
    void applyEffectStack(int trackIndex, int clipIndex, const drift::EffectStackPreset &stack,
                          const QString &undoLabel);
    void copyEffectStack(const drift::EffectStackPreset &stack, const QString &message);
    QString saveEffectStack(const drift::EffectStackPreset &stack, const QString &label);
    static drift::EffectStackPreset effectStackOnClipboard();
    void finishEdit(const QString &message);
    // Applies a finished replace probe as one undoable transaction, or reports why it cannot be.
    void finalizeAssetReplace(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // Moves every clip bound to `assetId` onto the replacement media. Returns how many had a
    // source range that no longer fitted and had to be pulled back to it.
    int rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset, int oldBinCorrection);
    // Keeps the keyframe strip's index-addressed hidden series in sync after an effect is removed.
    void dropKeyframeGraphPropertiesForEffect(int removedIndex);
    // Same idea after a reorder: fx.N.* indices move with the effect.
    void remapKeyframeGraphPropertiesForEffectMove(int fromIndex, int toIndex);
    // Publishes a finished beat analysis into m_beatAnalysis / m_beatSnapTargets.
    void applyBeatAnalysis(const AudioBeatAnalysis &analysis, double startSeconds, double durSeconds,
                           const QByteArray &fingerprint);
    // Builds m_shortcuts from the defaults plus whatever QSettings holds, migrating
    // stored values that a new release has superseded and dropping any default that
    // would end up sharing a chord with a stored one.
    void loadShortcuts();
    void loadAssetFavorites();
    void saveAssetFavorites(const QString &tabId);
    void applyEffectTemplateInternal(int trackIndex, int clipIndex, const EffectTemplateEntry &entry,
                                   const QString &mattePath = {},
                                   drift::TimeUs matteSrcOffsetUs = 0);
    bool resolveTemplateApplyTarget(int *trackIndex, int *clipIndex) const;
    bool beatAnalysisReadyForClip(const drift::Clip &clip, const QString &sync) const;
    // Digest of everything the AudioMixer reads; a change means detected beats are stale.
    QByteArray audioLayoutFingerprint() const;
    // Single key lookup for the tangent editors; null when nothing sits at `atSeconds`.
    drift::Keyframe<double> *keyframeAt(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds);
    static void applyTangents(drift::Keyframe<double> &key, double inDx, double inDy, double outDx,
                              double outDy, bool corner);
    // Recollects m_beatSnapTargets from whichever layers are currently visible.
    void rebuildBeatSnapTargets();
    // Beat onsets plus project bookmarks — anything clips should magnet to when snap is on.
    QList<drift::TimeUs> extraSnapTargets() const;
    // Same list, rebuilt only when the beat grid, bookmarks or the work area move. It has no
    // clip dependency, so an edit to the timeline does not invalidate it.
    const QList<drift::TimeUs> &extraSnapTargetsCached() const;
    void invalidateExtraSnapTargets() { m_extraSnapTargetsValid = false; }
    // Snaps against the vector pinned for the current trim gesture, falling back to a one-shot
    // build when called outside one.
    drift::TimeUs snapTimeForGesture(drift::TimeUs rawUs) const;
    // Decodes one frame per angle at the playhead and publishes them to MulticamImageStore.
    // Coalesces: a refresh requested while one is in flight is dropped, not queued.
    void refreshMulticamTiles();
    // Drops in-flight tile decodes. The worker captures `this`, so teardown has to wait
    // for it before AppController members disappear.
    void waitForMulticamRefresh();
    // Playhead rounded to the project's frame grid — a switch must land on a frame boundary,
    // the same rule stepFrames() follows.
    drift::TimeUs multicamSwitchTimeUs() const;
    bool startMulticamPunching(const QList<QPair<int, int>> &videoClips);
    void rebuildMulticamStaged();
    void applyMulticamSlicesToProject(drift::Project &project, bool combined);
    void refreshSegmentationPreview();
    void runSegmentationSeed(int generation);
    void finalizeDepth(const QString &clipId, const QString &path);
    void finalizeFaceDetection(const QString &clipId, const QString &trackPath,
                               drift::TimeUs srcOffsetUs);
    // Landmark a Face Swap source photo in the background and cache the result. Cheap enough
    // (one still, sub-second once the session is warm) that it gets no progress UI of its own —
    // the effect renders pass-through until the landmarks land, then the preview refreshes.
    void ingestFaceSwapSource(const QString &photoPath);
    // Every Face Swap photo in the project that has no cached landmarks. Runs on open, because
    // the sidecar is derived and does not travel with the bundle.
    void ingestFaceSwapSourcesInProject();
    // The one place a scan request is built, so the GUI and MCP paths cannot disagree about
    // the settings — and therefore about the cache key derived from them.
    drift::SceneDetectRequest sceneRequestFor(const drift::Clip &clip, bool withObjects,
                                              double minSceneSeconds) const;
    // Publishes a finished scene analysis into m_scenes, shaped for QML.
    void applySceneAnalysis(const drift::SceneAnalysis &analysis, const QString &clipId,
                            const QString &clipPath, int rotationCorrection);
    // Completes a segmentation job: pins the matte to the clip as a Mask adjustment on its own
    // lane. `outputMode` is kept only so the older "clips"/"mask" spellings stay accepted; all
    // three now produce the same mask layer.
    void finalizeSegmentation(const QString &clipId, const QString &mattePath,
                              const QString &matteFgrPath,
                              drift::TimeUs matteSrcOffsetUs, const QString &outputMode);
    struct SubtitleSource
    {
        QString path;
        drift::TimeUs srcIn = 0;
        drift::TimeUs srcOut = 0;
        drift::TimeUs timelineStart = 0;
        drift::TimeUs timelineDuration = 0;
        double speed = 1.0;
        bool reverse = false;
        QString assetId;
    };
    static SubtitleSource subtitleSourceFromClip(const drift::Clip &clip);
    // Captions straight from stored transcripts, when every source has a current one. Empty
    // when any source would need transcribing.
    std::optional<QList<drift::SubtitleCue>> cuesFromStoredTranscripts(const QList<SubtitleSource> &sources,
                                                                       drift::TimeUs rangeStart,
                                                                       int maxWordsPerCue) const;
    bool generateSubtitlesForSources(QList<SubtitleSource> sources, const QString &language,
                                     int maxWordsPerCue);
    void finalizeGeneratedSubtitles(drift::TimeUs timelineStart, drift::TimeUs timelineDuration,
                                    const QList<drift::SubtitleCue> &cues);
    void finalizeDenoise(const QString &clipId, const QString &audioPath);
    void watchStabilizeProgress(QProcess *process, const QString &clipId, qint64 durationUs,
                                double rangeFrom, double rangeTo);
    void setStabilizeProgress(const QString &clipId, double progress, const QString &status,
                              bool force);
    void clearStabilizeProgress(const QString &clipId);
    // Shared body of the two denoise jobs: decodes [srcIn, srcIn + span) of `path` at the model's
    // rate, runs each channel through it, and writes the result. Runs on a worker thread.
    // `originalPathOut` is written only when non-empty, for the preview's A/B source.
    bool renderDenoisedAudio(const QString &path, drift::TimeUs srcIn, drift::TimeUs span,
                             const QString &outPath, const QString &originalPath,
                             double progressFrom, double progressTo, QString *errorOut);
    // Defaulted severity so the existing call sites, which report ordinary status,
    // stay unchanged; pass "error"/"warning" explicitly where a failure is reported.
    Q_INVOKABLE void setLastMessage(const QString &message,
                                    const QString &severity = QStringLiteral("info"));
    drift::TimeUs playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(drift::TimeUs us);

    // Stickers and emoji are both a PNG dropped on an image track at the playhead.
    void addImageOverlayClip(const QString &path, const QString &name, const QString &emoji,
                             double atSeconds, const QString &undoText,
                             int requestedTrack = -1);

    // The inspector's and the MCP tools' view of a clip: every field, including the sub-maps
    // only an open inspector reads. The timeline strip does NOT come through here — it reads
    // clipRow() through TimelineClipsModel, which is what keeps an edit from rebuilding a
    // textStyle map for every clip in the project.
    //
    // `effectHost` supplies the stack to report for a media clip, whose effects now live on the
    // adjustment linked to it. Passing it in rather than looking it up keeps a whole-project
    // rebuild linear — resolving per clip would make it quadratic.
    // `faceSource` runs the other way: face landmarks are baked onto the media clip, but the
    // effects inspector now only ever sees the adjustment, so a linked adjustment reports the
    // clip it is pinned to. Null means "this clip's own", which is right for a media clip and for
    // an unlinked adjustment (which has no source to scan).
    QVariantMap clipToMap(const drift::Clip &clip, const drift::Clip *videoEffectHost = nullptr,
                          const drift::Clip *audioEffectHost = nullptr,
                          const drift::Clip *maskHost = nullptr,
                          const drift::Clip *faceSource = nullptr) const;

    // The media clip behind (trackIndex, clipIndex): a linked adjustment resolves to the clip it
    // is pinned to, anything else to itself. The per-clip bake jobs — face tracking today — are
    // reached through the adjustment that needs them, so they have to find their way back.
    drift::ClipRef sourceClipRef(int trackIndex, int clipIndex) const;

    // The clip whose `effects` / `audioEffects` list holds the stack for (trackIndex, clipIndex).
    // An adjustment hosts its own; a media clip's lives on the adjustment linked to it in one of
    // its track's lanes. `create` mints that lane and adjustment on demand, which is what lets
    // addEffect() keep taking a plain (trackIndex, clipIndex). Returns {-1,-1} when there is no
    // host and none was created. Creating INSERTS A TRACK, so indices captured earlier go stale.
    drift::ClipRef effectHostRef(int trackIndex, int clipIndex, drift::AdjustmentKind kind,
                                 bool create);
    const drift::Clip *effectHostClip(int trackIndex, int clipIndex,
                                      drift::AdjustmentKind kind) const;
    drift::ClipRef createLinkedAdjustment(int trackIndex, int clipIndex, drift::AdjustmentKind kind);
    int ensureAdjustmentLaneFor(int parentTrackIndex, drift::AdjustmentKind kind,
                                drift::TimeUs startUs, drift::TimeUs durationUs);

    // Rewrites (trackIndex, clipIndex) to the clip a property's keyframes actually live on:
    // transform props stay put, "fx.<i>.<param>" follows the effects to the linked adjustment.
    // A no-op when there is no such adjustment, so callers fail exactly as they did before.
    void redirectToKeyframeHost(int *trackIndex, int *clipIndex, const QString &prop) const;

    // In-place form of effectHostRef for the effect invokables, which all take a plain
    // (trackIndex, clipIndex) from QML and MCP. False when the clip has no stack of that kind
    // and none was created, in which case the caller should do nothing — the same outcome an
    // out-of-range effectIndex has always produced.
    bool redirectToEffectHost(int *trackIndex, int *clipIndex, drift::AdjustmentKind kind,
                              bool create);

    // Mirrors every pinned adjustment's span onto the clip it is linked to, and unlinks the ones
    // whose clip is gone. Runs in finishEdit so moves, trims, splits and deletes all keep links
    // true without every call site having to remember.
    void syncLinkedAdjustments(drift::Project &project) const;

    // Write a mask for (trackIndex, clipIndex), whether that names a mask adjustment (the mask is
    // its payload) or a media clip (the mask is pinned to it through a lane). MAY INSERT TRACKS.
    void writeClipMask(int trackIndex, int clipIndex, const drift::Mask &mask);
    // A ready-to-use mask for a maskCatalog() id, Freeform already seeded with the quad its rect
    // implies. An unknown id yields a mask with shape None, which contributes nothing.
    drift::Mask maskFromCatalogId(const QString &shape) const;
    // Select the clip with this id. Pinning a mask inserts a lane and normalization reorders, so
    // an index captured before the edit is stale by the time there is something to select.
    void selectClipById(const QString &clipId);
    // Re-establishes the two structural invariants the adjustment model rests on: no stack sits
    // on a media clip, and every lane is adjacent to and directly above its parent. Both passes
    // can insert or reorder tracks, so the selection is carried across by id. Idempotent and
    // cheap when nothing is out of place, which is why it can run on every edit.
    void normalizeProjectStructure();
    // Keeps open tabs pointing at sequences that exist, and the active one among them.
    void reconcileSequenceTabs();
    // A composite asset's duration follows its sequence's content.
    void syncCompositeAssetDurations();
    // Drops sequences no composite bin item refers to any more, leaving their tab if open.
    void pruneOrphanSequences();
    bool compositeAssetPlaceable(const QVariantMap &asset) const;
    void finishFlatten(const QString &clipId, const QString &sequenceId, const QString &path,
                       const QString &name, drift::TimeUs srcInUs, drift::TimeUs durationUs);

    // Keeps each lane adjacent to and directly above its parent, and demotes lanes whose parent
    // is no longer able to hold them.
    void normalizeAdjustmentLanes(drift::Project &project) const;
    void clampStoredTransitionDurations(drift::Project &project) const;

    // Selection survives a track-list reshuffle by id rather than index arithmetic: a move now
    // drags a track's lanes with it, so the destination index no longer says where things landed.
    QString trackIdAt(int trackIndex) const;
    QList<QPair<QString, int>> captureSelectionByTrackId() const;
    void restoreSelectionByTrackId(const QList<QPair<QString, int>> &captured);
    int assetIndexForClip(const drift::Clip &clip) const;
    drift::TimeUs clipDurationForAssetIndex(int assetIndex) const;
    // The absolute orientation (0/90/180/270) a video clip's frames come out at: its file's own
    // tag plus its rotationCorrection. setClipOrientationTo stores the correction that lands on
    // `degrees`.
    int clipOrientation(const drift::Clip &clip) const;
    void setClipOrientationTo(drift::Clip &clip, int degrees);
    drift::TimeUs sourceDurationForClip(const drift::Clip &clip) const;
    void startReverseRender(const QString &sourcePath, drift::TimeUs coverInUs,
                            drift::TimeUs coverOutUs);
    // Cached dense peaks for `path`, or nullptr while the off-thread decode is still running
    // (waveformReady is emitted when it lands).
    const MediaWaveform::Dense *densePeaksFor(const QString &path) const;
    struct SegmentReplaceResult
    {
        QStringList ids;
        drift::TimeUs deltaUs = 0;
    };
    // Replaces the clip at (trackIndex, clipIndex) and each linked partner with the segments
    // `build` makes from it, keeping ids, links, transitions and pinned effects attached, then
    // ripples every track in the group. No undo step: the caller snapshots and pushes one.
    bool replaceClipGroupWithSegments(int trackIndex, int clipIndex,
                                      const std::function<QList<drift::Clip>(const drift::Clip &)> &build,
                                      bool ripple, SegmentReplaceResult *out, QString *error);
    void trimClipGroupAt(int trackIndex, int clipIndex, drift::TimeUs atUs, bool dropLeft);
    void applyRippleShift(drift::Track &track, int fromClipIndex, drift::TimeUs delta);
    void restoreFilmstripsAfterLoad();
    void normalizeSelection();
    bool isValidClipIndex(int trackIndex, int clipIndex) const;
    QList<int> subtitleMergeIndices(const QList<QPair<int, int>> &pairs) const;
    void mergeSubtitleClipsAt(int trackIndex, QList<int> clipIndices);

    // Drops everything scoped to the outgoing project — clipboard, timeline-keyed caches, the
    // auxiliary-window sessions. Called by both newProject and applyProjectJson, before the
    // document is replaced, so the two paths cannot drift apart again.
    void resetSessionState();

    QByteArray serializeProjectJson() const;
    bool applyProjectJson(const QByteArray &data, QString *error);
    // Bracket every loadProject()/loadProjectJson() call, sync or async, success or
    // failure, so projectLoadPending is accurate regardless of what triggered the load.
    // beginProjectLoad() returns false (and acquires nothing) when a load already owns
    // the flag — the caller must bail out without touching m_projectLoadPending itself,
    // so a second, unrelated request can never clear the first one's pending state.
    bool beginProjectLoad();
    void finishProjectLoad(bool ok, const QString &message);
    // The actual body of loadProjectJson(), run once beginProjectLoad() has succeeded.
    // loadProject() delegates here directly for a JSON file — it already owns the
    // pending flag from its own beginProjectLoad(), so the JSON path must not try to
    // acquire it again (that would just no-op) nor release it early on failure.
    void loadProjectJsonInternal(const QUrl &url);
    // Shared by saveProject and packageProject. `embedSource` forces every source asset into the
    // bundle; otherwise each keeps whatever mode it had, tracked in m_embeddedSources. GUI thread
    // only — packageProject builds the request here and hands the finished copy to its worker.
    drift::bundle::WriteRequest buildWriteRequest(bool embedSource) const;
    // Who the document becomes when a Save As write succeeds. A fresh id keeps the copy from
    // sharing the original's extraction and derived-media directory, both of which are keyed on it.
    struct ProjectIdentity {
        QString id;
        QString name;
    };
    // Body of saveProject / saveProjectAs. `adopt` is empty for a plain Save; when set, the copy is
    // written under that identity and the open project only takes it on once the bytes are down.
    void writeProjectBundle(const QUrl &url, const std::optional<ProjectIdentity> &adopt);
    void adoptProjectIdentity(const std::optional<ProjectIdentity> &adopt);
    void rememberEmbeddedSources(const QList<drift::bundle::MediaEntry> &media);
    // Persist the save-picker folder and encode/scale choices for the next Export dialog.
    // Empty `outputPath` updates settings only and leaves lastExportFolder unchanged.
    void rememberExportChoice(const QString &outputPath, const QVariantMap &settings);
    // Repoint every path field the extraction moved. Clips duplicate their asset's path, so this
    // matches on the value rather than walking by id.
    void remapProjectPaths(const QHash<QString, QString> &remap);
    // Android: re-copy assets whose app-storage file is gone but whose originating SAF document is
    // still recorded and still granted. Cheap when nothing is missing — one stat per asset — and
    // the copies themselves run off-thread, so a project with gigabytes to restore still opens at
    // once and repoints its rows as they land. No-op on desktop.
    void rehydrateMissingSources();
    // Drops <AppData>/projects/<id> directories no project in the recents list still refers to.
    void sweepExtractionDirs();
    // Effects and transitions render as no-ops when their package is absent, which is silent and
    // looks like the project is simply wrong. Called after a load to say so instead.
    void reportMissingCatalogEntries();
    void reportMissingAddons(const QList<drift::bundle::AddonRef> &addons);
    void setDirty(bool dirty);
    void setCurrentProjectPath(const QString &path);
    void addRecentProject(const QString &path);
    // VEDIT: start-screen card for a project — a picture from its first shot and its length,
    // remembered when it is saved or opened.
    void rememberProjectCard(const QString &path);
public:
    static QString veditWhenText(const QDateTime &when, const QDateTime &now); // VEDIT
protected:
    void writeRecoveryFile();
    void deleteRecoveryFile();
    void detectRecoveryFile();
    static QString recoveryFilePath();
    QString historyHashAt(int stackIndex) const;
    int historyIndexForHash(const QString &prefix) const;
    QByteArray historyJsonAt(int stackIndex) const;
    static QString historySnapshotDir();
    static void pruneHistorySnapshots();

    AssetLibrary *m_assetLibrary = nullptr;
    AddonManager *m_addonManager = nullptr;
    MarketClient *m_marketClient = nullptr;
    JobRegistry *m_jobs = nullptr;
    CloudProviders *m_cloud = nullptr;
    // An error object when `provider` can't be used yet (no key, no consent); empty when it can.
    QJsonObject cloudUnavailable(const QString &provider) const;
    // Imports a generated file into the bin, records what made it, optionally places it.
    QJsonObject importGeneratedAudio(const QString &path, const QJsonObject &generator, const QJsonValue &place);
    // Lands a finished transcript on its asset, unless the asset is gone or its file changed.
    void storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript);
    BinFolderListModel m_binFolderModel;
    QString m_currentBinFolderId;
    bool m_importingFolder = false;
    TimelineModel m_timelineModel;
    ClipListModel m_clipListModel;
    // These trees must outlive m_playback: the compositor thread holds a bare
    // pointer into whichever one is live and may still be mid-composite at
    // teardown. During a multicam session that is m_multicamStaged, otherwise
    // m_project. Members are destroyed in reverse declaration order.
    drift::Project m_project;
    drift::Project m_multicamBase;
    drift::Project m_multicamStaged;
    PlaybackEngine m_playback;
    // Only for its audioOutputsChanged signal — the sinks resolve devices themselves.
    QMediaDevices m_mediaDevices;
    QString m_audioOutputDeviceId;
    // The audio error already on screen, so a device that fails repeatedly toasts once.
    QString m_lastAudioError;
    QUndoStack m_undoStack;
    // tracks() serialises every clip in the project into nested QVariantMaps, and used to do it
    // once per binding that read it. Cached between edits; notifyTracksChanged() drops it.
    mutable QVariantList m_tracksCache;
    mutable bool m_tracksCacheValid = false;
    // durationUs() is an uncached full scan behind the durationSeconds property, which notifies
    // on the same signal and is read just as widely (the overview strip sizes itself from it).
    mutable double m_durationSecondsCache = 0.0;
    mutable bool m_durationCacheValid = false;
    quint32 m_tracksRevision = 0;
    // One model per track index, parented to this. Grown on demand and never shrunk: a Repeater
    // holds the pointer it was handed, and deleting the model out from under it on a track
    // removal would leave it dangling. An unused slot just holds no rows.
    QList<TimelineClipsModel *> m_clipModels;
    // Live trim drag scope. The memo rejects a repeated pointer position before snapTime() and
    // the sync passes run; m_trimGestureChanged is what tells the caller whether the gesture
    // earned an undo step.
    mutable QList<drift::TimeUs> m_extraSnapTargetsCache;
    mutable bool m_extraSnapTargetsValid = false;
    // Pinned for the duration of a trim drag. Nothing can add or remove a clip mid-gesture, and
    // the only targets that do move are the dragged clip's own edges.
    drift::SnapTargets m_gestureSnapTargets;
    bool m_trimGestureActive = false;
    bool m_trimGestureChanged = false;
    drift::TimeUs m_trimGestureLastInputUs = -1;
    int m_trimGestureLastOutcome = 0;
    drift::TimeUs m_playheadUs = 0;
    bool m_playing = false;
    bool m_snapEnabled = true;
    bool m_rippleEnabled = s_defaultRippleEnabled; // VEDIT
    int m_emptyTrackCleanupHold = 0; // VEDIT: >0 inside a compound edit, or for addTrack
    bool removeEmptyTracks(); // VEDIT: immediate; true if any went
public:
    // VEDIT: media placed by the old fit (pinned to the top-left corner) moves to the middle.
    // Only untouched layouts: a clip the user moved, resized or animated keeps its framing.
    // Returns how many clips moved.
    int recentreLegacyFittedClips();
protected:
    // VEDIT: runs after the edit has returned, so a caller's track indices stay valid for the
    // rest of its own work; the removal joins the edit's undo step.
    void scheduleEmptyTrackCleanup();
    void runEmptyTrackCleanup();
    bool m_emptyTrackCleanupQueued = false;
    // VEDIT: tracks the user added with + — kept while still empty, until they get a clip.
    QSet<QString> m_userAddedTrackIds;
    bool m_allowClipOverlap = s_defaultAllowClipOverlap; // VEDIT: overlapping clips dissolve (Vegas-style)
    bool m_loopWorkAreaEnabled = false;
    bool m_darkModeOverridden = false;
    bool m_darkModePreferred = true;
    bool m_workspaceLayoutOverridden = false;
    QString m_workspaceLayoutPreferred = QStringLiteral("landscape");
    QString m_mediaViewMode = QStringLiteral("grid");
    bool m_autoKeyEnabled = false;
    bool m_timelineOverviewVisible = false;
    qreal m_trackLabelsWidth = 130;
    int m_timelineClipSize = 2; // VEDIT: large
    bool m_autoSaveEnabled = true; // VEDIT
    bool m_quietSave = false; // VEDIT: an auto-save shows no "Project saved" toast
    bool m_loudnessBusy = false; // VEDIT
    void finishClipLoudness(const drift::LoudnessResult &measured, const QString &audibleId); // VEDIT
    bool m_audioToolBusy = false; // VEDIT
    QString m_pendingBeatFitClipId; // VEDIT: music clip waiting for its beats to fit photos to
    drift::VoiceRecorder *m_voice = nullptr; // VEDIT: made on first use
    double m_voiceStartSeconds = 0.0; // VEDIT
    int m_voiceCountdown = 0; // VEDIT: seconds left before recording starts; 0 = not counting
    bool m_chromaPickActive = false; // VEDIT
    QTimer *m_voiceCountdownTimer = nullptr; // VEDIT
    void setAudioToolBusy(bool busy); // VEDIT
    QStringList m_timelineToolbarItems;
    QStringList m_timelineMenuItems;
    bool m_reopenLastProject = false;
    bool m_vaapiZeroCopy = false;
    bool m_mediaCodecZeroCopy = false;
    QString m_preferredGpu = QStringLiteral("auto");
    // One benchmark at a time: it drives the shared decoders and the GL thread, and two
    // sweeps interleaved would measure each other rather than the pipeline.
    std::atomic<bool> m_benchmarkRunning{false};
    bool m_invertTimelineScroll = false;
    QString m_uiLanguage;
    bool m_needsUiLanguagePrompt = false;
    double m_uiScale = 1.0;
    QStringList m_keyframeGraphHiddenProperties;
    bool m_subtitleEditing = false;
    int m_selectedSubtitleCue = -1;
    bool m_exportInProgress = false;
    double m_exportProgress = 0.0;
    // VEDIT: export clock and file for the remaining-time / size line.
    QElapsedTimer m_exportClock;
    QString m_exportOutputPath;
    QString m_exportStatusText;
    void updateExportStatusText(bool finished, bool ok);
    QAtomicInt m_exportCancel = 0;
    QUrl m_lastExportUrl;
    QString m_lastExportName;
    // Android: the publish-to-gallery copy behind Share is on a worker, so canShareExport reports
    // false while it runs — that both hides the button (the dialog binds its visibility to it) and
    // stops a second tap from starting the copy again. Unused on desktop.
    bool m_sharingExport = false;
    bool m_subtitleGenerating = false;
    QString m_replacingAssetId;
    // The content:// URI the in-flight replace picked, held across the probe so it can be put back
    // on the asset once applyProbedSource has overwritten the struct.
    QString m_replacingAssetSourceUri;
    bool m_editingAsset = false;
    double m_assetEditProgress = 0.0;
    QString m_assetEditStatus;
    QString m_assetEditKeepName;
    QString m_editingAssetId;
    QAtomicInt m_assetEditCancel = 0;
    bool m_assetEditIsConversion = false;
    // Asset ids waiting for convertAssetsToConstantFrameRate; the edit job runs one at a time.
    QStringList m_conversionQueue;
    double m_subtitleGenProgress = 0.0;
    QString m_subtitleGenStatus;
    QAtomicInt m_subtitleGenCancel = 0;
    bool m_segmenting = false;
    double m_segmentProgress = 0.0;
    QString m_segmentStatus;
    QAtomicInt m_segmentCancel = 0;
    // Media-bin preview session: the synthetic whole-source clip the bin row is auditioned as.
    ClipPreviewPlayer m_assetPreviewPlayer;
    int m_assetPreviewIndex = -1;
    int m_assetPreviewRevision = 0;
    bool m_assetPreviewActive = false;

    // Speed-curve session: the clip being retimed, the candidate ramp, and the player auditioning it.
    ClipPreviewPlayer m_speedCurvePlayer;
    drift::Clip m_speedCurveClip;
    drift::SpeedCurve m_speedCurve;
    int m_speedCurveTrack = -1;
    int m_speedCurveClipIndex = -1;
    int m_speedCurveRevision = 0;
    bool m_speedCurveActive = false;

    struct MulticamAngleSnap {
        int trackIndex = -1;
        QString clipId;
        drift::Clip original;
        int audioTrackIndex = -1;
        QString audioClipId;
        drift::Clip originalAudio;
        bool hasAudio = false;
    };

    // Multicam punching session. `m_project` stays the stacked originals until Save.
    bool m_multicamActive = false;
    QList<MulticamAngleSnap> m_multicamSnaps;
    drift::TimeUs m_multicamRangeStart = 0;
    drift::TimeUs m_multicamRangeEnd = 0;
    QList<drift::MulticamCut> m_multicamCuts;
    int m_multicamRevision = 0;
    // A refresh already running. Tiles are dropped rather than queued while it is set, so a
    // machine that cannot keep up falls behind in frame rate instead of in wall-clock time.
    bool m_multicamRefreshing = false;
    int m_multicamGeneration = 0; // bumped per refresh; stale decodes are dropped
    QFuture<void> m_multicamRefreshFuture;
    // Drives tile refreshes during playback only; scrubbing and paused edits refresh directly
    // off the signal that caused them.
    QTimer *m_multicamTimer = nullptr;

    bool m_fadeCurveActive = false;
    int m_fadeCurveTrack = -1;
    int m_fadeCurveClipIndex = -1;
    QString m_fadeCurveClipId;
    QString m_fadeCurveClipName;
    drift::FadeShape m_fadeShape;
    drift::FadeCurve m_fadeCurveBefore = drift::FadeCurve::Smooth;
    drift::FadeShape m_fadeShapeBefore;
    // Which shape the live session is editing; committed as-is by applyFadeCurve.
    drift::FadeCurve m_fadeCurveMode = drift::FadeCurve::Custom;
    bool m_transitionCurveActive = false;
    int m_transitionCurveTrack = -1;
    QString m_transitionCurveId;
    QString m_transitionCurveName;
    drift::FadeShape m_transitionShape;
    drift::FadeCurve m_transitionCurveBefore = drift::FadeCurve::Linear;
    drift::FadeShape m_transitionShapeBefore;
    drift::FadeCurve m_transitionCurveMode = drift::FadeCurve::Custom;
    bool m_transitionCurveApplied = false;
    bool m_fadeCurveApplied = false;

    bool m_reverseRendering = false;
    double m_reverseProgress = 0.0;
    QString m_reverseStatus;
    QAtomicInt m_reverseCancel = 0;

    bool m_denoising = false;
    double m_denoiseProgress = 0.0;
    QString m_denoiseStatus;
    QAtomicInt m_denoiseCancel = 0;
    // The A/B snippets currently on offer. Dragging the preview window along a clip re-renders
    // repeatedly, so each pair is deleted as the next supersedes it.
    QString m_denoisePreviewClean;
    QString m_denoisePreviewOriginal;
    // Source paths that were embedded when this project was last read or written, so a plain Save
    // keeps a packaged project packaged instead of quietly making it depend on the cache dir.
    QSet<QString> m_embeddedSources;
    // Handed to applyProjectJson by loadProject, applied alongside the other load-time path
    // migrations and cleared there.
    QHash<QString, QString> m_pendingPathRemap;
    // Playhead per timeline, so switching tabs returns to where each was left. "" = main.
    QHash<QString, drift::TimeUs> m_sequencePlayheads;
    bool m_packaging = false;
    double m_packageProgress = 0.0;
    QAtomicInt m_packageCancel = 0;
    bool m_faceDetecting = false;
    double m_faceDetectProgress = 0.0;
    QString m_faceDetectStatus;
    QAtomicInt m_faceDetectCancel = 0;
    // Photos with an ingest in flight, so a slider nudge or a second clip using the same photo
    // does not queue the landmarker twice.
    QSet<QString> m_faceSwapIngesting;
    // Scene detection. Only one clip's analysis is live at a time — the panel shows the
    // selected clip — so this needs no cache of its own beyond the on-disk one.
    QVariantList m_scenes;
    QString m_sceneClipId;
    QString m_sceneClipPath;
    int m_sceneClipRotationCorrection = 0;
    bool m_sceneDetecting = false;
    double m_sceneDetectProgress = 0.0;
    QString m_sceneDetectStatus;
    QAtomicInt m_sceneDetectCancel = 0;
    QMap<QString, QProcess*> m_stabilizeProcesses;
    QMap<QString, double> m_stabilizeProgress;
    QMap<QString, QString> m_stabilizeStatus;
    QMap<QString, qint64> m_stabilizeLastProgressEmit;
    QSet<QString> m_stabilizeCancelRequested;
    quint64 m_sceneGeneration = 0;
    bool m_segSessionActive = false;
    bool m_segForTemplate = false;
    bool m_segEncoding = false;
    int m_segTrack = -1;
    int m_segClip = -1;
    double m_segSeconds = 0.0;
    int m_segRevision = 0;
    int m_segGeneration = 0; // bumped per encode request; stale results are dropped
    int m_segSeedGeneration = 0; // bumped per seed preview; stale masks are dropped
    bool m_segSeedRunning = false;
    int m_loadGeneration = 0; // bumped per loadProject; stale extracts are dropped
    bool m_projectLoadPending = false;
    QImage m_segFrame;
    drift::Sam2Embedding m_segEmbedding;
    // "sam2" or "rvm". Persists across sessions so the window reopens on the last choice.
    QString m_segBackend = QStringLiteral("sam2");
    QString m_segQuality; // RVM variant; empty means the best installed
    QVariantList m_segPoints;
    int m_selectedTrack = -1;
    int m_selectedClip = -1;
    int m_selectedTransitionTrack = -1;
    int m_selectedTransitionLeftClip = -1;
    QList<QPair<int, int>> m_selection;
    int m_selectionRevision = 0;
    int m_timelineTrimCursorSide = 0;
    int m_timelineTrimCursorHeight = 0;
    int m_timelineTrimCursorOwner = 0;
    int m_trimCursorTokenSeq = 0;
    // The pointer is drawn with QPainter at the track's height. Heights come from a handful of
    // row sizes, so this stays tiny — and a member rather than a function-local static because a
    // QCursor holds a platform cursor that must not outlive QGuiApplication.
    mutable QHash<int, QCursor> m_trimCursorCache;
    QCursor trimCursorFor(int side, int heightPx) const;
    bool m_guidesEnabled = false;
    bool m_canvasCropMode = false;
    bool m_maskEditMode = false;
    QString m_guideType = QStringLiteral("thirds");
    QHash<QString, QString> m_shortcuts;
    QHash<QString, QSet<QString>> m_assetFavorites;
    int m_draggingAssetIndex = -1;
    QStringList m_draggingAssetGroup; // VEDIT
    QString m_groupDropTrackId; // VEDIT: a group drop that would overlap goes to this new track
    QVariantMap m_selectedVolumeKey; // VEDIT
    int m_selectedTrackRow = -1; // VEDIT
    void addClipsFromAssetsFrom(const QStringList &assetIds, drift::TimeUs startUs); // VEDIT
    bool m_assetPreviewWindowOpen = false;
    QString m_lastMessage;
    QString m_lastMessageSeverity = QStringLiteral("info");
    bool m_inlineTextEditing = false;
    bool m_previewDragActive = false;
    drift::Clip *textClipAt(int trackIndex, int clipIndex);
    // VEDIT: 소리로 맞추기
    bool m_audioSyncBusy = false;
    void compactMainTrack(); // VEDIT: closes the gaps on every track with 빈틈 닫기 on
    QTimer *m_spanPreviewTimer = nullptr; // VEDIT: previewSpan
    QTimer *m_renderCacheTimer = nullptr; // VEDIT: 임시 렌더링 re-key after edits settle
public:
    QVariantList renderCacheSegments() const; // VEDIT
private:
    drift::TimeUs m_spanPreviewEnd = 0;
    drift::TimeUs m_spanPreviewReturn = 0;
    drift::TimeUs m_spanPreviewLast = 0;
    bool m_spanPreviewLoopWas = false;
    void applyDefaultTrackMagnet(); // VEDIT: the main video track gets 빈틈 닫기 until the user chooses
    bool m_trackMagnetChosen = false; // VEDIT: the user has switched 빈틈 닫기 on a track
    double m_audioSyncProgress = 0.0;
    QAtomicInt m_audioSyncCancel{0};
    int applyAudioSync(const QList<AudioSyncJob> &jobs, const QList<double> &lags);
    // A text, subtitle or shape clip: anything that carries a shading stack.
    drift::Clip *styledClipAt(int trackIndex, int clipIndex, bool textOnly = false);
    static void applyTextStylePatch(drift::TextStyle &style, const QVariantMap &patch);
    static QVariantMap textAnimationPresetToMap(const drift::TextAnimationPreset &preset);
    drift::Project m_previewDragBefore;
    QString m_previewDragText;
    // Something was written since beginPreviewDrag; a press with no movement commits nothing.
    bool m_previewDragDirty = false;
    // Begun by a setter rather than a press (keyboard nudges); commits once the nudges stop.
    bool m_previewDragAuto = false;
    QTimer *m_previewAutoCommit = nullptr;
    void emitPreviewFrame();
    void emitPreviewEdit(int trackIndex, int clipIndex, const QStringList &keys);
    void beginImplicitPreviewDrag(const QString &undoText);
    // Runs spec off-thread and swaps its output in for the asset at assetIndex, reporting through
    // assetEditChanged/assetEditFinished. The caller has checked no edit is running.
    void startAssetEditJob(const QString &assetId, const QString &name,
                           const drift::MediaEditSpec &spec, bool conversion);
    void startNextConversion();
    void syncTextOverlaySkip();
    struct ClipboardItem
    {
        drift::Clip clip;
        drift::TrackType trackType = drift::TrackType::Video;
        QList<drift::Transition> transitions;
        // Carried separately because a mask lives on the adjustments pinned to the clip, not on
        // the clip: copying the Clip alone would silently drop it.
        QList<drift::Mask> masks;
    };
    QList<ClipboardItem> m_clipboard;
    QList<ClipboardItem> m_attrClipboard; // VEDIT: 특성 복사
    QString m_lastClickedTrackId;          // VEDIT
    ClipboardItem attributeItemFor(int trackIndex, int clipIndex); // VEDIT

    // Whole-file peaks, for the dialogs that show a complete clip at once (Denoise, Speed
    // Curve). The timeline uses m_waveformBlocks instead — see waveformPeaksRange.
    mutable QHash<QString, MediaWaveform::Dense> m_waveformCache;
    mutable QSet<QString> m_waveformPending;

    mutable WaveformBlockCache m_waveformBlocks;

    mutable FilmstripTileCache m_filmstripTiles;

    // Subtitle-lane voice waveform: the mixed audio underneath a subtitle clip's
    // span, voice band-passed. Keyed by "<startUs>:<durUs>"; invalidated on edits.
    mutable QHash<QString, QVariantList> m_subtitleWaveformCache;
    mutable QSet<QString> m_subtitleWaveformPending;

    // Beat detection. Only one range is live at a time, so this needs no cache — just a
    // generation counter so a job whose range the user has since left is dropped on arrival.
    AudioBeatAnalysis m_beatAnalysisRaw; // kept so snap targets can be rebuilt per layer
    QVariantMap m_beatAnalysis;          // the same result, shaped for QML
    bool m_beatAnalysisRunning = false;
    quint64 m_beatAnalysisGeneration = 0;
    QByteArray m_beatAudioFingerprint;
    bool m_beatGridVisible = false;
    bool m_onsetsVisible = false;
    struct PendingEffectTemplate
    {
        int trackIndex = -1;
        int clipIndex = -1;
        QString templateId;
        bool valid() const { return trackIndex >= 0 && clipIndex >= 0 && !templateId.isEmpty(); }
    };
    std::optional<PendingEffectTemplate> m_pendingEffectTemplate;
    // Beats and onsets as snap targets, thinned so a dense onset list cannot make every
    // position on the timeline snap to something. Only the visible layers contribute.
    QList<drift::TimeUs> m_beatSnapTargets;

    // Save state / autosave / crash recovery.
    QString m_currentProjectPath;
    bool m_dirty = false;
    QTimer *m_autosaveTimer = nullptr;
    bool m_recoveryAvailable = false;
    QVariantMap m_recoveryInfo;
    QUrl m_pendingStartupProject;
    QUrl m_lastExternalProject;
    bool m_uiReady = false;
    // Launch layout picker / first-clip setup completed for this empty project.
    bool m_projectLayoutChosen = false;

    std::unique_ptr<drift::mcp::McpServer> m_mcp;
    bool m_mcpStartOnLaunch = false;
    bool m_mcpUndoSuspended = false;
    int m_mcpBatchDepth = 0;
    drift::Project m_mcpBatchBefore;
    int m_mcpEditRevision = 0;
    mutable QHash<QString, QPair<int, int>> m_mcpClipIndex;
    mutable int m_mcpClipIndexRevision = -1;
    void rebuildMcpClipIndexIfNeeded() const;

    void setProjectLayoutChosen(bool chosen);

    static constexpr int kMaxUndoSteps = 50;
    static constexpr int kAutosaveIntervalMs = 15000;
    static constexpr int kMaxRecentProjects = 10;
};
