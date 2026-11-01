#include "CompositorService.h"

#include "PlaybackStats.h"
#include "engine/ClipReaderPool.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <cmath>

namespace {
constexpr double kAdaptiveScaleMin = 0.25;
constexpr double kAdaptiveScaleStepDown = 0.75;
constexpr double kAdaptiveScaleStepUp = 1.25;
constexpr int kLateBeforeScaleDown = 2;
// VEDIT: a second on time, not 6 frames — see noteFrameWork. A step back up that fails doubles this.
constexpr int kOnTimeBeforeScaleUp = 30;
constexpr int kMaxOnTimeBeforeScaleUp = 900;
// A step up undone within this many frames was a failed one.
constexpr int kStepUpTrialFrames = 90;
// A step up only if the frame it would make is predicted to leave this much of the budget spare.
constexpr double kStepUpBudgetShare = 0.85;
// Composites at the start of a run open and seek the decoders and are reliably
// slower than the ones after them. Charging those to the adaptive scale used to
// downscale the first seconds of every playback.
constexpr int kWarmupFrames = 3;
// Floor for the caller's budget: at 60 fps two ticks is 33 ms, close enough to
// normal jitter that the scale would rattle up and down against it.
constexpr int kMinLateFrameBudgetMs = 40;
}

CompositorWorker::CompositorWorker(QObject *parent)
    : QObject(parent)
{
}

void CompositorWorker::composite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options,
                                 std::shared_ptr<const drift::Project> snapshot, quint64 sequence)
{
    // Keep the shared tree alive for the whole frame; setProject only borrows.
    m_snapshot = std::move(snapshot);
    if (!m_snapshot) {
        // Still report completion: the service treats a request as in flight
        // until the worker answers, and the quality-mode play loop waits for it.
        emit frameReady(GpuFrameTexture{}, timeUs, CompositeTiming{}, sequence);
        return;
    }
    m_compositor.setProject(m_snapshot.get());

    // Zero the per-thread decode accumulator so what we read back below belongs to this
    // frame only. Safe against a second worker: the accumulator is thread_local.
    ClipReaderPool::resetDecodeWaitNs();
    QElapsedTimer elapsed;
    elapsed.start();

    GpuFrameTexture frame = m_compositor.compositeToTextureAt(timeUs, options);

    CompositeTiming timing;
    timing.compositeMs = double(elapsed.nsecsElapsed()) / 1'000'000.0;
    timing.decodeWaitMs = double(ClipReaderPool::decodeWaitNs()) / 1'000'000.0;
    emit frameReady(frame, timeUs, timing, sequence);
}

CompositorService::CompositorService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<drift::TimeUs>("drift::TimeUs");
    qRegisterMetaType<std::shared_ptr<const drift::Project>>("std::shared_ptr<const drift::Project>");
    qRegisterMetaType<FrameCompositor::RenderOptions>("FrameCompositor::RenderOptions");
    qRegisterMetaType<GpuFrameTexture>("GpuFrameTexture");
    qRegisterMetaType<CompositeTiming>("CompositeTiming");
    for (int i = 0; i < GpuCompositor::kMaxPreviewComposites; ++i) {
        m_workers[i] = new CompositorWorker;
        m_workers[i]->moveToThread(&m_threads[i]);
        connect(m_workers[i], &CompositorWorker::frameReady, this,
                &CompositorService::onWorkerFrameReady, Qt::QueuedConnection);
        m_threads[i].start();
    }
}

CompositorService::~CompositorService()
{
    for (int i = 0; i < GpuCompositor::kMaxPreviewComposites; ++i) {
        m_threads[i].quit();
        m_threads[i].wait();
        delete m_workers[i];
        m_workers[i] = nullptr;
    }
}

void CompositorService::setProject(const drift::Project *project)
{
    m_project = project;
    invalidateSnapshot();
}

void CompositorService::invalidateSnapshot()
{
    ++m_liveGeneration;
    m_sharedSnapshot.reset();
    // Forget what was last drawn. An edit changes the picture without moving the playhead, so
    // dispatchPending would otherwise recognise the incoming request as one it has already
    // served and decline to redraw — leaving the edit invisible until the playhead moved.
    m_lastDispatchedTimeUs = -1;
}

void CompositorService::setDropLateFrames(bool drop)
{
    if (m_dropLateFrames == drop)
        return;
    m_dropLateFrames = drop;
    // Quality mode renders every frame at the requested scale; leaving a
    // downscale from a previous fast-mode run would defeat the point.
    resetAdaptiveState();
}

void CompositorService::setAdaptiveQuality(bool enabled)
{
    if (m_adaptiveQuality == enabled)
        return;
    m_adaptiveQuality = enabled;
    // Switching away hands the caller its requested scale back immediately;
    // switching on starts from full rather than from a stale measurement.
    resetAdaptiveState();
}

void CompositorService::setPlaybackActive(bool active)
{
    if (m_playbackActive == active)
        return;
    m_playbackActive = active;
    // The scale itself survives across runs — a machine that could not keep up a
    // moment ago still cannot, and relearning that on every play would drop the
    // preview a second or two into each one. Only the streaks restart.
    m_lateStreak = 0;
    m_onTimeStreak = 0;
    m_warmupFramesLeft = active ? kWarmupFrames : 0;
}

