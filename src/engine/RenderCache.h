#pragma once

// VEDIT (조박사님): 임시 렌더링. Stretches of the timeline that are heavy to play live — a clip
// with its speed changed (rate, reverse, speed curve) or with video effects — are rendered in the
// background to a small preview-size file, and live playback shows that file instead of
// compositing them frame by frame. Like VEGAS's selective prerender, but automatic.
//
// A segment is valid only for the exact timeline content it was rendered from: its key hashes
// every visual clip overlapping it, so any edit there makes a new key and the old file is ignored
// (the stretch is rendered again). Rendering pauses while the timeline plays. Export never uses
// the cache: only playback passes RenderOptions::useRenderCache.

#include "core/Project.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>

#include <atomic>
#include <memory>

class RenderCache : public QObject
{
    Q_OBJECT

public:
    struct Segment
    {
        drift::TimeUs startUs = 0;
        drift::TimeUs endUs = 0;
        QString key;
        QString path;
        bool ready = false;
    };

    struct Hit
    {
        drift::TimeUs startUs = 0;
        drift::TimeUs endUs = 0;
        QString path;
        QString key;
    };

    static RenderCache &instance();

    // Any edit: stop trusting every segment until the next update() has re-keyed them. Cheap,
    // GUI thread.
    void invalidate();
    // Re-key the segments from `project` (GUI thread, debounced by the caller) and start rendering
    // whatever is missing.
    void update(const drift::Project &project);
    // Playback running: rendering waits (a running render is stopped and retried later).
    void setPlaying(bool playing);
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled.load(); }

    // Thread-safe: the ready segment covering `timelineUs`, for the compositor.
    bool lookup(drift::TimeUs timelineUs, Hit *hit) const;

    // For the ruler band: [{start, end, ready}] in seconds.
    QVariantList segmentsForUi() const;

    // Which clips make a stretch worth rendering.
    static bool clipNeedsRender(const drift::Project &project, int trackIndex, int clipIndex);

signals:
    void segmentsChanged();

private:
    RenderCache();
    ~RenderCache() override;
    void kick();
    void renderNext();

    mutable QMutex m_mutex;
    QList<Segment> m_segments;
    std::shared_ptr<const drift::Project> m_project;
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_busy{false};
    std::atomic<int> m_generation{0};
    QString m_dir;
};
