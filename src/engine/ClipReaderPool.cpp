#include "ClipReaderPool.h"

#include "ClipReaderWorker.h"

#include <QMetaObject>
#include <QMetaType>

#include <iterator>
#include <thread> // VEDIT

ClipReaderPool &ClipReaderPool::instance()
{
    static ClipReaderPool pool;
    static bool registered = false;
    if (!registered) {
        qRegisterMetaType<drift::TimeUs>("drift::TimeUs");
        qRegisterMetaType<PreviewVideoFrame>("PreviewVideoFrame");
        registered = true;
    }
    return pool;
}

ClipReaderPool::~ClipReaderPool()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_videoWorkers)
        stopWorkerEntry(*entry.second);
    for (auto &entry : m_audioWorkers)
        stopWorkerEntry(*entry.second);
    m_videoWorkers.clear();
    m_audioWorkers.clear();
}

void ClipReaderPool::stopWorkerEntry(WorkerEntry &entry)
{
    if (!entry.thread)
        return;

    if (entry.worker) {
        QMetaObject::invokeMethod(entry.worker, "closePath", Qt::BlockingQueuedConnection);
    }

    entry.thread->quit();
    entry.thread->wait();
    delete entry.worker;
    entry.worker = nullptr;
    entry.thread.reset();
}

ClipReaderPool::WorkerEntry &ClipReaderPool::ensureWorker(
    std::map<QString, std::unique_ptr<WorkerEntry>> &workers, const QString &path, quint64 streamId)
{
    // VEDIT (조박사님: "자르고 나면 버벅인다"): one thread per clip, not per file. A cut leaves two
    // clips on one file; on one thread the next piece's warm-up — a seek back to its keyframe and
    // a run of 4K decodes — held the piece on screen for a second or more, frame after frame.
    const QString key = streamId != 0 ? path + QStringLiteral("#") + QString::number(streamId, 16) : path;
    auto it = workers.find(key);
    // VEDIT (조박사님: "클립이 넘어갈 때 앞 클립 잔상"): a clip with no worker of its own takes over
    // one of its file's that nobody has read for a second (a piece already played past) instead of
    // opening another decoder — that open is what held the previous clip's frame on screen.
    if (it == workers.end() && streamId != 0 && &workers == &m_videoWorkers) {
        const QString prefix = path + QStringLiteral("#");
        auto spare = workers.end();
        for (auto w = workers.begin(); w != workers.end(); ++w) {
            if (!w->first.startsWith(prefix) || w->second->inFlight > 0
                || w->second->lastUse.elapsed() < kAdoptIdleMs || m_protectedVideoKeys.contains(w->first))
                continue;
            if (spare == workers.end() || w->second->lastUse.elapsed() > spare->second->lastUse.elapsed())
                spare = w;
        }
        if (spare != workers.end()) {
            std::unique_ptr<WorkerEntry> entry = std::move(spare->second);
            workers.erase(spare);
            QMetaObject::invokeMethod(entry->worker, "adoptStream", Qt::QueuedConnection,
                                      Q_ARG(quint64, streamId));
            it = workers.emplace(key, std::move(entry)).first;
        }
    }
    if (it == workers.end()) {
        auto entry = std::make_unique<WorkerEntry>();
        entry->thread = std::make_unique<QThread>();
        entry->worker = new ClipReaderWorker;
        entry->worker->moveToThread(entry->thread.get());
        entry->thread->start();

        // Open asynchronously. Callers that need frames/audio use BlockingQueued
        // decode methods, which run after this open on the worker's event queue —
        // so the GUI/audio threads are never stuck inside avformat_find_stream_info
        // while holding the pool mutex (multi-hour files make that open very slow).
        QMetaObject::invokeMethod(entry->worker, "openPath", Qt::QueuedConnection, Q_ARG(QString, path));
        it = workers.emplace(key, std::move(entry)).first;
    }

    it->second->lastUse.start();
    return *it->second;
}