void CompositorService::setLateFrameBudgetMs(int ms)
{
    m_lateFrameBudgetMs = qMax(kMinLateFrameBudgetMs, ms);
}

double CompositorService::adaptiveScaleFactor() const
{
    return m_adaptiveScale;
}

void CompositorService::resetAdaptiveState()
{
    m_adaptiveScale = 1.0;
    m_lateStreak = 0;
    m_onTimeStreak = 0;
    m_warmupFramesLeft = 0;
    m_workAverageMs = 0.0;
    m_upHoldFrames = kOnTimeBeforeScaleUp;
    m_framesSinceStepUp = kStepUpTrialFrames;
}

FrameCompositor::RenderOptions CompositorService::effectiveOptions(
    FrameCompositor::RenderOptions options) const
{
    if (!m_adaptiveQuality)
        return options;
    options.previewScale = qBound(kMinPreviewScale, options.previewScale * m_adaptiveScale, 1.0);
    return options;
}

// VEDIT (조박사님, 내장 GPU: 클립 경계에서 멈춤): a change of scale is a new decode size, which throws
// away every reader's decoded frames — the read-ahead and the next clip's warmed opening with them.
// A machine that could only just hold a scale used to climb back to it after 6 good frames and fall
// off it again a moment later: three changes a second, five times the seeks and a quarter-second
// stall at the cuts. So climbing now has to look affordable first (pixels are the cost, so the work
// goes with the square of the scale), and one that fails anyway waits twice as long next time.
void CompositorService::noteFrameWork(double workMs)
{
    if (m_warmupFramesLeft > 0) {
        --m_warmupFramesLeft;
        return;
    }

    m_workAverageMs = m_workAverageMs > 0.0 ? 0.9 * m_workAverageMs + 0.1 * workMs : workMs;
    const auto stepTo = [this](double scale) {
        const double ratio = scale / m_adaptiveScale;
        m_workAverageMs *= ratio * ratio;
        m_adaptiveScale = scale;
        m_lateStreak = 0;
        m_onTimeStreak = 0;
    };

    if (workMs > double(m_lateFrameBudgetMs)) {
        m_onTimeStreak = 0;
        ++m_lateStreak;
        if (m_lateStreak >= kLateBeforeScaleDown && m_adaptiveScale > kAdaptiveScaleMin + 1e-6) {
            if (m_framesSinceStepUp < kStepUpTrialFrames)
                m_upHoldFrames = qMin(kMaxOnTimeBeforeScaleUp, m_upHoldFrames * 2);
            stepTo(qMax(kAdaptiveScaleMin, m_adaptiveScale * kAdaptiveScaleStepDown));
        }
        return;
    }

    m_lateStreak = 0;
    ++m_onTimeStreak;
    ++m_framesSinceStepUp;
    if (m_onTimeStreak >= m_upHoldFrames && m_adaptiveScale < 1.0 - 1e-6) {
        const double up = qMin(1.0, m_adaptiveScale * kAdaptiveScaleStepUp);
        const double ratio = up / m_adaptiveScale;
        if (m_workAverageMs * ratio * ratio > m_lateFrameBudgetMs * kStepUpBudgetShare) {
            m_onTimeStreak = 0;
            return;
        }
        stepTo(up);
        m_framesSinceStepUp = 0;
    }
}

int CompositorService::maxInFlight() const
{
    // Pipelining exists to keep realtime playback fed. Scrubbing and seeking issue one
    // request at a time by nature, and running two decoders against a moving playhead would
    // only make them fight over the same cursor, so outside playback the depth stays at one.
    // Old Intel iGPUs cannot keep two composites off the texture on screen either.
    if (!m_playbackActive || GpuCompositor::previewGpuIsLimited())
        return 1;
    return GpuCompositor::kMaxPreviewComposites;
}

void CompositorService::dispatch(drift::TimeUs timeUs, const FrameCompositor::RenderOptions &options)
{
    if (!m_project)
        return;

    if (!m_sharedSnapshot || m_snapshotGeneration != m_liveGeneration) {
        // One uniquely-owned snapshot per generation; subsequent ticks only bump
        // the shared_ptr. Plain Project copy would keep sharing QMap/QList with
        // the live tree — unsafe once the GUI mutates while the worker reads.
        m_sharedSnapshot = std::make_shared<drift::Project>(m_project->detachedCopy());
        m_snapshotGeneration = m_liveGeneration;
    }

    ++m_inFlight;
    if (m_stats)
        m_stats->noteInFlight(m_inFlight);

    // Round-robin rather than "first idle": the workers are interchangeable, and alternating
    // keeps a long composite on one of them from starving the other's warm decoder caches.
    CompositorWorker *worker = m_workers[m_nextWorker];
    m_nextWorker = (m_nextWorker + 1) % GpuCompositor::kMaxPreviewComposites;

    QMetaObject::invokeMethod(worker, "composite", Qt::QueuedConnection,
                              Q_ARG(drift::TimeUs, timeUs),
                              Q_ARG(FrameCompositor::RenderOptions, options),
                              Q_ARG(std::shared_ptr<const drift::Project>, m_sharedSnapshot),
                              Q_ARG(quint64, m_nextSequence++));
}

