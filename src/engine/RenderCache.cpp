#include "RenderCache.h"

#include "core/TimelineOps.h"
#include "Exporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

// Keys that failed to render (a codec error, a missing file): not retried until they change.
QSet<QString> &failedKeys()
{
    static QSet<QString> keys;
    return keys;
}

bool hasVideoEffects(const drift::Clip &clip)
{
    for (const drift::Effect &effect : clip.effects)
        if (effect.enabled)
            return true;
    return false;
}

} // namespace

RenderCache &RenderCache::instance()
{
    static RenderCache *cache = new RenderCache();
    return *cache;
}

RenderCache::RenderCache()
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/render-cache");
    QDir().mkpath(m_dir);
}

RenderCache::~RenderCache() = default;

bool RenderCache::clipNeedsRender(const drift::Project &project, int trackIndex, int clipIndex)
{
    const drift::Track &track = project.tracks().at(trackIndex);
    if (track.hidden || track.type == drift::TrackType::Audio)
        return false;
    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type == drift::ClipType::Adjustment)
        return clip.adjustmentKind == drift::AdjustmentKind::VideoEffects && hasVideoEffects(clip);
    if (clip.type != drift::ClipType::Video)
        return hasVideoEffects(clip);
    return std::abs(clip.speed - 1.0) > 0.001 || clip.hasSpeedCurve() || clip.reverse || hasVideoEffects(clip);
}

void RenderCache::invalidate()
{
    bool any = false;
    {
        QMutexLocker lock(&m_mutex);
        for (Segment &segment : m_segments) {
            any = any || segment.ready;
            segment.ready = false;
        }
    }
    ++m_generation;
    if (any)
        emit segmentsChanged(); // the band turns orange at once, not after the re-key
}

void RenderCache::update(const drift::Project &project)
{
    // The stretches to render: each clip that needs it, overlapping ones (a dissolve) merged.
    QList<QPair<drift::TimeUs, drift::TimeUs>> ranges;
    if (m_enabled.load()) {
        for (int t = 0; t < project.tracks().size(); ++t)
            for (int c = 0; c < project.tracks().at(t).clips.size(); ++c)
                if (clipNeedsRender(project, t, c)) {
                    const drift::Clip &clip = project.tracks().at(t).clips.at(c);
                    if (clip.timelineDuration > 0)
                        ranges.append({clip.timelineStart, clip.timelineStart + clip.timelineDuration});
                }
    }
    std::sort(ranges.begin(), ranges.end());
    QList<QPair<drift::TimeUs, drift::TimeUs>> merged;
    for (const auto &r : std::as_const(ranges)) {
        if (!merged.isEmpty() && r.first < merged.last().second)
            merged.last().second = qMax(merged.last().second, r.second);
        else
            merged.append(r);
    }

    // Each keyed by everything visual that overlaps it.
    QList<Segment> segments;
    for (const auto &r : std::as_const(merged)) {
        drift::Project slice = project;
        slice.assets().clear();
        slice.assetOrder().clear();
        for (drift::Track &track : slice.tracks()) {
            if (track.type == drift::TrackType::Audio) {
                track.clips.clear();
                track.transitions.clear();
                continue;
            }
            QSet<QString> kept;
            for (int i = track.clips.size() - 1; i >= 0; --i) {
                drift::Clip &clip = track.clips[i];
                if (clip.timelineStart >= r.second || clip.timelineStart + clip.timelineDuration <= r.first) {
                    track.clips.removeAt(i);
                    continue;
                }
                clip.thumbnailPath.clear();
                clip.filmstripPath.clear();
                kept.insert(clip.id);
            }
            for (int i = track.transitions.size() - 1; i >= 0; --i)
                if (!kept.contains(track.transitions.at(i).fromClipId) && !kept.contains(track.transitions.at(i).toClipId))
                    track.transitions.removeAt(i);
        }
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QJsonDocument(slice.toJson(false)).toJson(QJsonDocument::Compact));
        hash.addData(QStringLiteral("|%1|%2|%3x%4@%5|v3") // VEDIT: v3 = 720p cache
                         .arg(r.first).arg(r.second).arg(project.width()).arg(project.height())
                         .arg(project.fps()).toUtf8());
        Segment segment;
        segment.startUs = r.first;
        segment.endUs = r.second;
        segment.key = QString::fromLatin1(hash.result().toHex().left(24));
        segment.path = m_dir + QLatin1Char('/') + segment.key + QStringLiteral(".mp4");
        segment.ready = QFileInfo(segment.path).size() > 0;
        segments.append(segment);
    }

    {
        QMutexLocker lock(&m_mutex);
        m_segments = segments;
        m_project = std::make_shared<const drift::Project>(project);
    }

    // Old renders the timeline no longer uses: dropped after two weeks, or oldest first once the
    // folder passes 4 GB. Only this folder's own files are ever touched.
    QSet<QString> inUse;
    for (const Segment &segment : std::as_const(segments))
        inUse.insert(QFileInfo(segment.path).fileName());
    QFileInfoList files = QDir(m_dir).entryInfoList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Time);
    qint64 total = 0;
    for (const QFileInfo &file : std::as_const(files))
        total += file.size();
    const QDateTime staleBefore = QDateTime::currentDateTime().addDays(-14);
    for (int i = files.size() - 1; i >= 0; --i) { // oldest first
        const QFileInfo &file = files.at(i);
        if (inUse.contains(file.fileName()))
            continue;
        if (total > 4LL * 1024 * 1024 * 1024 || file.lastModified() < staleBefore) {
            total -= file.size();
            QFile::remove(file.absoluteFilePath());
        }
    }
    ++m_generation;
    emit segmentsChanged();
    kick();
}