// Only unlinks the evictable workers; the caller tears them down after dropping the lock.
// stopWorkerEntry blocks twice — a BlockingQueuedConnection into the worker and then a thread join —
// and running that under m_mutex would stall every other reader for its duration, including the
// audio thread inside readAudioInterleaved. A worker with inFlight > 0 is being read right now and
// is never taken: that is what makes the raw WorkerEntry* those readers hold across the unlocked
// decode safe.
std::vector<std::unique_ptr<ClipReaderPool::WorkerEntry>> ClipReaderPool::detachIdleLocked(
    std::map<QString, std::unique_ptr<WorkerEntry>> &workers, const QSet<QString> &keep,
    qint64 minIdleMs)
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    for (auto it = workers.begin(); it != workers.end();) {
        WorkerEntry &entry = *it->second;
        const int hash = it->first.lastIndexOf(QLatin1Char('#')); // VEDIT: per-clip keys
        const QString path = hash >= 0 ? it->first.left(hash) : it->first;
        if (keep.contains(path) || entry.inFlight > 0 || entry.lastUse.elapsed() < minIdleMs) {
            ++it;
            continue;
        }
        evicted.push_back(std::move(it->second));
        it = workers.erase(it);
    }
    return evicted;
}

void ClipReaderPool::releaseAll()
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        evicted = detachIdleLocked(m_videoWorkers, {}, 0);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(m_audioWorkers, {}, 0);
        evicted.insert(evicted.end(), std::make_move_iterator(audio.begin()),
                       std::make_move_iterator(audio.end()));
    }
    for (const std::unique_ptr<WorkerEntry> &entry : evicted)
        stopWorkerEntry(*entry);
}

void ClipReaderPool::setReadAheadUs(drift::TimeUs readAheadUs)
{
    m_readAheadUs.store(qMax<drift::TimeUs>(0, readAheadUs), std::memory_order_relaxed);
}

void ClipReaderPool::resetVideoDecoders()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_videoWorkers) {
        QMetaObject::invokeMethod(entry.second->worker, "resetVideoDecoders",
                                  Qt::BlockingQueuedConnection);
    }
}

void ClipReaderPool::setHardwareDecodeMode(ClipReader::HardwareDecodeMode mode,
                                           drift::hwaccel::Backend backend)
{
    ClipReader::setHardwareDecodeMode(mode, backend);
    resetVideoDecoders();
}

void ClipReaderPool::warmVideoFrames(const QList<VideoRequest> &requests)
{
    for (const VideoRequest &request : requests) {
        if (request.path.isEmpty())
            continue;

        // The post happens under the pool mutex: it does not block, and holding the lock is what
        // stops the idle release from deleting the worker between resolving it and posting to it.
        QMutexLocker lock(&m_mutex);
        // Prefer the preview decode path so warm hits the same cache as composite.
        QMetaObject::invokeMethod(ensureWorker(m_videoWorkers, request.path, request.streamId).worker, "decodePreviewVideo",
                                  Qt::QueuedConnection,
                                  Q_ARG(quint64, request.streamId), Q_ARG(drift::TimeUs, request.sourceUs),
                                  Q_ARG(int, request.maxWidth), Q_ARG(int, request.maxHeight),
                                  Q_ARG(QString, QString()), Q_ARG(int, 15), Q_ARG(bool, false),
                                  Q_ARG(int, request.rotationCorrection));
    }
}

void ClipReaderPool::touchVideoStreams(const QList<VideoRequest> &requests)
{
    QMutexLocker lock(&m_mutex);
    for (const VideoRequest &request : requests) {
        const QString key = request.streamId != 0
            ? request.path + QStringLiteral("#") + QString::number(request.streamId, 16)
            : request.path;
        if (auto it = m_videoWorkers.find(key); it != m_videoWorkers.end())
            it->second->lastUse.start();
    }
}

void ClipReaderPool::protectVideoStreams(const QList<VideoRequest> &requests)
{
    QMutexLocker lock(&m_mutex);
    m_protectedVideoKeys.clear();
    for (const VideoRequest &request : requests) {
        const QString key = videoKey(request.path, request.streamId);
        m_protectedVideoKeys.insert(key);
        if (auto it = m_videoWorkers.find(key); it != m_videoWorkers.end())
            it->second->lastUse.start();
    }
}

void ClipReaderPool::warmVideoFrames(const QList<VideoRequest> &requests, drift::TimeUs readAheadUs)
{
    warmVideoFrames(requests);
    for (const VideoRequest &request : requests) {
        if (request.path.isEmpty())
            continue;
        QMutexLocker lock(&m_mutex);
        ensureWorker(m_videoWorkers, request.path, request.streamId).worker->requestPrefetchPreview(
            request.streamId, request.maxWidth, request.maxHeight, readAheadUs);
    }
}