bool CompositorService::dispatchPending()
{
    if (m_inFlight >= maxInFlight())
        return false;

    const drift::TimeUs latest = m_pendingTimeUs.load(std::memory_order_acquire);
    FrameCompositor::RenderOptions latestOptions;
    latestOptions.previewScale =
        static_cast<double>(m_pendingPreviewScalePercent.load(std::memory_order_acquire)) / 100.0;
    latestOptions.maxTimeEchoHistoryFrames =
        m_pendingMaxTimeEchoHistoryFrames.load(std::memory_order_acquire);
    latestOptions.readAheadUs = m_pendingReadAheadUs.load(std::memory_order_acquire);

    // Nothing new to draw. Re-rendering the frame already requested would burn a worker on a
    // picture identical to the one on screen.
    if (latest == m_lastDispatchedTimeUs
        && latestOptions.previewScale == m_lastDispatchedOptions.previewScale
        && latestOptions.maxTimeEchoHistoryFrames == m_lastDispatchedOptions.maxTimeEchoHistoryFrames
        && latestOptions.readAheadUs == m_lastDispatchedOptions.readAheadUs) {
        return false;
    }

    m_lastDispatchedTimeUs = latest;
    m_lastDispatchedOptions = latestOptions;
    FrameCompositor::RenderOptions options = effectiveOptions(latestOptions);
    options.cacheVideoSources = !m_playbackActive;
    dispatch(latest, options);
    return true;
}

void CompositorService::requestComposite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options)
{
    options.previewScale = qBound(kMinPreviewScale, options.previewScale, 1.0);

    // The pending scale is published as whole percent, and the catch-up dispatch in
    // onWorkerFrameReady reads it back as percent/100. Dispatching the unrounded value here
    // would leave the two permanently unequal (0.16667 vs 0.17), so every finished frame would
    // re-dispatch at a slightly different canvas size — the preview would flip between, say,
    // 120x213 and 122x218 on every frame, rebuilding the presentation ring's FBOs each time and
    // handing the scene graph freshly allocated (black) textures. Round-trip through the same
    // integer here so both paths ask for one size. What is stored and compared is the requested
    // scale; the adaptive multiplier is a discrete ratchet applied at dispatch, so both paths
    // still derive the same size from it until the ratchet deliberately moves.
    const int previewScalePercent =
        qBound(kMinPreviewScalePercent, static_cast<int>(std::lround(options.previewScale * 100.0)), 100);
    options.previewScale = previewScalePercent / 100.0;

    m_pendingTimeUs.store(timeUs, std::memory_order_release);
    m_pendingPreviewScalePercent.store(previewScalePercent, std::memory_order_release);
    m_pendingMaxTimeEchoHistoryFrames.store(options.maxTimeEchoHistoryFrames, std::memory_order_release);
    m_pendingReadAheadUs.store(options.readAheadUs, std::memory_order_release);

    // Folded into work already running. Counted because a high rate here is the signal that
    // the pipeline cannot keep up with the cadence being asked of it.
    if (!dispatchPending() && m_stats)
        m_stats->noteCoalesced();
}

void CompositorService::onWorkerFrameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs,
                                           CompositeTiming timing, quint64 sequence)
{
    Q_UNUSED(timeUs);
    m_inFlight = qMax(0, m_inFlight - 1);
    if (m_stats)
        m_stats->noteComposite(timing.compositeMs, timing.decodeWaitMs);

    // Whether the frame is worth showing and whether it was rendered fast enough are
    // different questions. Adaptation asks the second one, and asks it of the compositing
    // work itself rather than of dispatch-to-delivery: with more than one composite in
    // flight, time spent queued behind another frame is normal and says nothing about
    // whether this machine can keep up.
    if (m_adaptiveQuality && m_dropLateFrames && m_playbackActive)
        // VEDIT: less the wait for the decoder, which a smaller picture does not shorten — the first
        // frame after a cut is the usual one, and it says nothing about what this machine can draw.
        noteFrameWork(qMax(0.0, timing.compositeMs - timing.decodeWaitMs));

    // Present unless something newer already got there. That covers both ways a finished
    // frame can be worthless — it completed out of order behind a later one, or the playhead
    // moved past it and a newer request beat it to the screen — without the old behaviour of
    // discarding a frame purely for being late when nothing existed to replace it. Paying for
    // a composite and then showing nothing is a hitch; showing it slightly late is not.
    const bool overtaken = sequence <= m_lastPresentedSequence;
    if (!overtaken && frame.isValid()) {
        m_lastPresentedSequence = sequence;
        if (m_stats)
            m_stats->noteDelivered();
        emit frameReady(frame);
    } else if (overtaken && frame.isValid() && m_stats) {
        m_stats->noteDropped();
    }

    if (m_stats)
        m_stats->setAdaptiveScale(m_adaptiveScale);

    // Keep the pipe full: a slot just freed up, so start whatever is newest and pending.
    dispatchPending();
}