void RenderCache::setPlaying(bool playing)
{
    m_playing.store(playing);
    if (!playing)
        kick();
}

void RenderCache::setEnabled(bool enabled)
{
    m_enabled.store(enabled);
    if (!enabled)
        invalidate();
}

bool RenderCache::lookup(drift::TimeUs timelineUs, Hit *hit) const
{
    if (!m_enabled.load())
        return false;
    QMutexLocker lock(&m_mutex);
    for (const Segment &segment : m_segments) {
        if (segment.ready && timelineUs >= segment.startUs && timelineUs < segment.endUs) {
            if (hit)
                *hit = Hit{segment.startUs, segment.endUs, segment.path, segment.key};
            return true;
        }
    }
    return false;
}

QVariantList RenderCache::segmentsForUi() const
{
    QVariantList out;
    QMutexLocker lock(&m_mutex);
    for (const Segment &segment : m_segments)
        out.append(QVariantMap{{QStringLiteral("start"), drift::usToSeconds(segment.startUs)},
                               {QStringLiteral("end"), drift::usToSeconds(segment.endUs)},
                               {QStringLiteral("ready"), segment.ready}});
    return out;
}

void RenderCache::kick()
{
    // A moment's grace: edits come in bursts, and playback often resumes right after a stop.
    QTimer::singleShot(1500, this, [this] { renderNext(); });
}

void RenderCache::renderNext()
{
    if (!m_enabled.load() || m_playing.load() || m_busy.load())
        return;
    Segment target;
    std::shared_ptr<const drift::Project> project;
    {
        QMutexLocker lock(&m_mutex);
        for (const Segment &segment : std::as_const(m_segments))
            if (!segment.ready && !failedKeys().contains(segment.key)) {
                target = segment;
                break;
            }
        project = m_project;
    }
    if (target.key.isEmpty() || !project)
        return;

    m_busy.store(true);
    const int generation = m_generation.load();
    (void)QtConcurrent::run([this, target, project, generation] {
        // Picture only: the sound still plays live, so the render mixes none.
        drift::Project copy = *project;
        for (drift::Track &track : copy.tracks()) {
            if (track.type == drift::TrackType::Audio)
                track.clips.clear();
            for (drift::Clip &clip : track.clips)
                clip.suppressEmbeddedAudio = true;
        }
        ExportSettings settings = Exporter::defaultSettings();
        settings.startUs = target.startUs;
        settings.endUs = target.endUs;
        // VEDIT (조박사님: "화질이 안 좋다"): played back in place of the live picture, so it must
        // look like it — full HD (or the project's own height, if lower), not 540p.
        // VEDIT (조박사님 2026-10-23): 720p — 1080p made playback stutter.
        settings.targetHeight = qMin(720, copy.height());
        const bool nvenc = Exporter::videoCodecById(QStringLiteral("h264_nvenc"))
                               .value(QStringLiteral("available")).toBool();
        settings.videoCodecId = nvenc ? QStringLiteral("h264_nvenc") : QStringLiteral("h264");
        settings.videoPreset = nvenc ? QStringLiteral("p2") : QStringLiteral("superfast");
        settings.rateControl = QStringLiteral("crf");
        settings.crf = 19;
        settings.audioCodecId = QStringLiteral("aac");
        settings.audioBitrateKbps = 64;

        const QString part = target.path + QStringLiteral(".part.mp4");
        QString error;
        bool cancelled = false;
        const auto progress = [this, generation, &cancelled](double) {
            cancelled = m_playing.load() || m_generation.load() != generation || !m_enabled.load();
            return !cancelled;
        };
        bool ok = Exporter::run(copy, settings, part, &error, progress);
        // The hardware encoder refuses some sizes (very small frames): x264 takes anything.
        if (!ok && !cancelled && nvenc) {
            QFile::remove(part);
            settings.videoCodecId = QStringLiteral("h264");
            settings.videoPreset = QStringLiteral("ultrafast");
            ok = Exporter::run(copy, settings, part, &error, progress);
        }
        if (!ok && !cancelled)
            qWarning("RenderCache: rendering %s failed: %s", qPrintable(target.key), qPrintable(error));
        bool done = false;
        if (ok && !cancelled) {
            QFile::remove(target.path);
            done = QFile::rename(part, target.path);
        }
        QFile::remove(part);

        QMetaObject::invokeMethod(this, [this, target, done, ok, cancelled, generation] {
            m_busy.store(false);
            if (!ok && !cancelled)
                failedKeys().insert(target.key);
            // VEDIT (Codex 검토 6): an edit that landed while this finished has already stopped
            // trusting the segments, which still carry their pre-edit keys until update() re-keys
            // them. Marking them ready here would show the old render for that wait. The file stays
            // on disk under its key, so an undo back to it still finds it.
            if (done && m_generation.load() == generation) {
                QMutexLocker lock(&m_mutex);
                for (Segment &segment : m_segments)
                    if (segment.key == target.key)
                        segment.ready = true;
            }
            emit segmentsChanged();
            kick();
        }, Qt::QueuedConnection);
    });
}