namespace {
// Per-thread so two compositor workers building different frames each measure only their own
// blocking reads. Nanoseconds: a single 4K hardware readback can be well under a millisecond,
// and rounding those to 0 would hide exactly the cost this is here to find.
thread_local qint64 t_decodeWaitNs = 0;
} // namespace

void ClipReaderPool::resetDecodeWaitNs()
{
    t_decodeWaitNs = 0;
}

qint64 ClipReaderPool::decodeWaitNs()
{
    return t_decodeWaitNs;
}

QImage ClipReaderPool::readVideoFrame(const QString &path, quint64 streamId, drift::TimeUs sourceUs,
                                      int maxWidth, int maxHeight, const QString &stabilizePath,
                                      int stabilizeSmoothing, bool stabilizeTripod, int rotationCorrection)
{
    if (path.isEmpty())
        return {};

    WorkerEntry *entry = nullptr;
    {
        // Hold the pool mutex only to resolve the worker; releasing it before the
        // blocking decode lets audio and video (different workers) decode in
        // parallel instead of serializing on this lock. inFlight keeps the idle
        // release from destroying the entry while we hold its raw worker pointer.
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_videoWorkers, path, streamId);
        ++entry->inFlight;
    }
    ClipReaderWorker *worker = entry->worker;

    QImage frame;
    QElapsedTimer decodeWait;
    decodeWait.start();
    QMetaObject::invokeMethod(worker, "decodeVideo", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QImage, frame),
                               Q_ARG(quint64, streamId), Q_ARG(drift::TimeUs, sourceUs),
                               Q_ARG(int, maxWidth), Q_ARG(int, maxHeight),
                               Q_ARG(QString, stabilizePath), Q_ARG(int, stabilizeSmoothing), Q_ARG(bool, stabilizeTripod),
                               Q_ARG(int, rotationCorrection));
    t_decodeWaitNs += decodeWait.nsecsElapsed();

    // Decode one frame beyond the current position while the caller composites
    // this one. The reader knows the source frame duration; the old code guessed
    // a hardcoded 1/30 s, which missed on every clip that isn't 30 fps.
    QMetaObject::invokeMethod(worker, "prefetchNextVideo", Qt::QueuedConnection,
                              Q_ARG(quint64, streamId), Q_ARG(int, maxWidth), Q_ARG(int, maxHeight));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return frame;
}

PreviewVideoFrame ClipReaderPool::readPreviewVideoFrame(const QString &path, quint64 streamId,
                                                        drift::TimeUs sourceUs, int maxWidth, int maxHeight,
                                                        const QString &stabilizePath,
                                                        int stabilizeSmoothing, bool stabilizeTripod,
                                                        int rotationCorrection)
{
    if (path.isEmpty())
        return {};

    WorkerEntry *entry = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_videoWorkers, path, streamId);
        ++entry->inFlight;
    }
    ClipReaderWorker *worker = entry->worker;

    PreviewVideoFrame frame;
    QElapsedTimer decodeWait;
    decodeWait.start();
    QMetaObject::invokeMethod(worker, "decodePreviewVideo", Qt::BlockingQueuedConnection,
                               Q_RETURN_ARG(PreviewVideoFrame, frame), Q_ARG(quint64, streamId),
                               Q_ARG(drift::TimeUs, sourceUs), Q_ARG(int, maxWidth),
                               Q_ARG(int, maxHeight),
                               Q_ARG(QString, stabilizePath), Q_ARG(int, stabilizeSmoothing),
                               Q_ARG(bool, stabilizeTripod), Q_ARG(int, rotationCorrection));
    t_decodeWaitNs += decodeWait.nsecsElapsed();

    worker->requestPrefetchPreview(streamId, maxWidth, maxHeight,
                                   m_readAheadUs.load(std::memory_order_relaxed));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return frame;
}

int ClipReaderPool::readAudioInterleaved(const QString &path, quint64 streamId,
                                         drift::TimeUs sourceStartUs, int sampleCount,
                                         int outputSampleRate, float *interleavedStereoOut,
                                         int audioStreamOrdinal)
{
    if (path.isEmpty() || !interleavedStereoOut || sampleCount <= 0)
        return 0;

    WorkerEntry *entry = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_audioWorkers, path);
        ++entry->inFlight;
    }

    int written = 0;
    QMetaObject::invokeMethod(entry->worker, "decodeAudio", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, written),
                              Q_ARG(quint64, streamId), Q_ARG(drift::TimeUs, sourceStartUs),
                              Q_ARG(int, sampleCount), Q_ARG(int, outputSampleRate),
                              Q_ARG(float *, interleavedStereoOut),
                              Q_ARG(int, audioStreamOrdinal));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return written;
}

void ClipReaderPool::resetAudioStreams()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_audioWorkers)
        entry.second->worker->requestAudioReposition();
}

int ClipReaderPool::videoWorkerCount()
{
    QMutexLocker lock(&m_mutex);
    return int(m_videoWorkers.size());
}

void ClipReaderPool::retainActivePaths(const QSet<QString> &videoPaths, const QSet<QString> &audioPaths)
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        // VEDIT (조박사님: "멈추고 깜박거린다"): video workers are per clip now, each with its own
        // (often hardware, 4K) decoder. One a clip has not asked of for a few seconds is closed —
        // after a run of cuts they piled up (ten 4K NVDEC sessions) until the GPU ran out of
        // memory and every frame took seconds. Every read restarts lastUse, so the clips on screen
        // and the one being warmed stay open; going back to an old piece reopens it.
        // On top of that, never keep more than kMaxVideoWorkers: the least recently read go first
        // (only ones idle for half a second — anything mid-read or just read stays).
#ifndef Q_OS_ANDROID
        Q_UNUSED(videoPaths);
        evicted = detachIdleLocked(m_videoWorkers, {}, kVideoIdleCloseMs);
        // VEDIT (Codex 검토 1): the half-second grace alone let a run of short pieces — every one
        // just read or just warmed — sit above the cap. Anything not on screen or about to start is
        // closed regardless; the cap only stretches when more clips than that are on screen at once.
        const int cap = qMax(kMaxVideoWorkers, int(m_protectedVideoKeys.size()));
        while (int(m_videoWorkers.size()) > cap) {
            auto oldest = m_videoWorkers.end();
            for (auto it = m_videoWorkers.begin(); it != m_videoWorkers.end(); ++it) {
                if (it->second->inFlight > 0 || m_protectedVideoKeys.contains(it->first))
                    continue;
                if (oldest == m_videoWorkers.end()
                    || it->second->lastUse.elapsed() > oldest->second->lastUse.elapsed())
                    oldest = it;
            }
            if (oldest == m_videoWorkers.end())
                break;
            evicted.push_back(std::move(oldest->second));
            m_videoWorkers.erase(oldest);
        }
#endif
        for (const QString &path : audioPaths)
            ensureWorker(m_audioWorkers, path);
#ifdef Q_OS_ANDROID
        // Android only. This runs from FrameCompositor::prepare, i.e. once per composited frame,
        // and on desktop a decoder that has been off the playhead for ten seconds is one the user
        // is about to scrub back onto — keeping it open there is the whole point of the pool.
        // A phone cannot afford the thread and the open AVCodecContext per path that costs.
        evicted = detachIdleLocked(m_videoWorkers, videoPaths, kIdleReleaseMs);
        std::vector<std::unique_ptr<WorkerEntry>> audio =
            detachIdleLocked(m_audioWorkers, audioPaths, kIdleReleaseMs);
        evicted.insert(evicted.end(), std::make_move_iterator(audio.begin()),
                       std::make_move_iterator(audio.end()));
#endif
    }
#ifndef Q_OS_ANDROID
    // VEDIT: this runs on the render thread every frame; closing a 4K hardware decoder and joining
    // its thread can take a while, so the closed workers are torn down off to the side.
    if (!evicted.empty()) {
        std::thread([doomed = std::move(evicted)]() mutable {
            for (const std::unique_ptr<WorkerEntry> &entry : doomed)
                stopWorkerEntry(*entry);
        }).detach();
    }
#else
    for (const std::unique_ptr<WorkerEntry> &entry : evicted)
        stopWorkerEntry(*entry);
#endif
}

namespace drift {

MediaCodecSurfaceDecodeBlock::MediaCodecSurfaceDecodeBlock()
{
#ifdef Q_OS_ANDROID
    ClipReader::setSurfaceDecodeAllowed(false);
    ClipReaderPool::instance().resetVideoDecoders();
#endif
}

MediaCodecSurfaceDecodeBlock::~MediaCodecSurfaceDecodeBlock()
{
#ifdef Q_OS_ANDROID
    ClipReader::setSurfaceDecodeAllowed(true);
    ClipReaderPool::instance().resetVideoDecoders();
#endif
}

} // namespace drift
