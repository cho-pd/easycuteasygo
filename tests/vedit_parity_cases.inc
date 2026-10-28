// VEDIT: CapCut parity cases — spec in VEDIT/docs/capcut-parity-spec.md.
// Each case builds its own controller with VEDIT's defaults switched on explicitly (the suite's
// initTestCase turns ripple off for the upstream cases).

namespace {

struct ParityFixture
{
    AssetLibrary library;
    AppController state{&library};

    explicit ParityFixture(int trackCount = 1, drift::TrackType type = drift::TrackType::Video)
    {
        state.setRippleEnabled(true);
        state.setAllowClipOverlap(true);
        state.setSnapEnabled(false);
        state.project()->tracks().clear();
        for (int i = 0; i < trackCount; ++i)
            state.project()->tracks().append(drift::Track{.type = type});
    }

    void add(int track, const QString &id, double start, double duration,
             drift::ClipType type = drift::ClipType::Shape, const QString &linkId = {})
    {
        drift::Clip clip;
        clip.id = id;
        clip.type = type;
        clip.timelineStart = drift::secondsToUs(start);
        clip.timelineDuration = drift::secondsToUs(duration);
        clip.srcIn = 0;
        clip.srcOut = clip.timelineDuration;
        clip.linkId = linkId;
        state.project()->tracks()[track].clips.append(clip);
    }

    void ready() { state.project()->ensureTrackIds(); }

    const drift::Clip *clip(int track, const QString &id) const
    {
        return drift::clipById(state.project()->tracks().at(track), id);
    }

    int indexOf(int track, const QString &id) const
    {
        const auto &clips = state.project()->tracks().at(track).clips;
        for (int i = 0; i < clips.size(); ++i)
            if (clips.at(i).id == id)
                return i;
        return -1;
    }

    double startOf(int track, const QString &id) const
    {
        const drift::Clip *c = clip(track, id);
        return c ? drift::usToSeconds(c->timelineStart) : -1.0;
    }

    int clipCount(int track) const { return state.project()->tracks().at(track).clips.size(); }
};

bool nearly(double a, double b) { return qAbs(a - b) < 1e-3; }

} // namespace

void EditorStateTest::parity_P01_splitShortcutIsS()
{
    ParityFixture f;
    QCOMPARE(f.state.shortcutFor(QStringLiteral("split")), QStringLiteral("S"));
}

void EditorStateTest::parity_P02_splitSelectedOnly()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 4.0);
    f.add(1, QStringLiteral("b"), 0.0, 4.0);
    f.ready();
    f.state.selectClip(0, 0);
    f.state.setPlayheadSeconds(2.0);
    f.state.splitAtPlayhead();
    QCOMPARE(f.clipCount(0), 2);
    QCOMPARE(f.clipCount(1), 1); // the unselected clip on the other track is untouched
}

// VEDIT (조박사님, 2026-10-19): with nothing selected, S cuts nothing (was: everything under the
// playhead).
void EditorStateTest::parity_P03_splitWithoutSelectionSplitsAllUnderPlayhead()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 4.0);
    f.add(1, QStringLiteral("b"), 0.0, 4.0);
    f.ready();
    f.state.clearSelection();
    f.state.setPlayheadSeconds(2.0);
    f.state.splitAtPlayhead();
    QCOMPARE(f.clipCount(0), 1);
    QCOMPARE(f.clipCount(1), 1);
}

void EditorStateTest::parity_P04_deleteClosesGap()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 2.0);
    f.add(0, QStringLiteral("b"), 2.0, 3.0);
    f.add(0, QStringLiteral("c"), 5.0, 2.0);
    f.add(1, QStringLiteral("other"), 6.0, 1.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("b")));
    f.state.deleteSelectedClip();
    QCOMPARE(f.clipCount(0), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 2.0));
    QVERIFY(nearly(f.startOf(1, QStringLiteral("other")), 6.0));
}

void EditorStateTest::parity_P05_deleteLeftPullsRemainderBack()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 2.0);
    f.add(0, QStringLiteral("b"), 2.0, 4.0);
    f.add(0, QStringLiteral("c"), 6.0, 2.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("b")));
    f.state.setPlayheadSeconds(3.0); // drop 1s from the front of b
    f.state.splitSelectedClipLeft();
    const drift::Track &t = f.state.project()->tracks().at(0);
    // b's remainder starts where b started, and c follows it with no gap.
    double bStart = -1, bEnd = -1;
    for (const drift::Clip &c : t.clips) {
        if (c.id != QStringLiteral("a") && c.id != QStringLiteral("c")) {
            bStart = drift::usToSeconds(c.timelineStart);
            bEnd = drift::usToSeconds(c.timelineEnd());
        }
    }
    QVERIFY2(nearly(bStart, 2.0), qPrintable(QStringLiteral("b starts at %1").arg(bStart)));
    QVERIFY2(nearly(bEnd, 5.0), qPrintable(QStringLiteral("b ends at %1").arg(bEnd)));
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("c")), 5.0),
             qPrintable(QStringLiteral("c starts at %1").arg(f.startOf(0, QStringLiteral("c")))));
}

void EditorStateTest::parity_P06_deleteRightPullsFollowing()
{
    ParityFixture f;
    f.add(0, QStringLiteral("b"), 0.0, 4.0);
    f.add(0, QStringLiteral("c"), 4.0, 2.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("b")));
    f.state.setPlayheadSeconds(3.0); // drop the last 1s of b
    f.state.splitSelectedClipRight();
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("c")), 3.0),
             qPrintable(QStringLiteral("c starts at %1").arg(f.startOf(0, QStringLiteral("c")))));
}

void EditorStateTest::parity_P07_trimEndRipplesFollowing()
{
    ParityFixture f;
    f.add(0, QStringLiteral("b"), 0.0, 4.0);
    f.add(0, QStringLiteral("c"), 4.0, 2.0);
    f.ready();
    f.state.trimClipRight(0, f.indexOf(0, QStringLiteral("b")), 3.0);
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("c")), 3.0),
             qPrintable(QStringLiteral("after shortening, c starts at %1").arg(f.startOf(0, QStringLiteral("c")))));
}

void EditorStateTest::parity_P09_speedRange()
{
    ParityFixture f;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipSpeed(0, 0, 0.1);
    QVERIFY2(nearly(f.clip(0, QStringLiteral("v"))->speed, 0.1),
             qPrintable(QStringLiteral("slowest speed %1").arg(f.clip(0, QStringLiteral("v"))->speed)));
    f.state.setClipSpeed(0, 0, 100.0);
    QVERIFY2(nearly(f.clip(0, QStringLiteral("v"))->speed, 100.0),
             qPrintable(QStringLiteral("fastest speed %1").arg(f.clip(0, QStringLiteral("v"))->speed)));
}

void EditorStateTest::parity_P10_overlapMakesCurvedDissolve()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 2.0);
    f.add(0, QStringLiteral("b"), 2.0, 2.0);
    f.ready();
    f.state.moveClip(0, f.indexOf(0, QStringLiteral("b")), 1.5);
    const auto &transitions = f.state.project()->tracks().at(0).transitions;
    QCOMPARE(transitions.size(), 1);
    QVERIFY(transitions.at(0).autoOverlap);
    const auto mid = drift::transitionAudioGains(QStringLiteral("crossfade"), 0.5);
    QVERIFY(mid.outgoing > 0.7 && mid.incoming > 0.7); // curves cross above the middle
}

void EditorStateTest::parity_P11_duplicateInsertsAfterOriginal()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 2.0);
    f.add(0, QStringLiteral("c"), 2.0, 2.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("a")));
    f.state.duplicateSelectedClip();
    QCOMPARE(f.clipCount(0), 3);
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("c")), 4.0),
             qPrintable(QStringLiteral("c starts at %1").arg(f.startOf(0, QStringLiteral("c")))));
    QVERIFY2(f.state.project()->tracks().at(0).transitions.isEmpty(),
             "duplicating must not overlap and create a dissolve");
}

void EditorStateTest::parity_P12_undoRestoresDelete()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 2.0);
    f.add(0, QStringLiteral("b"), 2.0, 2.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("a")));
    f.state.deleteSelectedClip();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 0.0));
    f.state.undo();
    QCOMPARE(f.clipCount(0), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 2.0));
}

void EditorStateTest::parity_P14_deleteClosesOnlyUncoveredGap()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 3.0);   // reaches 1s into b's slot
    f.add(0, QStringLiteral("b"), 2.0, 3.0);   // 2..5
    f.add(0, QStringLiteral("c"), 5.0, 2.0);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("b")));
    f.state.deleteSelectedClip();
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("c")), 3.0),
             qPrintable(QStringLiteral("c starts at %1").arg(f.startOf(0, QStringLiteral("c")))));
}

void EditorStateTest::parity_P19_deleteLeftRightWithoutSelectionUseMainTrack()
{
    // Track 1 is the bottom (main) video track; track 0 holds an overlay that must not be cut.
    ParityFixture f(2);
    f.add(0, QStringLiteral("overlay"), 0.0, 6.0);
    f.add(1, QStringLiteral("main"), 0.0, 4.0, drift::ClipType::Image);
    f.add(1, QStringLiteral("next"), 4.0, 2.0, drift::ClipType::Image);
    f.ready();
    f.state.clearSelection();
    f.state.setPlayheadSeconds(3.0);
    f.state.splitSelectedClipRight(); // W: drop main's last second, pull "next" back
    QVERIFY2(nearly(f.startOf(1, QStringLiteral("next")), 3.0),
             qPrintable(QStringLiteral("next starts at %1").arg(f.startOf(1, QStringLiteral("next")))));
    QCOMPARE(f.clipCount(0), 1);
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("overlay"))->timelineDuration), 6.0));
}

void EditorStateTest::parity_P20_adjustSlidersOnSelectedClip()
{
    ParityFixture f;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, 0);
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.brightness"), QStringLiteral("brightness"), 0.3);
    // The clip the user picked stays selected (the effect host is an implementation detail).
    int track = f.state.selectedTrack();
    int clip = f.state.selectedClip();
    QCOMPARE(f.state.project()->tracks().at(track).clips.at(clip).id, QStringLiteral("v"));
    QVariantMap values = f.state.clipAdjustValues(track, clip);
    QVERIFY2(nearly(values.value(QStringLiteral("adjust.brightness.brightness")).toDouble(), 0.3),
             qPrintable(QStringLiteral("brightness %1").arg(values.value(QStringLiteral("adjust.brightness.brightness")).toDouble())));
    f.state.setClipAdjust(track, clip, QStringLiteral("adjust.saturation"), QStringLiteral("saturation"), 1.5);
    values = f.state.clipAdjustValues(f.state.selectedTrack(), f.state.selectedClip());
    QVERIFY(nearly(values.value(QStringLiteral("adjust.saturation.saturation")).toDouble(), 1.5));
    QVERIFY(nearly(values.value(QStringLiteral("adjust.brightness.brightness")).toDouble(), 0.3));
    f.state.undo();
    values = f.state.clipAdjustValues(f.state.selectedTrack(), f.state.selectedClip());
    QVERIFY(!values.contains(QStringLiteral("adjust.saturation.saturation")));
}

void EditorStateTest::parity_P21_speedCurvePresetInPlace()
{
    ParityFixture f;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("next"), 4.0, 2.0);
    f.ready();
    f.state.applySpeedCurvePreset(0, 0, QStringLiteral("bullet"));
    const drift::Clip *v = f.clip(0, QStringLiteral("v"));
    QVERIFY(v->hasSpeedCurve());
    QCOMPARE(f.state.clipSpeedCurvePreset(0, f.indexOf(0, QStringLiteral("v"))), QStringLiteral("bullet"));
    QCOMPARE(f.clipCount(0), 2); // in place: no copy on a new track
    // The clip after it follows the new length, with no gap and no overlap.
    QVERIFY2(nearly(f.startOf(0, QStringLiteral("next")), drift::usToSeconds(v->timelineEnd())),
             qPrintable(QStringLiteral("next at %1, v ends %2").arg(f.startOf(0, QStringLiteral("next")))
                            .arg(drift::usToSeconds(v->timelineEnd()))));
    f.state.applySpeedCurvePreset(0, f.indexOf(0, QStringLiteral("v")), QStringLiteral("none"));
    QVERIFY(!f.clip(0, QStringLiteral("v"))->hasSpeedCurve());
    QVERIFY(nearly(f.startOf(0, QStringLiteral("next")), 4.0));
}

// VEDIT (조박사님: "속도 곡선이 전혀 안 된다"): the real case — a piece cut out of a long shot,
// its sound linked on another row, the shot's speed already touched once.
void EditorStateTest::parity_P21b_speedCurvePresetOnATrimmedLinkedPiece()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 10.0, 6.0, drift::ClipType::Video, QStringLiteral("L"));
    f.add(1, QStringLiteral("a"), 10.0, 6.0, drift::ClipType::Audio, QStringLiteral("L"));
    for (int t = 0; t < 2; ++t) {
        drift::Clip &c = f.state.project()->tracks()[t].clips[0];
        c.srcIn = drift::secondsToUs(60.0);
        c.srcOut = drift::secondsToUs(66.0);
        c.path = QStringLiteral("C:/x/DJI.MP4");
    }
    f.ready();
    f.state.selectClip(0, 0);
    f.state.setClipSpeed(0, 0, 2.0);
    f.state.setClipSpeed(0, 0, 1.0);
    for (const QString id : {QStringLiteral("montage"), QStringLiteral("hero"), QStringLiteral("bullet"),
                             QStringLiteral("jumpcut"), QStringLiteral("flashin"), QStringLiteral("flashout")}) {
        f.state.applySpeedCurvePreset(f.state.selectedTrack(), f.state.selectedClip(), id);
        const drift::Clip *v = f.clip(0, QStringLiteral("v"));
        QVERIFY2(v && v->hasSpeedCurve(), qPrintable(id));
        QCOMPARE(f.state.clipSpeedCurvePreset(0, f.indexOf(0, QStringLiteral("v"))), id);
        // It plays at a changing rate: equal stretches of timeline cover unequal stretches of source.
        const drift::TimeUs a = v->timelineToSourceUs(v->timelineStart + v->timelineDuration / 10)
            - v->timelineToSourceUs(v->timelineStart);
        const drift::TimeUs mid = v->timelineToSourceUs(v->timelineStart + v->timelineDuration / 2)
            - v->timelineToSourceUs(v->timelineStart + v->timelineDuration * 4 / 10);
        const drift::TimeUs last = v->timelineToSourceUs(v->timelineEnd())
            - v->timelineToSourceUs(v->timelineStart + v->timelineDuration * 9 / 10);
        const drift::TimeUs b = qAbs(mid - a) > qAbs(last - a) ? mid : last; // flash-out ramps only at the end
        QVERIFY2(qAbs(a - b) > 20'000, qPrintable(QStringLiteral("%1: %2 vs %3").arg(id).arg(a).arg(b)));
    }
}

// VEDIT (조박사님: "키프레임이 먹지도 않는다"): CapCut's way — ◆ on the first frame, move on, change
// the value: it animates from the first frame's value to the new one, and ◆ shows keyed both times.
void EditorStateTest::vedit_keyframeOnFirstFrameAnimates()
{
    ParityFixture f;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, 0);
    for (const QString prop : {QStringLiteral("opacity"), QStringLiteral("x"), QStringLiteral("rotation")}) {
        f.state.setPlayheadSeconds(0.0);
        const double start = f.state.propertyValueAt(0, 0, prop, 0.0, prop == QStringLiteral("opacity") ? 1.0 : 0.0);
        f.state.toggleClipKeyframeHere(0, 0, prop, start);
        QVERIFY2(f.state.clipPropsKeyState(0, 0, {prop}).value(QStringLiteral("atPlayhead")).toBool(), qPrintable(prop));
        f.state.setPlayheadSeconds(2.0);
        const double target = prop == QStringLiteral("opacity") ? 0.2 : 300.0;
        f.state.previewSetClipKeyframe(0, 0, prop, 2.0, target);
        f.state.commitPreviewDrag();
        QVERIFY2(f.state.clipPropsKeyState(0, 0, {prop}).value(QStringLiteral("atPlayhead")).toBool(), qPrintable(prop));
        QVERIFY2(nearly(f.state.propertyValueAt(0, 0, prop, 2.0, 0.0), target), qPrintable(prop));
        QVERIFY2(nearly(f.state.propertyValueAt(0, 0, prop, 0.0, 0.0), start), qPrintable(prop));
        const double mid = f.state.propertyValueAt(0, 0, prop, 1.0, 0.0);
        QVERIFY2(qMin(start, target) < mid && mid < qMax(start, target), qPrintable(prop + QStringLiteral(" mid %1").arg(mid)));
        // ◆ again on the first frame takes the key out; the 2 s one stays.
        f.state.setPlayheadSeconds(0.0);
        f.state.toggleClipKeyframeHere(0, 0, prop, start);
        QVERIFY2(!f.state.clipPropsKeyState(0, 0, {prop}).value(QStringLiteral("atPlayhead")).toBool(), qPrintable(prop));
    }
}

// VEDIT (조박사님: CapCut 비트): a mark put at the playhead, kept in the song's own time (so a trim
// does not move it), saved with the clip, snapped to, and taken off again on a second press.
void EditorStateTest::vedit_beatMarksByHand()
{
    ParityFixture f;
    f.add(0, QStringLiteral("m"), 0.0, 8.0, drift::ClipType::Audio);
    f.ready();
    f.state.setPlayheadSeconds(3.0);
    f.state.toggleBeatMarkAtPlayhead(0, 0);
    QCOMPARE(f.state.beatMarkCount(0, 0), 1);
    const drift::TimeUs src = f.clip(0, QStringLiteral("m"))->beatMarks.first();
    QVERIFY(qAbs(src - drift::secondsToUs(3.0)) < 2000);
    // Saved and read back with the clip.
    const QJsonObject saved = f.state.project()->toJson();
    const drift::Project back = drift::Project::fromJson(saved);
    QCOMPARE(back.tracks().first().clips.first().beatMarks.size(), 1);
    // A second press on it takes it off; undo puts it back.
    f.state.toggleBeatMarkAtPlayhead(0, 0);
    QCOMPARE(f.state.beatMarkCount(0, 0), 0);
    f.state.undo();
    QCOMPARE(f.state.beatMarkCount(0, 0), 1);
    f.state.clearBeatMarks(0, 0);
    QCOMPARE(f.state.beatMarkCount(0, 0), 0);
}

// VEDIT (조박사님, VEGAS): the crossfade's ends are the clips' own ends — the left end is where
// the next clip starts, the right end where the one before stops. Dragged to under a frame they
// simply meet; and the next clip cannot start earlier than its footage allows.
void EditorStateTest::vedit_crossfadeEdgesTrimAndMeet()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 5.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 5.0, drift::ClipType::Video);
    f.state.project()->tracks()[0].clips[1].srcIn = drift::secondsToUs(2.0); // 2 s of head room
    f.state.project()->tracks()[0].clips[1].srcOut = drift::secondsToUs(7.0);
    f.ready();
    // Left end earlier: b starts earlier, its far end stays.
    f.state.previewOverlapEdge(0, 0, 1, -1, 3.0);
    f.state.commitPreviewDrag();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 3.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineEnd()), 9.0));
    // Past its footage it stops: only 1 s more of head room was left.
    f.state.previewOverlapEdge(0, 0, 1, -1, 0.5);
    f.state.commitPreviewDrag();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 2.0));
    // Left end dragged onto the right end: no overlap, the clips meet where a ends.
    f.state.previewOverlapEdge(0, 0, 1, -1, 4.99);
    f.state.commitPreviewDrag();
    const drift::Clip *a2 = f.clip(0, QStringLiteral("a"));
    const drift::Clip *b2 = f.clip(0, QStringLiteral("b"));
    QCOMPARE(b2->timelineStart, a2->timelineEnd());
    QVERIFY(nearly(drift::usToSeconds(b2->timelineEnd()), 9.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0)); // the one before never moved
}

// VEDIT (조박사님: Alt + 끌기): the dissolve slides along at the same length — the clip before's end
// and the next clip's start move together, the far ends and other clips stay, and it stops where
// the next clip's footage (srcIn) runs out or it would leave the next clip shorter than a frame.
void EditorStateTest::vedit_altDragSlidesDissolve()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 5.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 5.0, drift::ClipType::Video); // overlap 4..5
    f.add(0, QStringLiteral("c"), 9.0, 2.0, drift::ClipType::Video);
    f.state.project()->tracks()[0].clips[1].srcIn = drift::secondsToUs(1.0);
    f.state.project()->tracks()[0].clips[1].srcOut = drift::secondsToUs(6.0);
    {   // a's footage runs 10 s, so it has 5 s past its end
        drift::MediaAsset asset;
        asset.id = QStringLiteral("a.mp4");
        asset.name = asset.id;
        asset.kind = drift::MediaKind::Video;
        asset.path = QStringLiteral("/nonexistent/a.mp4");
        asset.durationUs = drift::secondsToUs(10.0);
        f.state.project()->addAsset(asset);
        f.state.project()->tracks()[0].clips[0].assetId = asset.id;
    }
    f.ready();
    const auto overlapOf = [&f]() {
        return drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()
                                  - f.clip(0, QStringLiteral("b"))->timelineStart);
    };
    // Later by 2 s.
    f.state.previewSlideOverlap(0, 0, 1, 2.0);
    f.state.commitPreviewDrag();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 7.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 6.0));
    QVERIFY(nearly(overlapOf(), 1.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineEnd()), 9.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 9.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->srcIn), 3.0));
    // Far earlier: stops where b's footage runs out (3 s of head room).
    f.state.previewSlideOverlap(0, 0, 1, -5.0);
    f.state.commitPreviewDrag();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 3.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 4.0));
    QVERIFY(nearly(overlapOf(), 1.0));
    QVERIFY(f.clip(0, QStringLiteral("b"))->srcIn >= 0);
    // Far later: stops a frame before b's end; b's end and c stay.
    f.state.previewSlideOverlap(0, 0, 1, 10.0);
    f.state.commitPreviewDrag();
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    const drift::Clip *b = f.clip(0, QStringLiteral("b"));
    QVERIFY(a->timelineEnd() < b->timelineEnd());
    QVERIFY(b->timelineDuration >= drift::frameDurationUs(30));
    QVERIFY(nearly(drift::usToSeconds(b->timelineEnd()), 9.0));
    QVERIFY(nearly(overlapOf(), 1.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 9.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));
}

// VEDIT (조박사님): a 5 s dissolve; its right end brought in to 2 s from its left, then its left end
// brought up to the same point — the moment the dissolve vanishes, the clips meet right there.
void EditorStateTest::vedit_dissolveClosedFromBothEndsMeetsThere()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 5.0, 10.0, drift::ClipType::Video); // overlap 5..10
    f.ready();
    f.state.previewOverlapEdge(0, 0, 1, +1, 7.0); // right end to 2 s in
    f.state.commitPreviewDrag();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 7.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 5.0));
    f.state.previewOverlapEdge(0, 0, 1, -1, 7.0); // left end to the same point
    f.state.commitPreviewDrag();
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    const drift::Clip *b = f.clip(0, QStringLiteral("b"));
    QVERIFY(nearly(drift::usToSeconds(a->timelineEnd()), 7.0));
    QCOMPARE(b->timelineStart, a->timelineEnd());
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));
    QVERIFY(nearly(drift::usToSeconds(b->timelineEnd()), 15.0));
}

// VEDIT (조박사님: "크로스 페이드 좌우 끝은 클립의 끝"): a dissolve always spans the clips' overlap —
// added short, or after a trim changes the overlap, it is the overlap.
void EditorStateTest::vedit_dissolveSpansTheOverlap()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 5.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 5.0, drift::ClipType::Video); // 1 s overlap
    f.ready();
    f.state.addTransition(0, 0, QStringLiteral("crossfade"), 0.1);
    QVariantMap t = f.state.transitionBetweenClips(0, 0);
    QVERIFY2(nearly(t.value(QStringLiteral("end")).toDouble() - t.value(QStringLiteral("start")).toDouble(), 1.0),
             qPrintable(QStringLiteral("%1..%2").arg(t.value(QStringLiteral("start")).toDouble())
                            .arg(t.value(QStringLiteral("end")).toDouble())));
    f.state.trimClipRight(0, 0, 4.5); // the one before now stops half a second into the next
    t = f.state.transitionBetweenClips(0, 0);
    QVERIFY(nearly(t.value(QStringLiteral("start")).toDouble(), 4.0));
    QVERIFY(nearly(t.value(QStringLiteral("end")).toDouble(), 4.5));
}

// VEDIT (조박사님): with overlap switched off, an edit that would put one clip over another moves
// the later one to a track of its own — nothing is left hidden behind another clip.
void EditorStateTest::vedit_overlapOffSeparatesTracks()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("c"), 0.0, 1.0, drift::ClipType::Video); // stray: over a from the start
    f.state.project()->tracks()[0].clips[2].timelineStart = drift::secondsToUs(2.0);
    f.ready();
    f.state.setAllowClipOverlap(false);
    f.state.setRippleEnabled(false);
    f.state.setPlayheadSeconds(0.5);
    f.state.toggleBeatMarkAtPlayhead(0, 0); // any edit settles the project
    const auto &tracks = f.state.project()->tracks();
    int videoTracks = 0;
    for (const drift::Track &tr : tracks) {
        if (tr.type != drift::TrackType::Video)
            continue;
        ++videoTracks;
        for (int i = 0; i < tr.clips.size(); ++i)
            for (int j = i + 1; j < tr.clips.size(); ++j)
                QVERIFY2(tr.clips.at(i).timelineEnd() <= tr.clips.at(j).timelineStart
                             || tr.clips.at(j).timelineEnd() <= tr.clips.at(i).timelineStart,
                         "two clips still overlap on one track");
    }
    QVERIFY(videoTracks >= 2);
}

// VEDIT (조박사님): the dissolve's right end brought left past its left end stops where the overlap
// is gone — the clip before ends exactly where the next one starts; nothing moves beyond that.
void EditorStateTest::vedit_dissolveEndTakenPastTheOther()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 5.0, 10.0, drift::ClipType::Video); // overlap 5..10
    f.add(0, QStringLiteral("c"), 15.0, 2.0, drift::ClipType::Video);
    f.ready();
    f.state.previewOverlapEdge(0, 0, 1, +1, 3.0); // asked for past the left end
    f.state.commitPreviewDrag();
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    const drift::Clip *b = f.clip(0, QStringLiteral("b"));
    QVERIFY(nearly(drift::usToSeconds(a->timelineEnd()), 5.0)); // stopped at the meeting point
    QCOMPARE(b->timelineStart, a->timelineEnd());
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 5.0));   // the next clip never moved
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 15.0));
    QVERIFY(f.state.transitionBetweenClips(0, f.indexOf(0, QStringLiteral("a"))).isEmpty());
}

// VEDIT (조박사님: "단 1개의 프레임도 겹치기 없어"): two clips overlapping by less than a frame are
// made to meet; and anything a frame or more is a crossfade — never a clip hidden under another.
void EditorStateTest::vedit_noOverlapWithoutCrossfade()
{
    ParityFixture f;
    f.add(0, QStringLiteral("a"), 0.0, 5.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.99, 5.0, drift::ClipType::Video); // 10 ms: under a frame
    f.add(0, QStringLiteral("c"), 9.0, 3.0, drift::ClipType::Video);  // 0.99 s: a real hand-off
    f.add(0, QStringLiteral("d"), 10.0, 0.5, drift::ClipType::Video); // wholly inside c
    f.ready();
    f.state.setPlayheadSeconds(1.0);
    f.state.toggleBeatMarkAtPlayhead(0, f.indexOf(0, QStringLiteral("a"))); // any edit settles it
    // Tracks may have been added above, so look the clips up wherever they are.
    const auto find = [&f](const QString &id, int *trackOut) -> const drift::Clip * {
        const auto &tracks = f.state.project()->tracks();
        for (int ti = 0; ti < tracks.size(); ++ti)
            for (const drift::Clip &c : tracks.at(ti).clips)
                if (c.id == id) {
                    *trackOut = ti;
                    return &c;
                }
        return nullptr;
    };
    int ta = -1, tb = -1, tc = -1, td = -1;
    const drift::Clip *a = find(QStringLiteral("a"), &ta);
    const drift::Clip *b = find(QStringLiteral("b"), &tb);
    QVERIFY(a && b && find(QStringLiteral("c"), &tc) && find(QStringLiteral("d"), &td));
    QCOMPARE(ta, tb);
    QCOMPARE(a->timelineEnd(), b->timelineStart);
    QVERIFY(td != tc); // the clip wholly inside c moved to a track of its own
    for (const drift::Track &tr : f.state.project()->tracks())
        for (const drift::Clip &x : tr.clips)
            for (const drift::Clip &y : tr.clips) {
                if (&x == &y || y.timelineStart <= x.timelineStart || y.timelineStart >= x.timelineEnd())
                    continue;
                bool crossfaded = false;
                for (const drift::Transition &tn : tr.transitions)
                    crossfaded = crossfaded || (tn.fromClipId == x.id && tn.toClipId == y.id);
                QVERIFY2(crossfaded, qPrintable(x.id + QStringLiteral(" over ") + y.id + QStringLiteral(" without a crossfade")));
            }
}

void EditorStateTest::parity_P25_clipCropPersistsAndUndoes()
{
    ParityFixture f;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipCrop(0, 0, 0.1, 0.2, 0.3, 0.9); // bottom inset clamps so 2% stays
    const drift::Clip *v = f.clip(0, QStringLiteral("v"));
    QVERIFY(v->hasCrop());
    QVERIFY(nearly(v->cropL, 0.1) && nearly(v->cropT, 0.2) && nearly(v->cropR, 0.3));
    QVERIFY2(nearly(v->cropB, 0.78), qPrintable(QString::number(v->cropB)));

    // Survives a save/load round trip.
    const drift::Project loaded = drift::Project::fromJson(f.state.project()->toJson());
    const drift::Clip &l = loaded.tracks().at(0).clips.at(0);
    QVERIFY(nearly(l.cropL, 0.1) && nearly(l.cropT, 0.2) && nearly(l.cropR, 0.3) && nearly(l.cropB, 0.78));

    f.state.undo();
    QVERIFY(!f.clip(0, QStringLiteral("v"))->hasCrop());
}

// VEDIT P33: a clip is picked up and dropped on another track, up or down; dropped above every
// row it gets a new track of its own kind there.
void EditorStateTest::parity_P33_dragClipBetweenTracks()
{
    ParityFixture f(2);
    f.add(1, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();

    f.state.selectClip(1, 0);
    f.state.dropClip(1, 0, 0, 5.0, -1, false); // up one row
    QCOMPARE(f.clipCount(1), 0);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 5.0));

    f.state.selectClip(0, 0);
    f.state.dropClip(0, 0, -1, 2.0, 0, false); // above the top row
    QCOMPARE(f.state.project()->tracks().size(), 3);
    QCOMPARE(f.state.project()->tracks().at(0).type, drift::TrackType::Video);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 2.0));
    QCOMPARE(f.clipCount(1), 0);

    f.state.undo(); // one step back: the new track goes with it
    QCOMPARE(f.state.project()->tracks().size(), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 5.0));
}

// VEDIT P34: Ctrl held at the drop copies — the original stays put, the copy is a separate clip
// (own id, own link group) and is what ends up selected. One undo removes it.
void EditorStateTest::parity_P34_ctrlDragCopies()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video, QStringLiteral("L"));
    f.ready();

    f.state.selectClip(0, 0);
    f.state.dropClip(0, 0, 0, 6.0, -1, true);
    QCOMPARE(f.clipCount(0), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));
    const drift::Clip &copy = f.state.project()->tracks().at(0).clips.at(1);
    QVERIFY(copy.id != QStringLiteral("a"));
    QVERIFY(nearly(drift::usToSeconds(copy.timelineStart), 6.0));
    QVERIFY(!copy.linkId.isEmpty() && copy.linkId != QStringLiteral("L"));
    QCOMPARE(f.state.selectedClip(), 1);

    f.state.undo();
    QCOMPARE(f.clipCount(0), 1);

    // Copy onto a new track below.
    f.state.selectClip(0, 0);
    f.state.dropClip(0, 0, -1, 0.0, 1, true);
    QCOMPARE(f.state.project()->tracks().size(), 2);
    QCOMPARE(f.clipCount(0), 1);
    QCOMPARE(f.clipCount(1), 1);
    QVERIFY(f.state.project()->tracks().at(1).clips.at(0).id != QStringLiteral("a"));
}

// VEDIT P35: a track an edit leaves empty goes away by itself (CapCut); a track the user has
// just added stays until something is put on it or another edit runs.
void EditorStateTest::parity_P35_emptyTracksRemoveThemselves()
{
    struct Scope {
        Scope() { AppController::s_autoRemoveEmptyTracks = true; }
        ~Scope() { AppController::s_autoRemoveEmptyTracks = false; }
    } scope;
    // The tidy runs right after the edit returns (so the edit's own indices stay valid).
    const auto settle = [] { QCoreApplication::processEvents(); };

    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();

    // Move b up onto a's track: its old row is gone, and the selection still points at b.
    f.state.selectClip(1, 0);
    f.state.dropClip(1, 0, 0, 6.0, -1, false);
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 1);
    QCOMPARE(f.state.selectedTrack(), 0);
    QCOMPARE(f.state.project()->tracks().at(0).clips.at(f.state.selectedClip()).id, QStringLiteral("b"));

    // One undo brings the row back with b on it — the tidy is part of the move's step.
    f.state.undo();
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 2);
    QCOMPARE(f.clipCount(1), 1);
    // And redo removes it again.
    f.state.redo();
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 1);
    f.state.undo();
    settle();

    // A Ctrl-copy never loses its original's row.
    f.state.selectClip(1, 0);
    f.state.dropClip(1, 0, 0, 6.0, -1, true);
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 2);
    QCOMPARE(f.clipCount(0), 2);
    QCOMPARE(f.clipCount(1), 1);

    // A track the user adds stays empty through unrelated edits until it is used.
    f.state.addTrack(QStringLiteral("video"));
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 3);
    f.state.setTrackMuted(2, true);
    settle();
    QCOMPARE(f.state.project()->tracks().size(), 3);
    QCOMPARE(f.clipCount(0), 0);

    // A project saved with empty rows opens without them.
    {
        drift::Project saved = *f.state.project();
        saved.tracks().append(drift::Track{.type = drift::TrackType::Video});
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(saved.toJson()).toJson());
        file.close();
        f.state.loadProjectJson(QUrl::fromLocalFile(path));
        int empty = 0;
        for (const drift::Track &t : f.state.project()->tracks())
            empty += t.clips.isEmpty() ? 1 : 0;
        QCOMPARE(empty, 0);
    }
}

// VEDIT P40: 필터 — one look per clip, strength scales it, choosing another replaces it, "원본"
// takes it off, and the Adjust sliders never see the filter's own layers.
void EditorStateTest::parity_P40_clipFilters()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, 0);

    const auto filterLayers = [&]() {
        QList<drift::Effect> out;
        for (const drift::Track &track : f.state.project()->tracks())
            for (const drift::Clip &clip : track.clips)
                for (const drift::Effect &e : clip.effects)
                    if (e.parameters.contains(QStringLiteral("veditFilter")))
                        out.append(e);
        return out;
    };
    const auto index = [&]() { return f.indexOf(0, QStringLiteral("v")); };

    f.state.applyClipFilter(0, index(), QStringLiteral("warm"), 1.0);
    QCOMPARE(f.state.clipFilter(0, index()).value(QStringLiteral("id")).toString(), QStringLiteral("warm"));
    QList<drift::Effect> layers = filterLayers();
    QCOMPARE(layers.size(), 2);
    double temperature = 0;
    for (const drift::Effect &e : layers)
        if (e.catalogId == QStringLiteral("adjust.temperature"))
            temperature = e.parameters.value(QStringLiteral("temperature")).toDouble();
    QVERIFY(nearly(temperature, 7000.0));
    QVERIFY(!f.state.clipAdjustValues(0, index()).contains(QStringLiteral("adjust.temperature.temperature")));

    // Half strength sits halfway to neutral.
    f.state.applyClipFilter(0, index(), QStringLiteral("warm"), 0.5);
    for (const drift::Effect &e : filterLayers())
        if (e.catalogId == QStringLiteral("adjust.temperature"))
            QVERIFY(nearly(e.parameters.value(QStringLiteral("temperature")).toDouble(), 6750.0));

    // Another look replaces it.
    f.state.applyClipFilter(0, index(), QStringLiteral("bw"), 1.0);
    layers = filterLayers();
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers.first().catalogId, QStringLiteral("stylize.grayscale"));

    // 원본 takes it off; undo puts black-and-white back.
    f.state.applyClipFilter(0, index(), QStringLiteral("none"), 1.0);
    QVERIFY(filterLayers().isEmpty());
    QCOMPARE(f.state.clipFilter(0, index()).value(QStringLiteral("id")).toString(), QStringLiteral("none"));
    f.state.undo();
    QCOMPARE(f.state.clipFilter(0, index()).value(QStringLiteral("id")).toString(), QStringLiteral("bw"));
}

// VEDIT P42: 트랙 잠금 — a locked track's clips cannot be selected, moved, cut or dropped onto;
// unlocking gives them back. Locking is one undo step.
void EditorStateTest::parity_P42_lockedTracksStayPut()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();

    f.state.setTrackLocked(0, true);
    QVERIFY(f.state.trackIsLocked(0));
    f.state.selectClip(0, 0);
    QCOMPARE(f.state.selectedClip(), -1);

    f.state.moveClip(0, 0, 2.0);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));

    // Dropping b onto the locked row is refused.
    f.state.selectClip(1, 0);
    f.state.dropClip(1, 0, 0, 5.0, -1, false);
    QCOMPARE(f.clipCount(0), 1);
    QCOMPARE(f.clipCount(1), 1);

    // Cutting under the playhead leaves the locked row whole.
    f.state.selectClip(1, 0);
    f.state.setPlayheadSeconds(2.0);
    f.state.splitAtPlayhead();
    QCOMPARE(f.clipCount(0), 1);
    QCOMPARE(f.clipCount(1), 2);

    f.state.setTrackLocked(0, false);
    f.state.selectClip(0, 0);
    QCOMPARE(f.state.selectedTrack(), 0);

    // Ctrl-copying out of a locked track is refused (it used to stack a duplicate in place).
    f.state.setTrackLocked(0, true);
    f.state.dropClip(0, 0, 1, 8.0, -1, true);
    QCOMPARE(f.clipCount(0), 1);
}

// VEDIT P42b: a clip's linked sound on a locked track stays put when the picture moves.
void EditorStateTest::parity_P42b_lockedPartnerStaysPut()
{
    ParityFixture f(2);
    f.state.project()->tracks()[1].type = drift::TrackType::Audio;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video, QStringLiteral("L"));
    f.add(1, QStringLiteral("s"), 0.0, 4.0, drift::ClipType::Audio, QStringLiteral("L"));
    f.ready();
    f.state.setTrackLocked(1, true);
    f.state.selectClip(0, 0);
    QCOMPARE(f.state.selectionCount(), 1);
    f.state.moveClip(0, 0, 3.0);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("v")), 3.0));
    QVERIFY(nearly(f.startOf(1, QStringLiteral("s")), 0.0));
}

// VEDIT P43: 콤보 — "slowly closer" grows the picture 15% about its centre from the first frame
// to the last; "hold still" puts it back. One undo step each.
void EditorStateTest::parity_P43_clipMotionCombos()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("p"), 0.0, 5.0, drift::ClipType::Image);
    f.ready();
    const int W = f.state.project()->width();
    const int H = f.state.project()->height();

    f.state.applyClipMotion(0, 0, QStringLiteral("zoomIn"));
    const drift::Clip *c = f.clip(0, QStringLiteral("p"));
    const drift::TimeUs end = c->timelineDuration;
    QVERIFY(nearly(c->transformW.evaluateAt(0), W));
    QVERIFY(nearly(c->transformW.evaluateAt(end), W * 1.15));
    // Centred: the left edge moves out by half the growth.
    QVERIFY(nearly(c->transformX.evaluateAt(end), -(W * 0.15) / 2.0));
    QVERIFY(nearly(c->transformY.evaluateAt(end), -(H * 0.15) / 2.0));

    f.state.applyClipMotion(0, 0, QStringLiteral("panRight"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(nearly(c->transformX.evaluateAt(0), 0.0));
    QVERIFY(nearly(c->transformX.evaluateAt(end), -(W * 0.15)));

    f.state.applyClipMotion(0, 0, QStringLiteral("none"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(nearly(c->transformW.evaluateAt(end), W));

    f.state.undo();
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(nearly(c->transformX.evaluateAt(end), -(W * 0.15)));
}

// VEDIT P46: 모두에 적용 — one press puts the transition at every cut on the track; one undo
// takes them all off again.
void EditorStateTest::parity_P46_transitionAtEveryCut()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("c"), 6.0, 3.0, drift::ClipType::Image);
    f.ready();
    QCOMPARE(f.state.project()->tracks().at(0).transitions.size(), 0);

    QCOMPARE(f.state.applyTransitionToAllCuts(0, QStringLiteral("crossfade"), 0.5), 2);
    QCOMPARE(f.state.project()->tracks().at(0).transitions.size(), 2);
    for (const drift::Transition &t : f.state.project()->tracks().at(0).transitions)
        QCOMPARE(t.kindId, QStringLiteral("crossfade"));

    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(0).transitions.size(), 0);
}

// VEDIT P47: 사진 길이 한 번에 — every photo takes the length, and what follows on the track moves
// with it (the video after the photos keeps its own length).
void EditorStateTest::parity_P47_allPhotosSameLength()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("v"), 6.0, 4.0, drift::ClipType::Video);
    f.ready();

    QCOMPARE(f.state.setAllPhotoDurations(5.0), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 5.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("v")), 10.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 5.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("v"))->timelineDuration), 4.0));

    f.state.undo();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("v")), 6.0));
}

// VEDIT P48: volume goes up to 400% (조박사님) — enough to lift a quietly recorded voice.
void EditorStateTest::parity_P48_volumeUpTo400()
{
    ParityFixture f(1, drift::TrackType::Audio);
    f.add(0, QStringLiteral("s"), 0.0, 4.0, drift::ClipType::Audio);
    f.ready();
    f.state.mcpSetClipVolume(0, 0, 3.5, false, 0);
    QVERIFY(nearly(f.clip(0, QStringLiteral("s"))->volume.evaluateAt(0), 3.5));
    f.state.mcpSetClipVolume(0, 0, 9.0, false, 0);
    QVERIFY(nearly(f.clip(0, QStringLiteral("s"))->volume.evaluateAt(0), 4.0));
}

// VEDIT P50: two photos that overlap — by dragging the first one's end longer — cross with a curved
// crossfade automatically, even with the overlap switch off.
void EditorStateTest::parity_P50_overlapAlwaysCrossfades()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 4.0, 5.0, drift::ClipType::Image);
    f.ready();
    f.state.setRippleEnabled(false);
    f.state.trimClipRight(0, f.indexOf(0, QStringLiteral("a")), 5.0);
    const auto &tr = f.state.project()->tracks().at(0).transitions;
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 5.0),
             qPrintable(QString::number(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()))));
    QCOMPARE(tr.size(), 1);
    QCOMPARE(tr.at(0).kindId, QStringLiteral("crossfade"));
    QVERIFY(nearly(drift::usToSeconds(tr.at(0).durationUs), 1.0));

    // Overlap switch off: an overlap that exists still gets its crossfade.
    ParityFixture g(1);
    g.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Image);
    g.add(0, QStringLiteral("b"), 4.0, 5.0, drift::ClipType::Image);
    g.ready();
    g.state.setRippleEnabled(false);
    g.state.setAllowClipOverlap(false);
    g.state.trimClipRight(0, g.indexOf(0, QStringLiteral("a")), 5.0);
    if (g.clip(0, QStringLiteral("a"))->timelineEnd() > g.clip(0, QStringLiteral("b"))->timelineStart)
        QCOMPARE(g.state.project()->tracks().at(0).transitions.size(), 1);
}

// VEDIT P51: a photo that is not the canvas shape goes in the middle of the picture, with even
// bars either side — not against the left edge.
void EditorStateTest::parity_P51_photoCentredOnCanvas()
{
    ParityFixture f(1, drift::TrackType::Shape); // photos go on the graphics track
    f.state.project()->setResolution(1920, 1080);
    drift::MediaAsset asset;
    asset.name = QStringLiteral("tall.jpg");
    asset.kind = drift::MediaKind::Image;
    asset.path = QStringLiteral("/nonexistent/tall.jpg");
    asset.width = 1080;
    asset.height = 1920;
    f.state.project()->addAsset(asset);
    f.library.syncToProject();
    f.ready();
    f.state.addClipFromAssetAt(0, 0, 0.0);
    const drift::Clip *found = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &clip : t.clips)
            if (clip.type == drift::ClipType::Image)
                found = &clip;
    QVERIFY2(found, qPrintable(f.state.lastMessage()));
    const drift::Clip &c = *found;
    const double w = c.transformW.evaluateAt(0);
    QVERIFY2(nearly(w, 1080.0 * 1080.0 / 1920.0), qPrintable(QString::number(w)));
    QVERIFY2(nearly(c.transformX.evaluateAt(0), (1920.0 - w) / 2.0),
             qPrintable(QString::number(c.transformX.evaluateAt(0))));
    QVERIFY(nearly(c.transformY.evaluateAt(0), 0.0));
}

// VEDIT P52: a project saved before centring opens with its photos in the middle — but only the
// ones still at the old automatic fit; a photo the user moved keeps its place.
void EditorStateTest::parity_P52_oldPhotosOpenCentred()
{
    ParityFixture f(1, drift::TrackType::Shape);
    drift::Project &p = *f.state.project();
    p.setResolution(1920, 1080);
    drift::MediaAsset asset;
    asset.id = QStringLiteral("tall");
    asset.name = QStringLiteral("tall.jpg");
    asset.kind = drift::MediaKind::Image;
    asset.path = QStringLiteral("/nonexistent/tall.jpg");
    asset.width = 1080;
    asset.height = 1920;
    p.addAsset(asset);
    const double w = 1080.0 * 1080.0 / 1920.0;
    for (int i = 0; i < 2; ++i) {
        drift::Clip c;
        c.id = i == 0 ? QStringLiteral("old") : QStringLiteral("moved");
        c.type = drift::ClipType::Image;
        c.assetId = QStringLiteral("tall");
        c.timelineStart = drift::secondsToUs(5.0 * i);
        c.timelineDuration = drift::secondsToUs(5.0);
        c.srcOut = c.timelineDuration;
        c.transformX.setKeyframe(0, i == 0 ? 0.0 : 300.0);
        c.transformY.setKeyframe(0, 0.0);
        c.transformW.setKeyframe(0, w);
        c.transformH.setKeyframe(0, 1080.0);
        p.tracks()[0].clips.append(c);
    }
    f.ready();
    p.setVeditLayoutVersion(0); // a file from before the tidy-up
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("old.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(p.toJson()).toJson());
    file.close();

    f.state.loadProjectJson(QUrl::fromLocalFile(path));
    const drift::Clip *old = nullptr, *moved = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips) {
            if (c.id == QStringLiteral("old")) old = &c;
            if (c.id == QStringLiteral("moved")) moved = &c;
        }
    QVERIFY(old && moved);
    QVERIFY2(nearly(old->transformX.evaluateAt(0), (1920.0 - w) / 2.0),
             qPrintable(QString::number(old->transformX.evaluateAt(0))));
    QVERIFY(nearly(moved->transformX.evaluateAt(0), 300.0));

    // Saved again (now marked tidied), a photo put flush left on purpose stays there on reopen.
    drift::Project again = *f.state.project();
    for (drift::Track &t : again.tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("old")) {
                c.transformX = {};
                c.transformX.setKeyframe(0, 0.0);
            }
    const QString path2 = dir.filePath(QStringLiteral("again.json"));
    QFile file2(path2);
    QVERIFY(file2.open(QIODevice::WriteOnly));
    file2.write(QJsonDocument(again.toJson()).toJson());
    file2.close();
    f.state.loadProjectJson(QUrl::fromLocalFile(path2));
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("old"))
                QVERIFY(nearly(c.transformX.evaluateAt(0), 0.0));
}

// VEDIT P53: the extra CapCut-style clip animations move the clip while they play and leave it
// exactly in place once settled; the loop combos key the whole clip and "hold still" undoes them.
void EditorStateTest::parity_P53_moreClipAnimations()
{
    using namespace drift;
    for (const char *k : {"swing", "shake", "drop", "zoomSpin", "flyBig", "flipH", "flipV", "jelly",
                          "stretchH", "stretchV", "riseUp", "whirl", "elastic", "diagonal",
                          "whooshLeft", "whooshRight", "blink"}) {
        ClipAnimation in;
        in.kind = clipAnimKindFromString(QString::fromLatin1(k));
        QVERIFY2(in.kind != ClipAnimKind::None, k);
        QCOMPARE(clipAnimKindToString(in.kind), QString::fromLatin1(k));
        in.durationUs = secondsToUs(1.0);
        const ClipAnimSample early = evaluateClipAnimation(0, secondsToUs(5.0), in, ClipAnimation{},
                                                           secondsToUs(0.05), 1920, 1080);
        const bool moving = qAbs(early.dx) > 0.5 || qAbs(early.dy) > 0.5 || qAbs(early.scale - 1.0) > 0.01
                            || qAbs(early.rotationDeg) > 0.1 || early.opacity < 0.99
                            || qAbs(early.scaleX - 1.0) > 0.01 || qAbs(early.scaleY - 1.0) > 0.01
                            || qAbs(early.yawDeg) > 0.1 || qAbs(early.pitchDeg) > 0.1; // VEDIT: 3D
        QVERIFY2(moving, k);
        const ClipAnimSample settled = evaluateClipAnimation(0, secondsToUs(5.0), in, ClipAnimation{},
                                                             secondsToUs(2.0), 1920, 1080);
        QVERIFY2(nearly(settled.dx, 0) && nearly(settled.dy, 0) && nearly(settled.scale, 1)
                     && nearly(settled.rotationDeg, 0) && nearly(settled.opacity, 1)
                     && nearly(settled.scaleX, 1) && nearly(settled.scaleY, 1), k);
    }

    ParityFixture f(1, TrackType::Shape);
    f.add(0, QStringLiteral("p"), 0.0, 4.0, ClipType::Image);
    f.ready();
    f.state.applyClipMotion(0, 0, QStringLiteral("pulse"));
    const Clip *c = f.clip(0, QStringLiteral("p"));
    QVERIFY(c->transformW.keyframes().size() > 8);
    double wmax = 0;
    for (auto it = c->transformW.keyframes().cbegin(); it != c->transformW.keyframes().cend(); ++it)
        wmax = qMax(wmax, c->transformW.evaluateAt(it.key()));
    QVERIFY(wmax > f.state.project()->width() * 1.05);

    f.state.applyClipMotion(0, 0, QStringLiteral("sway"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(c->rotation.keyframes().size() > 8);
    f.state.applyClipMotion(0, 0, QStringLiteral("spinLoop"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(c->rotation.evaluateAt(secondsToUs(4.0)) > 100.0); // half a turn per 4 s
    f.state.applyClipMotion(0, 0, QStringLiteral("jitter"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(c->rotation.isEmpty());
    QVERIFY(c->transformX.keyframes().size() > 30);
    f.state.applyClipMotion(0, 0, QStringLiteral("none"));
    c = f.clip(0, QStringLiteral("p"));
    QVERIFY(c->rotation.isEmpty());
    QVERIFY(nearly(c->transformW.evaluateAt(secondsToUs(1.0)), f.state.project()->width()));
}

// VEDIT P54: a photo and a video share one track (CapCut) — a photo dragged onto the video track
// lands there, and a fresh photo goes on the video track by default.
void EditorStateTest::parity_P54_photosOnVideoTrack()
{
    ParityFixture f(2);
    f.state.project()->tracks()[1].type = drift::TrackType::Shape;
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("p"), 0.0, 3.0, drift::ClipType::Image);
    f.ready();
    QVERIFY(f.state.trackAcceptsKind(0, QStringLiteral("image")));
    f.state.selectClip(1, 0);
    f.state.dropClip(1, 0, 0, 4.0, -1, false);
    QCOMPARE(f.clipCount(0), 2);
    QVERIFY(nearly(f.startOf(0, QStringLiteral("p")), 4.0));
    QCOMPARE(drift::defaultTrackForClipType(*f.state.project(), drift::ClipType::Image), 0);
}

// VEDIT P57: big videos get their light preview copy queued by themselves; small ones and stills
// are left alone.
void EditorStateTest::parity_P57_autoProxyQueuesBigVideos()
{
    struct Scope {
        Scope() { AssetLibrary::s_autoProxyDefault = true; }
        ~Scope() { AssetLibrary::s_autoProxyDefault = false; }
    } scope;
    ParityFixture f(1);
    const auto addAsset = [&](const QString &id, drift::MediaKind kind, int w, int h) {
        drift::MediaAsset a;
        a.id = id;
        a.name = id;
        a.kind = kind;
        a.path = QStringLiteral("/nonexistent/") + id;
        a.width = w;
        a.height = h;
        a.durationUs = drift::secondsToUs(60.0);
        f.state.project()->addAsset(a);
    };
    addAsset(QStringLiteral("big.mp4"), drift::MediaKind::Video, 3840, 2160);
    addAsset(QStringLiteral("small.mp4"), drift::MediaKind::Video, 320, 240);
    addAsset(QStringLiteral("photo.jpg"), drift::MediaKind::Image, 4000, 3000);
    f.library.syncToProject();
    QSettings().setValue(QStringLiteral("preview/autoProxies"), true);
    QCOMPARE(f.library.autoProxyMissing(), 1);
    QVERIFY(f.library.proxyState(QStringLiteral("big.mp4")) != QStringLiteral("none"));
    QCOMPARE(f.library.proxyState(QStringLiteral("small.mp4")), QStringLiteral("none"));
}

// VEDIT P58: 비트 맞추기 — with a beat every half second, each photo runs six beats (3 s) and ends on a
// beat; the video after the photos keeps its length and moves up behind them. One undo step.
void EditorStateTest::parity_P58_photosChangeOnTheBeat()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 5.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 5.0, 5.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("v"), 10.0, 4.0, drift::ClipType::Video);
    f.ready();
    QVariantList beats;
    for (int i = 0; i <= 40; ++i)
        beats.append(0.5 * i);
    QCOMPARE(f.state.fitPhotosToBeatTimes(beats), 2);
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 3.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 3.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("v")), 6.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("v"))->timelineDuration), 4.0));
    f.state.undo();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("v")), 10.0));
}

// VEDIT P59: 캔버스 — the empty space around an off-shape picture can be black, a blurred copy of
// the picture, or white; one undo step each.
void EditorStateTest::parity_P59_canvasFillChoices()
{
    ParityFixture f(1);
    f.ready();
    f.state.setBackground({{QStringLiteral("kind"), QStringLiteral("blur")}, {QStringLiteral("blurStrength"), 40}});
    QCOMPARE(f.state.background().value(QStringLiteral("kind")).toString(), QStringLiteral("blur"));
    f.state.setBackground({{QStringLiteral("kind"), QStringLiteral("color")}, {QStringLiteral("color"), QStringLiteral("#ffffffff")}});
    QCOMPARE(f.state.background().value(QStringLiteral("color")).toString(), QStringLiteral("#ffffffff"));
    f.state.undo();
    QCOMPARE(f.state.background().value(QStringLiteral("kind")).toString(), QStringLiteral("blur"));
}

// VEDIT P65: 모두에 적용 for text clips — the look goes to the other text clips (not captions),
// each keeps its own words; one undo step.
void EditorStateTest::parity_P65_textStyleToAllText()
{
    ParityFixture f(1, drift::TrackType::Text);
    for (int i = 0; i < 3; ++i) {
        f.add(0, QStringLiteral("t%1").arg(i), 3.0 * i, 3.0, drift::ClipType::Text);
        f.state.project()->tracks()[0].clips[i].textContent = QStringLiteral("word %1").arg(i);
    }
    f.state.project()->tracks()[0].clips[0].textStyle.fontFamily = QStringLiteral("Jua");
    f.state.project()->tracks()[0].clips[0].textStyle.pixelSize = 99;
    f.ready();
    QCOMPARE(f.state.applyTextStyleToCaptions(0, 0, QStringLiteral("track")), 2);
    for (int i = 1; i < 3; ++i) {
        const drift::Clip &c = f.state.project()->tracks().at(0).clips.at(i);
        QCOMPARE(c.textStyle.fontFamily, QStringLiteral("Jua"));
        QCOMPARE(c.textContent, QStringLiteral("word %1").arg(i));
    }
    f.state.undo();
    QVERIFY(f.state.project()->tracks().at(0).clips.at(1).textStyle.fontFamily != QStringLiteral("Jua"));
}

// VEDIT P67: 화면 속 화면 — "bottom right" puts the picture at a third of the screen in that corner
// with a small margin; "full" fits it back to the screen.
void EditorStateTest::parity_P67_pictureInPicture()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    const double W = f.state.project()->width(), H = f.state.project()->height();
    f.state.placeClipOnScreen(0, 0, QStringLiteral("bottomRight"));
    const drift::Clip *c = f.clip(0, QStringLiteral("v"));
    const double w = c->transformW.evaluateAt(0), h = c->transformH.evaluateAt(0);
    QVERIFY(nearly(w, W / 3.0) && nearly(h, H / 3.0));
    const double margin = 0.04 * qMin(W, H);
    QVERIFY(nearly(c->transformX.evaluateAt(0), W - w - margin));
    QVERIFY(nearly(c->transformY.evaluateAt(0), H - h - margin));
    f.state.placeClipOnScreen(0, 0, QStringLiteral("full"));
    c = f.clip(0, QStringLiteral("v"));
    QVERIFY(nearly(c->transformW.evaluateAt(0), W) && nearly(c->transformX.evaluateAt(0), 0.0));
    f.state.undo();
    QVERIFY(nearly(f.clip(0, QStringLiteral("v"))->transformW.evaluateAt(0), W / 3.0));
}

// VEDIT P68: 사진 슬라이드쇼 — three photos (and a video, which is left out) become 3 s photos on one
// track overlapping half a second (so the cuts are dissolves), with alternating motion; one undo.
void EditorStateTest::parity_P68_photoSlideshow()
{
    ParityFixture f(1);
    QStringList ids;
    for (int i = 0; i < 3; ++i) {
        drift::MediaAsset a;
        a.id = QStringLiteral("p%1").arg(i);
        a.name = a.id + QStringLiteral(".jpg");
        a.kind = drift::MediaKind::Image;
        a.path = QStringLiteral("/nonexistent/") + a.name;
        a.width = 1600;
        a.height = 1200;
        f.state.project()->addAsset(a);
        ids << a.id;
    }
    drift::MediaAsset v;
    v.id = QStringLiteral("vid");
    v.name = QStringLiteral("vid.mp4");
    v.kind = drift::MediaKind::Video;
    v.path = QStringLiteral("/nonexistent/vid.mp4");
    v.durationUs = drift::secondsToUs(4.0);
    f.state.project()->addAsset(v);
    ids << v.id;
    f.library.syncToProject();
    f.ready();

    QCOMPARE(f.state.makeSlideshow(ids, 3.0), 3);
    QList<const drift::Clip *> photos;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Image)
                photos.append(&c);
    QCOMPARE(photos.size(), 3);
    std::sort(photos.begin(), photos.end(), [](const drift::Clip *a, const drift::Clip *b) {
        return a->timelineStart < b->timelineStart;
    });
    QVERIFY(nearly(drift::usToSeconds(photos[1]->timelineStart - photos[0]->timelineStart), 2.5));
    QVERIFY(nearly(drift::usToSeconds(photos[0]->timelineDuration), 3.0));
    QCOMPARE(photos[0]->motionPreset, QStringLiteral("zoomIn"));
    QCOMPARE(photos[1]->motionPreset, QStringLiteral("zoomOut"));
    int dissolves = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        dissolves += t.transitions.size();
    QCOMPARE(dissolves, 2);
    f.state.undo();
    int left = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        left += t.clips.size();
    QCOMPARE(left, 0);
}

// VEDIT P69: 음악을 영상 길이에 맞추기 — 20 s of music under 8 s of pictures ends at 8 s and fades out
// over the last 2 s; one undo puts it back.
void EditorStateTest::parity_P69_fitMusicToVideo()
{
    ParityFixture f(2);
    f.state.project()->tracks()[1].type = drift::TrackType::Audio;
    f.add(0, QStringLiteral("p"), 0.0, 8.0, drift::ClipType::Image);
    f.add(1, QStringLiteral("m"), 0.0, 20.0, drift::ClipType::Audio);
    f.ready();
    QVERIFY(f.state.fitMusicToVideo(1, 0));
    const drift::Clip *m = f.clip(1, QStringLiteral("m"));
    QVERIFY(nearly(drift::usToSeconds(m->timelineEnd()), 8.0));
    QVERIFY(nearly(drift::usToSeconds(m->fadeOutUs), 2.0));
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(f.clip(1, QStringLiteral("m"))->timelineEnd()), 20.0));
}

// VEDIT P70: 초록 배경 지우기 — keying a colour adds the key to the clip with that colour's hue; a
// grey is refused; picking again retunes the same key.
void EditorStateTest::parity_P70_chromaKeyFromColour()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    QVERIFY(f.state.applyChromaKeyColor(0, 0, QStringLiteral("#ff00ff00")));
    const auto hue = [&]() {
        for (const drift::Track &t : f.state.project()->tracks())
            for (const drift::Clip &c : t.clips)
                for (const drift::Effect &e : c.effects)
                    if (e.catalogId == QStringLiteral("key.chroma"))
                        return e.parameters.value(QStringLiteral("u_keyHue")).toDouble();
        return -1.0;
    };
    QVERIFY(nearly(hue(), 120.0));
    QVERIFY(!f.state.applyChromaKeyColor(0, 0, QStringLiteral("#ff808080")));
    QVERIFY(f.state.applyChromaKeyColor(0, f.indexOf(0, QStringLiteral("v")), QStringLiteral("#ff0000ff")));
    QVERIFY(nearly(hue(), 240.0));
}

// VEDIT P71: 큰 한글 자막 — a new caption starts in Pretendard, bold, at least 52px, on a box.
void EditorStateTest::parity_P71_captionsStartBigAndKorean()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    const drift::Clip *caption = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Subtitle)
                caption = &c;
    QVERIFY(caption);
    QCOMPARE(caption->textStyle.fontFamily, QStringLiteral("Pretendard"));
    QVERIFY(caption->textStyle.pixelSize >= 52);
    QVERIFY(caption->textStyle.fontWeight >= 700);
    QVERIFY(caption->textStyle.boxEnabled);
}

// VEDIT P72: 조정 노출·밝은 부분·어두운 부분·가장자리 어둡게 — the adjust.tone package is in the
// catalog with its four neutral-at-zero controls, and a slider writes into one effect on the clip.
void EditorStateTest::parity_P72_lightAndShadowAdjust()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5);
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.tone"), QStringLiteral("vignette"), 0.7);
    const QVariantMap values = f.state.clipAdjustValues(0, 0);
    QVERIFY(nearly(values.value(QStringLiteral("adjust.tone.exposure")).toDouble(), 0.5));
    QVERIFY(nearly(values.value(QStringLiteral("adjust.tone.vignette")).toDouble(), 0.7));
    QVERIFY(nearly(values.value(QStringLiteral("adjust.tone.shadows"), -9.0).toDouble(), 0.0));
    int tones = 0;
    for (const drift::Track &t : f.state.project()->tracks()) // the clip or its effect host
        for (const drift::Clip &c : t.clips)
            for (const drift::Effect &e : c.effects)
                tones += e.catalogId == QStringLiteral("adjust.tone");
    QCOMPARE(tones, 1);
}

// VEDIT P73: 글자 읽어주기 — the Windows voice makes a WAV of the words. Skipped where no voice is
// installed (CI); the timeline placement itself is placeRecordedAudio, covered with the recorder.
void EditorStateTest::parity_P73_readTextAloud()
{
    if (!drift::speechSynthesisAvailable())
        QSKIP("no speech engine on this machine");
    QTemporaryDir dir;
    const QString wav = dir.filePath(QStringLiteral("hello.wav"));
    QString error;
    if (!drift::synthesizeSpeechToWav(QStringLiteral("안녕하세요. 쉽게 자르고, 쉽게 간다."), wav, &error)
        && error == QStringLiteral("no Korean voice"))
        QSKIP("no Korean voice installed");
    QVERIFY2(QFileInfo(wav).size() > 1000, qPrintable(error));
    QFile f(wav);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(4), QByteArray("RIFF"));
    QVERIFY(!drift::synthesizeSpeechToWav(QStringLiteral("   "), dir.filePath(QStringLiteral("x.wav"))));
}

// A silent 16-bit mono WAV of `seconds`.
static void writeSilentWav(const QString &path, double seconds)
{
    const quint32 rate = 22050;
    const quint32 bytes = quint32(seconds * rate) * 2;
    QByteArray h;
    QDataStream ds(&h, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData("RIFF", 4);
    ds << quint32(36 + bytes);
    ds.writeRawData("WAVEfmt ", 8);
    ds << quint32(16) << quint16(1) << quint16(1) << rate << quint32(rate * 2) << quint16(2) << quint16(16);
    ds.writeRawData("data", 4);
    ds << bytes;
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(h);
    f.write(QByteArray(int(bytes), '\0'));
}

// VEDIT P74: a recording (or read-aloud voice) is placed as long as the sound is — it used to be
// placed before its length was read and came out as a 5 s clip.
void EditorStateTest::parity_P74_placedSoundKeepsItsLength()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.ready();
    QTemporaryDir dir;
    const QString wav = dir.filePath(QStringLiteral("long.wav"));
    writeSilentWav(wav, 8.0);
    QVERIFY(f.state.placeRecordedAudio(wav, 1.0));
    const drift::Clip *sound = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Audio)
                sound = &c;
    QVERIFY(sound);
    QVERIFY2(qAbs(drift::usToSeconds(sound->timelineDuration) - 8.0) < 0.1,
             qPrintable(QString::number(drift::usToSeconds(sound->timelineDuration))));
    QVERIFY(nearly(drift::usToSeconds(sound->timelineStart), 1.0));
}

// VEDIT P74: 자막 읽어주기 — each caption line is spoken onto one new sound track, in order and
// without overlapping. Needs the Korean Windows voice; skipped elsewhere.
void EditorStateTest::parity_P74_readCaptionsAloud()
{
    if (!drift::speechSynthesisAvailable())
        QSKIP("no speech engine on this machine");
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    int st = -1, sc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Subtitle) {
                st = t;
                sc = c;
            }
    QVERIFY(st >= 0);
    f.state.project()->tracks()[st].clips[sc].subtitleCues = {
        drift::SubtitleCue{0, drift::secondsToUs(1.0), QStringLiteral("안녕하세요")},
        drift::SubtitleCue{drift::secondsToUs(0.5), drift::secondsToUs(2.0), QStringLiteral("반갑습니다")},
        drift::SubtitleCue{drift::secondsToUs(6.0), drift::secondsToUs(8.0), QStringLiteral("  ")}};
    {
        QTemporaryDir probe;
        QString error;
        if (!drift::synthesizeSpeechToWav(QStringLiteral("안녕"), probe.filePath(QStringLiteral("p.wav")), &error)
            && error == QStringLiteral("no Korean voice"))
            QSKIP("no Korean voice installed");
    }
    const int tracksBefore = f.state.project()->tracks().size();
    const int read = f.state.readCaptionsAloud(st, sc);
    QCOMPARE(read, 2);
    QList<const drift::Clip *> sounds;
    int soundTracks = 0;
    for (const drift::Track &t : f.state.project()->tracks()) {
        bool any = false;
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Audio) {
                sounds.append(&c);
                any = true;
            }
        soundTracks += any;
    }
    QCOMPARE(sounds.size(), 2);
    QCOMPARE(soundTracks, 1);
    QVERIFY(sounds.at(0)->timelineDuration > drift::secondsToUs(0.3));
    const drift::Clip *first = sounds.at(0)->timelineStart < sounds.at(1)->timelineStart ? sounds.at(0) : sounds.at(1);
    const drift::Clip *second = first == sounds.at(0) ? sounds.at(1) : sounds.at(0);
    QVERIFY(second->timelineStart >= first->timelineEnd()); // the long first line pushed it later
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().size(), tracksBefore);
}

// VEDIT P75: 화면 캡처 — one press writes the frame at the playhead as a project-sized JPEG.
void EditorStateTest::parity_P75_saveFrameAsPhoto()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("t"), 0.0, 4.0, drift::ClipType::Text);
    f.ready();
    QTemporaryDir dir;
    AppController::s_framePhotoDirOverride = dir.path();
    f.state.setPlayheadUs(drift::secondsToUs(1.0));
    const QString path = f.state.saveFrameAsPhoto();
    AppController::s_framePhotoDirOverride.clear();
    QVERIFY(!path.isEmpty());
    QVERIFY(path.endsWith(QStringLiteral(".jpg")));
    const QImage saved(path);
    QVERIFY(!saved.isNull());
    QCOMPARE(saved.size(), QSize(f.state.project()->width(), f.state.project()->height()));
}

// VEDIT P76: 자동 보정 — a dark shot is brightened, a bright one toned down, a flat grey one gets
// contrast, a good picture is left alone and a black-and-white one keeps its (lack of) colour.
void EditorStateTest::parity_P76_autoAdjustChoosesSensibleFixes()
{
    const auto fixes = [](const QImage &img) { return AppController::autoAdjustValuesFor(img); };
    QImage dark(160, 90, QImage::Format_RGB32);
    dark.fill(QColor(30, 26, 22));
    QVERIFY(fixes(dark).value(QStringLiteral("exposure")).toDouble() > 0.8);

    QImage bright(160, 90, QImage::Format_RGB32);
    bright.fill(QColor(235, 235, 230));
    QVERIFY(fixes(bright).value(QStringLiteral("exposure")).toDouble() < -0.3);

    QImage flat(160, 90, QImage::Format_RGB32);
    flat.fill(QColor(118, 118, 118));
    const QVariantMap f = fixes(flat);
    QVERIFY(f.value(QStringLiteral("contrast")).toDouble() > 1.2);
    QCOMPARE(f.value(QStringLiteral("saturation")).toDouble(), 1.0); // grey stays grey

    // A well-exposed, contrasty, colourful picture: stripes of strong colours around mid grey.
    QImage good(160, 90, QImage::Format_RGB32);
    const QColor stripes[] = {QColor(230, 60, 40), QColor(30, 30, 30), QColor(60, 200, 90),
                              QColor(240, 240, 240), QColor(40, 90, 220), QColor(120, 120, 120)};
    for (int x = 0; x < good.width(); ++x)
        for (int y = 0; y < good.height(); ++y)
            good.setPixelColor(x, y, stripes[(x / 16) % 6]);
    const QVariantMap g = fixes(good);
    QCOMPARE(g.value(QStringLiteral("contrast")).toDouble(), 1.0);
    QCOMPARE(g.value(QStringLiteral("saturation")).toDouble(), 1.0);
    QVERIFY(std::abs(g.value(QStringLiteral("exposure")).toDouble()) < 0.4);
}

// VEDIT P77: 자동 보정 end to end — a dark photo letterboxed in a wide frame is brightened (the
// side bars are not counted as dark picture), it is one undo step, and [처음대로] removes the
// adjustments again rather than leaving neutral effects behind.
void EditorStateTest::parity_P77_autoAdjustAndResetOnAClip()
{
    QTemporaryDir dir;
    const QString png = dir.filePath(QStringLiteral("dark.png"));
    QImage dark(540, 960, QImage::Format_RGB32); // portrait in a landscape project
    dark.fill(QColor(70, 66, 60));
    QVERIFY(dark.save(png));
    ParityFixture f(1);
    f.add(0, QStringLiteral("p"), 0.0, 4.0, drift::ClipType::Image);
    f.state.project()->tracks()[0].clips[0].path = png;
    f.ready();
    const auto toneEffects = [&]() {
        int n = 0;
        for (const drift::Track &t : f.state.project()->tracks())
            for (const drift::Clip &c : t.clips)
                for (const drift::Effect &e : c.effects)
                    n += e.catalogId.startsWith(QStringLiteral("adjust."));
        return n;
    };
    QCOMPARE(toneEffects(), 0);
    QVERIFY(f.state.autoAdjustClip(0, f.indexOf(0, QStringLiteral("p"))));
    const QVariantMap v = f.state.clipAdjustValues(0, f.indexOf(0, QStringLiteral("p")));
    const double exposure = v.value(QStringLiteral("adjust.tone.exposure")).toDouble();
    // 70/255 grey wants about +0.7 stops; counting the black side bars would push it to the cap.
    QVERIFY2(exposure > 0.4 && exposure < 1.1, qPrintable(QString::number(exposure)));
    f.state.undo();
    QCOMPARE(toneEffects(), 0);
    f.state.redo();
    QVERIFY(toneEffects() > 0);
    QVERIFY(f.state.resetClipAdjust(0, f.indexOf(0, QStringLiteral("p"))));
    QCOMPARE(toneEffects(), 0);
    QVERIFY(!f.state.resetClipAdjust(0, f.indexOf(0, QStringLiteral("p"))));
}

// VEDIT P78: 사람만 남기기 — only a video clip with a source can start it; a photo or a title gets
// a message and nothing starts. (The cut-out itself needs the model and real video: manual check.)
void EditorStateTest::parity_P78_keepOnlyPeopleRefusesNonVideo()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("p"), 0.0, 4.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("t"), 4.0, 2.0, drift::ClipType::Text);
    f.ready();
    QVERIFY(!f.state.keepOnlyPeople(0, f.indexOf(0, QStringLiteral("p"))));
    QVERIFY(!f.state.segmenting());
    QVERIFY(!f.state.keepOnlyPeople(0, f.indexOf(0, QStringLiteral("t"))));
    QVERIFY(!f.state.segmenting());
}

// VEDIT P79: a sound from the 효과음 / 음악 tab is placed as long as it is — a short click stays
// short and a long piece is not cut to the 5 s stand-in.
void EditorStateTest::parity_P79_libraryAudioKeepsItsLength()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.ready();
    QTemporaryDir dir;
    const QString shortWav = dir.filePath(QStringLiteral("click.wav"));
    const QString longWav = dir.filePath(QStringLiteral("song.wav"));
    writeSilentWav(shortWav, 0.4);
    writeSilentWav(longWav, 12.0);
    QVERIFY(f.state.addBundledAudio(shortWav));
    QVERIFY(f.state.addBundledAudio(longWav));
    QList<double> lengths;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Audio)
                lengths.append(drift::usToSeconds(c.timelineDuration));
    std::sort(lengths.begin(), lengths.end());
    QCOMPARE(lengths.size(), 2);
    QVERIFY2(qAbs(lengths.at(0) - 0.4) < 0.05, qPrintable(QString::number(lengths.at(0))));
    QVERIFY2(qAbs(lengths.at(1) - 12.0) < 0.1, qPrintable(QString::number(lengths.at(1))));
}

// VEDIT P80: 바꾸기 — a photo clip swapped for another photo keeps its time, length, effects and
// motion; undo brings the old picture back; a title can't be replaced.
void EditorStateTest::parity_P80_replaceClipKeepsItsPlaceAndLooks()
{
    QTemporaryDir dir;
    const QString a = dir.filePath(QStringLiteral("a.png"));
    const QString b = dir.filePath(QStringLiteral("b.png"));
    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(a));
    QImage tall(180, 320, QImage::Format_RGB32);
    tall.fill(Qt::blue);
    QVERIFY(tall.save(b));
    ParityFixture f(1);
    f.add(0, QStringLiteral("p"), 2.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("t"), 5.0, 2.0, drift::ClipType::Text);
    f.state.project()->tracks()[0].clips[0].path = a;
    f.ready();
    const int pi = f.indexOf(0, QStringLiteral("p"));
    f.state.setClipAdjust(0, pi, QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5);
    f.state.applyClipMotion(0, f.indexOf(0, QStringLiteral("p")), QStringLiteral("zoomIn"));
    QVERIFY(f.state.replaceClipMedia(0, f.indexOf(0, QStringLiteral("p")), QUrl::fromLocalFile(b)));
    const drift::Clip *clip = f.clip(0, QStringLiteral("p"));
    QVERIFY(clip);
    QCOMPARE(QFileInfo(clip->path).fileName(), QStringLiteral("b.png"));
    QVERIFY(nearly(drift::usToSeconds(clip->timelineStart), 2.0));
    QVERIFY(nearly(drift::usToSeconds(clip->timelineDuration), 3.0));
    QCOMPARE(clip->motionPreset, QStringLiteral("zoomIn"));
    QVERIFY(nearly(f.state.clipAdjustValues(0, f.indexOf(0, QStringLiteral("p")))
                       .value(QStringLiteral("adjust.tone.exposure")).toDouble(), 0.5));
    f.state.undo();
    QCOMPARE(QFileInfo(f.clip(0, QStringLiteral("p"))->path).fileName(), QStringLiteral("a.png"));
    QVERIFY(!f.state.replaceClipMedia(0, f.indexOf(0, QStringLiteral("t")), QUrl::fromLocalFile(b)));
}

// VEDIT P81: 장면마다 자르기 — shots found at 3 s and 7 s of a 10 s clip make three clips, in one
// undo step; the first scene (at 0) is not a cut.
void EditorStateTest::parity_P81_cutAtEveryScene()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    f.state.m_sceneClipId = QStringLiteral("v");
    f.state.m_scenes = {QVariantMap{{QStringLiteral("sourceStart"), 0.0}},
                        QVariantMap{{QStringLiteral("sourceStart"), 3.0}},
                        QVariantMap{{QStringLiteral("sourceStart"), 7.0}}};
    QCOMPARE(f.state.splitAtScenes(), 2);
    QVERIFY(f.state.scenes().isEmpty()); // the old list described the uncut clip
    QList<double> starts;
    for (const drift::Clip &c : f.state.project()->tracks().at(0).clips)
        starts.append(drift::usToSeconds(c.timelineStart));
    std::sort(starts.begin(), starts.end());
    QCOMPARE(starts.size(), 3);
    QVERIFY(nearly(starts.at(1), 3.0));
    QVERIFY(nearly(starts.at(2), 7.0));
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(0).clips.size(), 1);
}

// VEDIT P82: 긴 자막 나누기 — a long line is broken into lines of at most 24 characters that keep
// every word, in order, and share the original time with no gap; short lines are untouched.
void EditorStateTest::parity_P82_longCaptionsBecomeShortLines()
{
    const QString longLine = QStringLiteral("오늘은 손주들과 함께 바닷가에 갔습니다, 날씨가 정말 좋아서 하루 종일 즐겁게 놀았어요");
    const QList<drift::SubtitleCue> cues{
        drift::SubtitleCue{0, drift::secondsToUs(6.0), longLine},
        drift::SubtitleCue{drift::secondsToUs(6.0), drift::secondsToUs(7.0), QStringLiteral("짧은 줄")}};
    const QList<drift::SubtitleCue> out = AppController::splitLongCues(cues, 24);
    QVERIFY(out.size() >= 4);
    QStringList words;
    for (int i = 0; i < out.size() - 1; ++i) {
        QVERIFY2(out.at(i).text.size() <= 24, qPrintable(out.at(i).text));
        QCOMPARE(out.at(i).endUs, out.at(i + 1).startUs);
        words.append(out.at(i).text);
    }
    QCOMPARE(words.join(QLatin1Char(' ')), longLine);
    QCOMPARE(out.first().startUs, drift::TimeUs{0});
    QCOMPARE(out.last().text, QStringLiteral("짧은 줄"));
    QCOMPARE(out.at(out.size() - 2).endUs, drift::secondsToUs(6.0));

    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    int st = -1, sc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Subtitle) {
                st = t;
                sc = c;
            }
    QVERIFY(st >= 0);
    f.state.project()->tracks()[st].clips[sc].subtitleCues = cues;
    QVERIFY(f.state.splitLongCaptions(st, sc) >= 2);
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(st).clips.at(sc).subtitleCues.size(), 2);
    QCOMPARE(f.state.splitLongCaptions(st, sc, 200), 0);
}

// VEDIT P83: 소리 탭 페이드 chips — what the chips call: fade in 2 s then fade out 3 s on a music
// clip, each press keeping the other end, read back through clipAt as the chips do.
void EditorStateTest::parity_P83_soundFadeChips()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("m"), 0.0, 20.0, drift::ClipType::Audio);
    f.ready();
    const int t = 1;
    f.state.setClipFade(t, f.indexOf(t, QStringLiteral("m")), 2.0, 0.0);
    QVariantMap data = f.state.clipAt(t, f.indexOf(t, QStringLiteral("m")));
    f.state.setClipFade(t, f.indexOf(t, QStringLiteral("m")), data.value(QStringLiteral("fadeIn")).toDouble(), 3.0);
    data = f.state.clipAt(t, f.indexOf(t, QStringLiteral("m")));
    QVERIFY(nearly(data.value(QStringLiteral("fadeIn")).toDouble(), 2.0));
    QVERIFY(nearly(data.value(QStringLiteral("fadeOut")).toDouble(), 3.0));
    f.state.undo();
    data = f.state.clipAt(t, f.indexOf(t, QStringLiteral("m")));
    QVERIFY(nearly(data.value(QStringLiteral("fadeOut")).toDouble(), 0.0));
    QVERIFY(nearly(data.value(QStringLiteral("fadeIn")).toDouble(), 2.0));
}

// VEDIT P84: a slideshow of 120 photos is laid out correctly and in reasonable time (each photo
// used to refresh the whole timeline as it was placed).
void EditorStateTest::parity_P84_bigSlideshowIsQuick()
{
    ParityFixture f(1);
    QStringList ids;
    for (int i = 0; i < 120; ++i) {
        drift::MediaAsset a;
        a.id = QStringLiteral("p%1").arg(i);
        a.name = a.id + QStringLiteral(".jpg");
        a.kind = drift::MediaKind::Image;
        a.path = QStringLiteral("/nonexistent/") + a.name;
        a.width = 1600;
        a.height = 1200;
        f.state.project()->addAsset(a);
        ids << a.id;
    }
    f.library.syncToProject();
    f.ready();
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(f.state.makeSlideshow(ids, 3.0), 120);
    const qint64 ms = timer.elapsed();
    qInfo() << "VEDIT slideshow of 120 photos took" << ms << "ms";
    int moving = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            moving += c.type == drift::ClipType::Image && !c.motionPreset.isEmpty();
    QCOMPARE(moving, 120);
    f.state.undo();
    int left = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        left += int(t.clips.size());
    QCOMPARE(left, 0);
    QVERIFY2(ms < 30000, qPrintable(QString::number(ms)));
}

// VEDIT P85: 자주 쓰는 글자 — every template names a style that exists, and one press puts the words
// on the timeline in that style.
void EditorStateTest::parity_P85_readyMadeWords()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    const QVariantList templates = f.state.quickTextTemplates();
    QVERIFY(templates.size() >= 6);
    for (const QVariant &t : templates)
        QVERIFY2(drift::textStyleForPresetId(t.toMap().value(QStringLiteral("preset")).toString()).has_value(),
                 qPrintable(t.toMap().value(QStringLiteral("preset")).toString()));
    QVERIFY(f.state.addQuickText(QStringLiteral("thanks")));
    const drift::Clip *text = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Text)
                text = &c;
    QVERIFY(text);
    QCOMPARE(text->textContent, QStringLiteral("Thank you")); // tests run untranslated
    QCOMPARE(text->textStyle.packId, QStringLiteral("ko-greeting"));
    QVERIFY(!f.state.addQuickText(QStringLiteral("nope")));
}

// VEDIT P86: 소리만 넣기 — a video with sound gives one sound clip, as long as the video, at the
// playhead and no picture; a silent video is refused; one undo takes it away.
void EditorStateTest::parity_P86_addSoundFromVideo()
{
    ParityFixture f(1);
    drift::MediaAsset v;
    v.id = QStringLiteral("song");
    v.name = QStringLiteral("song.mp4");
    v.kind = drift::MediaKind::Video;
    v.path = QStringLiteral("/nonexistent/song.mp4");
    v.durationUs = drift::secondsToUs(6.0);
    v.width = 1920;
    v.height = 1080;
    v.hasAudio = true;
    v.hasAudioKnown = true;
    f.state.project()->addAsset(v);
    drift::MediaAsset quiet = v;
    quiet.id = QStringLiteral("quiet");
    quiet.path = QStringLiteral("/nonexistent/quiet.mp4");
    quiet.hasAudio = false;
    f.state.project()->addAsset(quiet);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video); // a timeline to stand on
    f.library.syncToProject();
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(2.0));
    QVERIFY(f.state.addSoundFromVideo(QStringLiteral("song")));
    int sounds = 0, pictures = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips) {
            if (c.type == drift::ClipType::Audio) {
                ++sounds;
                QVERIFY2(nearly(drift::usToSeconds(c.timelineStart), 2.0),
                         qPrintable(QString::number(drift::usToSeconds(c.timelineStart))));
                QVERIFY(nearly(drift::usToSeconds(c.timelineDuration), 6.0));
                QCOMPARE(c.assetId, QStringLiteral("song"));
            } else if (c.type == drift::ClipType::Video) {
                ++pictures;
            }
        }
    QCOMPARE(sounds, 1);
    QCOMPARE(pictures, 1); // only the clip that was already there
    QVERIFY(!f.state.addSoundFromVideo(QStringLiteral("quiet")));
    f.state.undo();
    int left = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        left += int(t.clips.size());
    QCOMPARE(left, 1);
}

// VEDIT P87: 목소리 또렷하게 — adds the voice EQ (mid +3, high +4, low -3) and a compressor in one
// undo step; pressing again does not stack a second chain.
void EditorStateTest::parity_P87_enhanceVoice()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("a"), 0.0, 10.0, drift::ClipType::Audio);
    f.ready();
    const auto chain = [&]() {
        QStringList ids;
        double mid = 0.0;
        for (const drift::Track &t : f.state.project()->tracks())
            for (const drift::Clip &c : t.clips)
                for (const drift::Effect &e : c.audioEffects) {
                    ids << e.catalogId;
                    if (e.catalogId == QStringLiteral("utility.eq"))
                        mid = e.parameters.value(QStringLiteral("mid")).toDouble();
                }
        return qMakePair(ids, mid);
    };
    QVERIFY(f.state.enhanceVoice(1, f.indexOf(1, QStringLiteral("a"))));
    const auto after = chain();
    QCOMPARE(after.first, QStringList({QStringLiteral("utility.eq"), QStringLiteral("utility.compressor")}));
    QVERIFY(nearly(after.second, 3.0));
    QVERIFY(!f.state.enhanceVoice(1, f.indexOf(1, QStringLiteral("a"))));
    QCOMPARE(chain().first.size(), 2);
    f.state.undo();
    QVERIFY(chain().first.isEmpty());
}

// VEDIT P89: every Korean text style finds its bundled font — by the picker's Korean name — and a
// family is also found by the name inside the font file, ignoring case and spaces.
void EditorStateTest::parity_P89_koreanStylesFindTheirFonts()
{
    QString fonts;
    for (QDir dir(QCoreApplication::applicationDirPath()); ; ) {
        if (dir.exists(QStringLiteral("bundled-addons/fonts.korean/fonts"))) {
            fonts = dir.filePath(QStringLiteral("bundled-addons/fonts.korean/fonts"));
            break;
        }
        if (!dir.cdUp())
            break;
    }
    if (fonts.isEmpty())
        QSKIP("Korean font bundle not present");
    ::reloadFontCatalog({fonts});
    QStringList missing;
    for (const QString &id : {QStringLiteral("ko-title"), QStringLiteral("ko-subtitle"), QStringLiteral("ko-news"),
                              QStringLiteral("ko-handwriting"), QStringLiteral("ko-brush"), QStringLiteral("ko-cute"),
                              QStringLiteral("ko-elegant"), QStringLiteral("ko-poster"), QStringLiteral("ko-greeting"),
                              QStringLiteral("ko-celebrate")}) {
        const std::optional<drift::TextStyle> style = drift::textStyleForPresetId(id);
        if (!style || !::fontFamilyForName(style->fontFamily))
            missing << id + QLatin1Char('=') + (style ? style->fontFamily : QString());
    }
    const ::FontFamilyEntry *byKorean = ::fontFamilyForName(QStringLiteral("검은고딕"));
    const ::FontFamilyEntry *byFile = ::fontFamilyForName(QStringLiteral("black han sans"));
    const ::FontFamilyEntry *spaced = ::fontFamilyForName(QStringLiteral("Nanum Gothic"));
    ::reloadFontCatalog(); // back to the default roots for the other tests
    QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));
    QVERIFY(byKorean);
    QCOMPARE(byFile, byKorean);
    QVERIFY(spaced);
}

// VEDIT P90: 길이 버튼 — what the 2/3/5/10 s chips call: a title and a photo take the pressed
// length (read back as the chips read it), and the clip after a photo follows (ripple).
void EditorStateTest::parity_P90_lengthChipsWithoutDragging()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("p"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("q"), 3.0, 3.0, drift::ClipType::Image);
    f.ready();
    f.state.setClipDuration(0, f.indexOf(0, QStringLiteral("p")), 5.0);
    QVERIFY(nearly(f.state.clipAt(0, f.indexOf(0, QStringLiteral("p"))).value(QStringLiteral("duration")).toDouble(), 5.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("q"))->timelineStart), 5.0));
    f.state.setClipDuration(0, f.indexOf(0, QStringLiteral("p")), 2.0); // shorter: no gap left
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("q"))->timelineStart), 2.0));
    {   // a dissolve partner keeps its overlap but never slides over the shortened clip
        ParityFixture g(1);
        g.add(0, QStringLiteral("x"), 0.0, 10.0, drift::ClipType::Image);
        g.add(0, QStringLiteral("y"), 8.0, 4.0, drift::ClipType::Image);
        g.ready();
        g.state.setClipDuration(0, g.indexOf(0, QStringLiteral("x")), 1.0);
        const double y = drift::usToSeconds(g.clip(0, QStringLiteral("y"))->timelineStart);
        QVERIFY2(y >= 0.0 - 1e-6 && y < 1.0 && nearly(drift::usToSeconds(g.clip(0, QStringLiteral("x"))->timelineDuration), 1.0),
                 qPrintable(QString::number(y)));
        QVERIFY2(y > 0.0 + 1e-6, qPrintable(QString::number(y))); // starts after x does, not on top
    }
    f.state.addTextClip(QStringLiteral("제목"), 0.0, {});
    int tt = -1, tc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Text) {
                tt = t;
                tc = c;
            }
    QVERIFY(tt >= 0);
    f.state.setClipDuration(tt, tc, 10.0);
    QVERIFY(nearly(f.state.clipAt(tt, tc).value(QStringLiteral("duration")).toDouble(), 10.0));
}

// VEDIT P91: a speed change that shortens or lengthens a clip moves what follows (빈틈 메움), as a
// length change does (P90).
void EditorStateTest::parity_P91_speedChangeRipples()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipSpeed(0, f.indexOf(0, QStringLiteral("a")), 2.0);
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    const drift::Clip *b = f.clip(0, QStringLiteral("b"));
    QVERIFY2(nearly(drift::usToSeconds(a->timelineDuration), 2.0), qPrintable(QString::number(drift::usToSeconds(a->timelineDuration))));
    QVERIFY2(nearly(drift::usToSeconds(b->timelineStart), 2.0), qPrintable(QString::number(drift::usToSeconds(b->timelineStart))));
    f.state.setClipSpeed(0, f.indexOf(0, QStringLiteral("a")), 0.5); // the whole 4 s shot, slowly
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 8.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->srcOut - f.clip(0, QStringLiteral("a"))->srcIn), 4.0));
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 8.0),
             qPrintable(QString::number(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart))));
}

// VEDIT P92: replacing a 6 s video with a 2 s one shortens the clip and pulls the next one in,
// leaving no gap; the whole thing is one undo step.
void EditorStateTest::parity_P92_replaceWithShorterVideoCloses()
{
    QTemporaryDir dir;
    const QString shortPath = dir.filePath(QStringLiteral("short.mp4"));
    {
        QFile f(shortPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    ParityFixture f(1);
    drift::MediaAsset s;
    s.id = QStringLiteral("short");
    s.name = QStringLiteral("short.mp4");
    s.kind = drift::MediaKind::Video;
    s.path = shortPath;
    s.durationUs = drift::secondsToUs(2.0);
    s.width = 1920;
    s.height = 1080;
    s.hasAudio = true;
    s.hasAudioKnown = true;
    f.state.project()->addAsset(s);
    f.add(0, QStringLiteral("a"), 0.0, 6.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 6.0, 3.0, drift::ClipType::Video);
    f.library.syncToProject();
    f.ready();
    QVERIFY(f.state.replaceClipMedia(0, f.indexOf(0, QStringLiteral("a")), QUrl::fromLocalFile(shortPath)));
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 2.0),
             qPrintable(QString::number(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration))));
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 2.0),
             qPrintable(QString::number(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart))));
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 6.0));
}

// VEDIT P93: 음량 버튼 — what the chips call sets the clip's (non-keyframed) volume, read back the
// way the chips read it; mute is 0.
void EditorStateTest::parity_P93_volumeChips()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("m"), 0.0, 10.0, drift::ClipType::Audio);
    f.ready();
    const int mi = f.indexOf(1, QStringLiteral("m"));
    f.state.setPlayheadUs(drift::secondsToUs(3.0));
    f.state.setClipKeyframe(1, mi, QStringLiteral("volume"), 3.0, 0.5);
    QVERIFY2(nearly(f.state.clipAt(1, mi).value(QStringLiteral("volume")).toDouble(), 0.5),
             qPrintable(f.state.clipAt(1, mi).value(QStringLiteral("volume")).toString()));
    f.state.setClipKeyframe(1, mi, QStringLiteral("volume"), 3.0, 0.0);
    QVERIFY(nearly(f.state.clipAt(1, mi).value(QStringLiteral("volume")).toDouble(), 0.0));
}

// VEDIT P95: 순서 바꾸기 — b (3-7 s) moved earlier takes a's place: b 0-4, a 4-7; the clip after
// them does not move; a dissolve overlap is kept; one undo restores the order; ends refuse.
void EditorStateTest::parity_P95_moveClipInOrder()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 4.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("c"), 7.0, 2.0, drift::ClipType::Image);
    f.ready();
    QVERIFY(f.state.moveClipInOrder(0, f.indexOf(0, QStringLiteral("b")), -1));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 0.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), 4.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("c"))->timelineStart), 7.0));
    QVERIFY(!f.state.moveClipInOrder(0, f.indexOf(0, QStringLiteral("b")), -1)); // already first
    QVERIFY(!f.state.moveClipInOrder(0, f.indexOf(0, QStringLiteral("c")), 1));  // already last
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), 0.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 3.0));
}

// VEDIT P96: 줄 옮기기 — a photo moved up lands on a new track above at the same time (picture in
// picture); one undo puts it back.
void EditorStateTest::parity_P96_moveClipToNeighbourTrack()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Image);
    f.ready();
    const int tracks = f.state.project()->tracks().size();
    QVERIFY(f.state.moveClipToNeighbourTrack(0, f.indexOf(0, QStringLiteral("b")), -1));
    QCOMPARE(f.state.project()->tracks().size(), tracks + 1);
    int bt = -1, bc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).id == QStringLiteral("b")) {
                bt = t;
                bc = c;
            }
    int at = -1, ac = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).id == QStringLiteral("a"))
                at = t, ac = c;
    QVERIFY(bt >= 0 && at >= 0);
    QVERIFY(bt < at); // above
    QVERIFY(nearly(drift::usToSeconds(f.state.project()->tracks().at(bt).clips.at(bc).timelineStart), 4.0));
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().size(), tracks);
    QCOMPARE(f.state.project()->tracks().at(0).clips.size(), 2);
}

// VEDIT P97: 복제 — the copy goes right after the original on the same track and what followed
// moves along (CapCut), rather than landing over the next clip or somewhere else.
void EditorStateTest::parity_P97_duplicateInsertsRightAfter()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("a")));
    f.state.duplicateSelectedClip();
    QStringList where;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            where << QStringLiteral("%1@%2:%3").arg(c.id.left(6)).arg(t).arg(drift::usToSeconds(c.timelineStart));
    const QString info = where.join(QStringLiteral(", "));
    QCOMPARE(f.state.project()->tracks().at(0).clips.size(), 3);
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 6.0), qPrintable(info));
    bool copyAt3 = false;
    for (const drift::Clip &c : f.state.project()->tracks().at(0).clips)
        copyAt3 = copyAt3 || (c.id != QStringLiteral("a") && c.id != QStringLiteral("b")
                              && nearly(drift::usToSeconds(c.timelineStart), 3.0));
    QVERIFY2(copyAt3, qPrintable(info));
}

// A compact picture of the timeline: every clip's id, track, start, length, effect counts and
// caption lines — enough to tell whether redo rebuilt exactly what the edit made.
static QString timelineFingerprint(const drift::Project &p)
{
    QStringList parts;
    for (int t = 0; t < p.tracks().size(); ++t)
        for (const drift::Clip &c : p.tracks().at(t).clips)
            parts << QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
                         .arg(c.id).arg(t).arg(c.timelineStart).arg(c.timelineDuration)
                         .arg(c.effects.size()).arg(c.audioEffects.size()).arg(c.subtitleCues.size());
    parts.sort();
    return QString::number(p.tracks().size()) + QLatin1Char('|') + parts.join(QLatin1Char(';'));
}

// VEDIT P98: each of tonight's edits, undone and redone, leaves exactly what it first made.
void EditorStateTest::parity_P98_tonightsEditsRedoCleanly()
{
    struct Case { const char *name; std::function<void(ParityFixture &)> edit; };
    const QList<Case> cases{
        {"order", [](ParityFixture &f) { f.state.moveClipInOrder(0, f.indexOf(0, QStringLiteral("b")), -1); }},
        {"track", [](ParityFixture &f) { f.state.moveClipToNeighbourTrack(0, f.indexOf(0, QStringLiteral("b")), -1); }},
        {"length", [](ParityFixture &f) { f.state.setClipDuration(0, f.indexOf(0, QStringLiteral("a")), 5.0); }},
        {"speed", [](ParityFixture &f) { f.state.setClipSpeed(0, f.indexOf(0, QStringLiteral("v")), 2.0); }},
        {"voice", [](ParityFixture &f) { f.state.enhanceVoice(0, f.indexOf(0, QStringLiteral("v"))); }},
        {"tone", [](ParityFixture &f) { f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5); }},
        {"scenes", [](ParityFixture &f) {
             f.state.m_sceneClipId = QStringLiteral("v");
             f.state.m_scenes = {QVariantMap{{QStringLiteral("sourceStart"), 0.0}},
                                 QVariantMap{{QStringLiteral("sourceStart"), 1.0}}};
             f.state.splitAtScenes();
         }},
    };
    for (const Case &k : cases) {
        ParityFixture f(1);
        f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
        f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
        f.add(0, QStringLiteral("v"), 6.0, 4.0, drift::ClipType::Video);
        f.ready();
        const QString start = timelineFingerprint(*f.state.project());
        k.edit(f);
        const QString made = timelineFingerprint(*f.state.project());
        QVERIFY2(made != start, k.name);
        f.state.undo();
        QVERIFY2(timelineFingerprint(*f.state.project()) == start, k.name);
        f.state.redo();
        QVERIFY2(timelineFingerprint(*f.state.project()) == made,
                 qPrintable(QString::fromLatin1(k.name) + QStringLiteral(": ") + made + QStringLiteral(" != ")
                            + timelineFingerprint(*f.state.project())));
    }
}

// VEDIT P99: tonight's settings survive saving and opening the project again — 노출 (adjust.tone),
// 목소리 또렷하게 (EQ mid +3), 2x speed, the Korean title font, split caption lines.
void EditorStateTest::parity_P99_tonightsWorkSurvivesSaveAndOpen()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("v"), 3.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.6);
    QVERIFY(f.state.enhanceVoice(0, f.indexOf(0, QStringLiteral("v"))));
    f.state.setClipSpeed(0, f.indexOf(0, QStringLiteral("v")), 2.0);
    QVERIFY(f.state.addQuickText(QStringLiteral("today")));
    f.state.addSubtitleClip(0.0);
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Subtitle)
                c.subtitleCues = AppController::splitLongCues(
                    {drift::SubtitleCue{0, drift::secondsToUs(4.0),
                                        QStringLiteral("오늘은 손주들과 함께 바닷가에 갔습니다 날씨가 정말 좋았어요")}});
    const QString before = timelineFingerprint(*f.state.project());

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("night.drift"));
    f.state.saveProject(QUrl::fromLocalFile(path));
    QVERIFY(QFileInfo::exists(path));
    f.state.loadProject(QUrl::fromLocalFile(path));
    QCOMPARE(timelineFingerprint(*f.state.project()), before);

    double exposure = 0.0, mid = 0.0, speed = 0.0;
    QString titleFont;
    int cueLines = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips) {
            for (const drift::Effect &e : c.effects)
                if (e.catalogId == QStringLiteral("adjust.tone"))
                    exposure = e.parameters.value(QStringLiteral("exposure")).toDouble();
            for (const drift::Effect &e : c.audioEffects)
                if (e.catalogId == QStringLiteral("utility.eq"))
                    mid = e.parameters.value(QStringLiteral("mid")).toDouble();
            if (c.id == QStringLiteral("v"))
                speed = c.speed;
            if (c.type == drift::ClipType::Text)
                titleFont = c.textStyle.fontFamily;
            if (c.type == drift::ClipType::Subtitle)
                cueLines = int(c.subtitleCues.size());
        }
    QVERIFY(nearly(exposure, 0.6));
    QVERIFY(nearly(mid, 3.0));
    QVERIFY(nearly(speed, 2.0));
    QCOMPARE(titleFont, QStringLiteral("검은고딕"));
    QVERIFY(cueLines >= 2);
}

// VEDIT P101: a key at the end of a 4 s clip is still at its end after 0.5x (8 s) — the move
// keeps finishing with the clip instead of half way through.
void EditorStateTest::parity_P101_speedChangeMovesKeyframes()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    drift::Clip &clip = f.state.project()->tracks()[0].clips[0];
    clip.opacity.setKeyframe(0, 0.0);
    clip.opacity.setKeyframe(drift::secondsToUs(4.0), 1.0);
    f.state.setClipSpeed(0, 0, 0.5);
    const drift::Clip *v = f.clip(0, QStringLiteral("v"));
    QVERIFY(nearly(drift::usToSeconds(v->timelineDuration), 8.0));
    QVERIFY(v->opacity.keyframes().contains(drift::secondsToUs(8.0)));
    QVERIFY(!v->opacity.keyframes().contains(drift::secondsToUs(4.0)));
    f.state.undo();
    QVERIFY(f.clip(0, QStringLiteral("v"))->opacity.keyframes().contains(drift::secondsToUs(4.0)));
}

// VEDIT P102: 예상 크기 — 60 s at 5000 + 192 kbps is about 39 MB; quality mode gives a positive
// guess that grows with the picture size; an empty timeline estimates nothing.
void EditorStateTest::parity_P102_exportSizeEstimate()
{
    ParityFixture empty(1);
    empty.ready();
    QCOMPARE(empty.state.estimatedExportMegabytes(5000, 192, 0, true), 0.0);
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 60.0, drift::ClipType::Video);
    f.ready();
    const double kakao = f.state.estimatedExportMegabytes(5000, 192, 720, true);
    QVERIFY2(qAbs(kakao - 38.94) < 0.5, qPrintable(QString::number(kakao)));
    const double small = f.state.estimatedExportMegabytes(0, 192, 720, false);
    const double big = f.state.estimatedExportMegabytes(0, 192, 2160, false);
    QVERIFY(small > 0.0);
    QVERIFY(big > small * 4.0);
    const double range = f.state.estimatedExportMegabytes(5000, 192, 720, true, 6.0); // work area
    QVERIFY(qAbs(range - kakao / 10.0) < 0.1);
}

// VEDIT P103: adding a photo while the playhead is inside a main-track clip puts it right after
// that clip and moves the rest along (CapCut), instead of on top of or far from the playhead.
void EditorStateTest::parity_P103_addInMiddleInsertsAfterClip()
{
    ParityFixture f(1);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 1200;
    f.state.project()->addAsset(a);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.library.syncToProject();
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(1.0));
    f.state.addClipsFromAssets({QStringLiteral("pic")});
    QStringList where;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            where << QStringLiteral("%1@%2:%3-%4").arg(c.id.left(4)).arg(t).arg(drift::usToSeconds(c.timelineStart))
                         .arg(drift::usToSeconds(c.timelineEnd()));
    const QString info = where.join(QStringLiteral(", "));
    const drift::Clip *pic = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.assetId == QStringLiteral("pic"))
                pic = &c;
    QVERIFY2(pic, qPrintable(info));
    // The playhead (1 s) is nearer a's start than its end: the photo goes in before a.
    QVERIFY2(nearly(drift::usToSeconds(pic->timelineStart), 0.0), qPrintable(info));
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), drift::usToSeconds(pic->timelineEnd())),
             qPrintable(info));
    QVERIFY2(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart),
                    drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd())), qPrintable(info));
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), 0.0));
}

// VEDIT P104: the effect list never offers an effect whose face / depth model is not installed —
// it would do nothing when picked.
void EditorStateTest::parity_P104_effectsNeedingMissingModelsHidden()
{
    ParityFixture f(1);
    f.ready();
    const bool face = f.state.faceDetectionAvailable();
    const bool depth = f.state.depthAvailable();
    QStringList offered; // what the effect browser shows: entries not marked unusable
    for (const QVariant &v : f.state.effectCatalog())
        if (v.toMap().value(QStringLiteral("usable"), true).toBool())
            offered << v.toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!offered.isEmpty());
    // agents still see all — but the engine-only ones (VEDIT: motion blur), which nobody picks
    int pickable = 0;
    for (const EffectPresetEntry &def : ::effectCatalog())
        if (def.meta.category != QLatin1String("internal"))
            ++pickable;
    QCOMPARE(f.state.effectCatalog().size(), pickable);
    for (const EffectPresetEntry &def : ::effectCatalog()) {
        if (def.meta.category == QLatin1String("internal"))
            QVERIFY2(!offered.contains(def.meta.id), qPrintable(def.meta.id));
        else if ((def.needsFace && !face) || (def.needsDepth && !depth))
            QVERIFY2(!offered.contains(def.meta.id), qPrintable(def.meta.id));
        else
            QVERIFY2(offered.contains(def.meta.id), qPrintable(def.meta.id));
    }
}

// VEDIT P105: two photos added together at a playhead inside a clip go in side by side, in the
// order picked, before the clip they were dropped near; the rest moves by both.
void EditorStateTest::parity_P105_insertSeveralKeepsTheirOrder()
{
    ParityFixture f(1);
    for (const QString &id : {QStringLiteral("p1"), QStringLiteral("p2")}) {
        drift::MediaAsset a;
        a.id = id;
        a.name = id + QStringLiteral(".jpg");
        a.kind = drift::MediaKind::Image;
        a.path = QStringLiteral("/nonexistent/") + a.name;
        a.width = 1600;
        a.height = 1200;
        f.state.project()->addAsset(a);
    }
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Image);
    f.library.syncToProject();
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(3.0)); // nearer a's end: in between a and b
    f.state.addClipsFromAssets({QStringLiteral("p1"), QStringLiteral("p2")});
    QMap<drift::TimeUs, QString> order;
    for (const drift::Clip &c : f.state.project()->tracks().at(0).clips)
        order.insert(c.timelineStart, c.assetId.isEmpty() ? c.id : c.assetId);
    const QStringList seq = order.values();
    QCOMPARE(seq, QStringList({QStringLiteral("a"), QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("b")}));
    // Back to back, no gaps or overlaps.
    drift::TimeUs end = 0;
    for (auto it = order.constBegin(); it != order.constEnd(); ++it) {
        QCOMPARE(it.key(), end);
        for (const drift::Clip &c : f.state.project()->tracks().at(0).clips)
            if (c.timelineStart == it.key())
                end = c.timelineEnd();
    }
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(0).clips.size(), 2);
}

// VEDIT P106: inserting where two clips dissolve (playhead in the overlap, nearer the first one's
// end) pushes the dissolve partner past the new picture instead of leaving it underneath.
void EditorStateTest::parity_P106_insertInsideDissolve()
{
    ParityFixture f(1);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 1200;
    f.state.project()->addAsset(a);
    f.add(0, QStringLiteral("x"), 0.0, 10.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("y"), 8.0, 10.0, drift::ClipType::Image);
    f.library.syncToProject();
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(9.0));
    f.state.addClipsFromAssets({QStringLiteral("pic")});
    const drift::Clip *pic = nullptr;
    for (const drift::Clip &c : f.state.project()->tracks().at(0).clips)
        if (c.assetId == QStringLiteral("pic"))
            pic = &c;
    QVERIFY(pic);
    QVERIFY(nearly(drift::usToSeconds(pic->timelineStart), 10.0));
    const drift::Clip *y = f.clip(0, QStringLiteral("y"));
    QVERIFY2(y->timelineStart > pic->timelineStart, qPrintable(QString::number(drift::usToSeconds(y->timelineStart))));
}

// VEDIT P107: a speed change also stretches the curve handles and the mask's keys.
void EditorStateTest::parity_P107_speedScalesHandlesAndMask()
{
    drift::KeyframeTrack<double> t;
    drift::Keyframe<double> k;
    k.value = 1.0;
    k.outDx = 1000.0;
    k.inDx = -1000.0;
    t.setKeyframe(drift::secondsToUs(2.0), k);
    t.scaleBy(2.0);
    QVERIFY(t.keyframes().contains(drift::secondsToUs(4.0)));
    QVERIFY(nearly(t.keyframes().value(drift::secondsToUs(4.0)).outDx, 2000.0));
    QVERIFY(nearly(t.keyframes().value(drift::secondsToUs(4.0)).inDx, -2000.0));
    drift::Clip clip;
    clip.mask.keyframes[QStringLiteral("x")].setKeyframe(drift::secondsToUs(3.0), 0.5);
    drift::scaleClipKeyframes(clip, 2.0);
    QVERIFY(clip.mask.keyframes.value(QStringLiteral("x")).keyframes().contains(drift::secondsToUs(6.0)));
}

// VEDIT P108: slowing a video whose sound was separated also moves the sound's volume keys, so a
// fade drawn on the sound still ends with it.
void EditorStateTest::parity_P108_speedMovesPartnerVolumeKeys()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("s"), 0.0, 4.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            c.linkId = QStringLiteral("L");
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("s"))
                c.volume.setKeyframe(drift::secondsToUs(4.0), 0.0);
    int vt = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            if (c.id == QStringLiteral("v"))
                vt = t;
    QVERIFY(vt >= 0);
    f.state.setClipSpeed(vt, f.indexOf(vt, QStringLiteral("v")), 0.5);
    const drift::Clip *sound = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("s"))
                sound = &c;
    QVERIFY(sound);
    QVERIFY2(sound->volume.keyframes().contains(drift::secondsToUs(8.0)),
             qPrintable(QString::number(sound->volume.keyframes().isEmpty() ? -1.0
                                        : drift::usToSeconds(sound->volume.keyframes().firstKey()))));
}

// VEDIT P109: inserting a photo before a video whose sound was separated moves that sound and the
// narration after it on the sound track together, so they never overlap.
void EditorStateTest::parity_P109_insertMovesSoundTrackTogether()
{
    ParityFixture f(2);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 1200;
    f.state.project()->addAsset(a);
    f.add(0, QStringLiteral("v1"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("v2"), 4.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("s2"), 4.0, 4.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("n"), 8.0, 2.0, drift::ClipType::Audio);
    f.library.syncToProject();
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("v2") || c.id == QStringLiteral("s2"))
                c.linkId = QStringLiteral("L2");
    f.state.setPlayheadUs(drift::secondsToUs(4.0));
    f.state.addClipsFromAssets({QStringLiteral("pic")});
    const auto find = [&](const QString &id) -> const drift::Clip * {
        for (const drift::Track &t : f.state.project()->tracks())
            for (const drift::Clip &c : t.clips)
                if (c.id == id)
                    return &c;
        return nullptr;
    };
    QVERIFY(find(QStringLiteral("v2")) && find(QStringLiteral("s2")) && find(QStringLiteral("n")));
    QCOMPARE(find(QStringLiteral("s2"))->timelineStart, find(QStringLiteral("v2"))->timelineStart);
    QVERIFY2(find(QStringLiteral("n"))->timelineStart >= find(QStringLiteral("s2"))->timelineEnd(),
             qPrintable(QString::number(drift::usToSeconds(find(QStringLiteral("n"))->timelineStart))));
    QVERIFY(nearly(drift::usToSeconds(find(QStringLiteral("v1"))->timelineStart), 0.0));
}

// VEDIT P110: deleting a video (with its separated sound) from the middle closes the gap on the
// main track and on its sound track alike: the next video and its sound stay together, and the
// narration behind them on the sound track does not end up on top of them.
void EditorStateTest::parity_P110_deleteClosesSoundTrackTogether()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v1"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("v2"), 4.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("v3"), 8.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("s2"), 4.0, 4.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("s3"), 8.0, 4.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            if (c.id == QStringLiteral("v2") || c.id == QStringLiteral("s2"))
                c.linkId = QStringLiteral("L2");
            if (c.id == QStringLiteral("v3") || c.id == QStringLiteral("s3"))
                c.linkId = QStringLiteral("L3");
        }
    int vt = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            if (c.id == QStringLiteral("v2"))
                vt = t;
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v2")));
    f.state.deleteSelectedClip();
    const auto find = [&](const QString &id) -> const drift::Clip * {
        for (const drift::Track &t : f.state.project()->tracks())
            for (const drift::Clip &c : t.clips)
                if (c.id == id)
                    return &c;
        return nullptr;
    };
    QStringList where;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            where << QStringLiteral("%1@%2:%3").arg(c.id).arg(t).arg(drift::usToSeconds(c.timelineStart));
    const QString info = where.join(QStringLiteral(", "));
    QVERIFY2(!find(QStringLiteral("v2")) && !find(QStringLiteral("s2")), qPrintable(info));
    QVERIFY2(find(QStringLiteral("v3")) && nearly(drift::usToSeconds(find(QStringLiteral("v3"))->timelineStart), 4.0), qPrintable(info));
    QVERIFY2(find(QStringLiteral("s3")) && find(QStringLiteral("s3"))->timelineStart == find(QStringLiteral("v3"))->timelineStart,
             qPrintable(info));
}

// Two videos with their separated sounds below, and a narration after them on the sound track.
static void linkedPairFixture(ParityFixture &f)
{
    f.add(0, QStringLiteral("v1"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("v2"), 4.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("s1"), 0.0, 4.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("s2"), 4.0, 4.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("n"), 8.0, 2.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            if (c.id == QStringLiteral("v1") || c.id == QStringLiteral("s1"))
                c.linkId = QStringLiteral("L1");
            if (c.id == QStringLiteral("v2") || c.id == QStringLiteral("s2"))
                c.linkId = QStringLiteral("L2");
        }
}

static const drift::Clip *clipById(const drift::Project &p, const QString &id, int *trackOut = nullptr)
{
    for (int t = 0; t < p.tracks().size(); ++t)
        for (const drift::Clip &c : p.tracks().at(t).clips)
            if (c.id == id) {
                if (trackOut)
                    *trackOut = t;
                return &c;
            }
    return nullptr;
}

static QString layoutText(const drift::Project &p)
{
    QStringList where;
    for (int t = 0; t < p.tracks().size(); ++t)
        for (const drift::Clip &c : p.tracks().at(t).clips)
            where << QStringLiteral("%1@%2:%3-%4").arg(c.id).arg(t).arg(drift::usToSeconds(c.timelineStart))
                         .arg(drift::usToSeconds(c.timelineEnd()));
    return where.join(QStringLiteral(", "));
}

// VEDIT P111: swapping two videos swaps their separated sounds too.
void EditorStateTest::parity_P111_swapKeepsSoundsWithPictures()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v2"), &vt);
    QVERIFY(f.state.moveClipInOrder(vt, f.indexOf(vt, QStringLiteral("v2")), -1));
    const drift::Project &p = *f.state.project();
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s1"))->timelineStart == clipById(p, QStringLiteral("v1"))->timelineStart,
             qPrintable(layoutText(p)));
}

// VEDIT P112: lengthening a video pushes its neighbour's sound and the narration after it alike,
// so nothing on the sound track ends up overlapping.
void EditorStateTest::parity_P112_lengthChangeKeepsSoundTrackClear()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    // v1 is a video: lengthen by slowing it (a length change that keeps its footage).
    f.state.setClipSpeed(vt, f.indexOf(vt, QStringLiteral("v1")), 0.5);
    const drift::Project &p = *f.state.project();
    int st = -1;
    clipById(p, QStringLiteral("s2"), &st);
    QList<QPair<drift::TimeUs, drift::TimeUs>> spans;
    for (const drift::Clip &c : p.tracks().at(st).clips)
        spans.append({c.timelineStart, c.timelineEnd()});
    std::sort(spans.begin(), spans.end());
    for (int i = 1; i < spans.size(); ++i)
        QVERIFY2(spans.at(i).first >= spans.at(i - 1).second, qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
}

// VEDIT P113: duplicating a video whose sound was separated pushes the next video, its sound and
// the narration behind it along together; the copy is the picture alone (the sound is its own clip).
void EditorStateTest::parity_P113_duplicateCarriesItsSound()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v1")));
    f.state.duplicateSelectedClip();
    const drift::Project &p = *f.state.project();
    int audio = 0;
    for (const drift::Track &t : p.tracks())
        for (const drift::Clip &c : t.clips)
            audio += c.type == drift::ClipType::Audio;
    // The separated sound stays its own clip (as in CapCut): the copy is the picture alone.
    QVERIFY2(audio == 3, qPrintable(layoutText(p)));
    // The narration after the pushed sound moved with it instead of lying underneath.
    QVERIFY2(clipById(p, QStringLiteral("n"))->timelineStart >= clipById(p, QStringLiteral("s2"))->timelineEnd(),
             qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
}

// VEDIT P114: shortening a photo-length video by number pulls the next video, its sound and the
// narration back together — no gap and no overlap on the sound track.
void EditorStateTest::parity_P114_shortenKeepsSoundTrackTogether()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.setClipDuration(vt, f.indexOf(vt, QStringLiteral("v1")), 2.0);
    const drift::Project &p = *f.state.project();
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("v2"))->timelineStart), 2.0), qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("s1"))->timelineDuration), 2.0), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("n"))->timelineStart), 6.0), qPrintable(layoutText(p)));
}

// No two clips on one track overlap.
static bool soundTrackClear(const drift::Project &p, int track)
{
    QList<QPair<drift::TimeUs, drift::TimeUs>> spans;
    for (const drift::Clip &c : p.tracks().at(track).clips)
        spans.append({c.timelineStart, c.timelineEnd()});
    std::sort(spans.begin(), spans.end());
    for (int i = 1; i < spans.size(); ++i)
        if (spans.at(i).first < spans.at(i - 1).second)
            return false;
    return true;
}

// VEDIT P115: 앞부분 지우기 (Q) on the first video pulls the next video, its sound and the
// narration back together.
void EditorStateTest::parity_P115_trimFrontKeepsSoundTrackTogether()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v1")));
    f.state.setPlayheadUs(drift::secondsToUs(1.0));
    f.state.splitSelectedClipLeft(); // Q
    const drift::Project &p = *f.state.project();
    int st = -1;
    clipById(p, QStringLiteral("s2"), &st);
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
    QVERIFY2(soundTrackClear(p, st), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("v2"))->timelineStart), 3.0), qPrintable(layoutText(p)));
}

// VEDIT P116: moving the first video up a track (picture in picture) keeps every sound clear and
// each separated sound with its picture.
void EditorStateTest::parity_P116_moveUpKeepsSoundsWithPictures()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    QVERIFY(f.state.moveClipToNeighbourTrack(vt, f.indexOf(vt, QStringLiteral("v1")), -1));
    const drift::Project &p = *f.state.project();
    QVERIFY2(clipById(p, QStringLiteral("s1"))->timelineStart == clipById(p, QStringLiteral("v1"))->timelineStart,
             qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
    int st = -1;
    clipById(p, QStringLiteral("s2"), &st);
    QVERIFY2(soundTrackClear(p, st), qPrintable(layoutText(p)));
}

// VEDIT P117: 뒷부분 지우기 (W) on the first video pulls the next video, its sound and the
// narration back together, with nothing overlapping on the sound track.
void EditorStateTest::parity_P117_trimBackKeepsSoundTrackTogether()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v1")));
    f.state.setPlayheadUs(drift::secondsToUs(3.0));
    f.state.splitSelectedClipRight(); // W
    const drift::Project &p = *f.state.project();
    int st = -1;
    clipById(p, QStringLiteral("s2"), &st);
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("v2"))->timelineStart), 3.0), qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("s2"))->timelineStart == clipById(p, QStringLiteral("v2"))->timelineStart,
             qPrintable(layoutText(p)));
    QVERIFY2(soundTrackClear(p, st), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("n"))->timelineStart), 7.0), qPrintable(layoutText(p)));
}

// VEDIT P118: split (S) the first video at 2 s and delete the front piece: its sound is split and
// removed with it, and every remaining sound still starts with its picture.
void EditorStateTest::parity_P118_splitThenDeleteKeepsSoundsInStep()
{
    ParityFixture f(2);
    linkedPairFixture(f);
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v1")));
    f.state.setPlayheadUs(drift::secondsToUs(2.0));
    f.state.splitAtPlayhead();
    // The front piece keeps the id "v1".
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v1")));
    f.state.deleteSelectedClip();
    const drift::Project &p = *f.state.project();
    QVERIFY2(!clipById(p, QStringLiteral("v1")) && !clipById(p, QStringLiteral("s1")), qPrintable(layoutText(p)));
    // Every picture's sound starts with it.
    for (const drift::Track &t : p.tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Video && !c.linkId.isEmpty())
                for (const drift::Track &u : p.tracks())
                    for (const drift::Clip &a : u.clips)
                        if (a.type == drift::ClipType::Audio && a.linkId == c.linkId)
                            QVERIFY2(a.timelineStart == c.timelineStart, qPrintable(layoutText(p)));
    int st = -1;
    clipById(p, QStringLiteral("s2"), &st);
    QVERIFY2(soundTrackClear(p, st), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("v2"))->timelineStart), 2.0), qPrintable(layoutText(p)));
}

// VEDIT P119: 자막 위치 — top puts the caption band near the top of the screen with the text at
// its top; bottom brings it back; one undo each.
void EditorStateTest::parity_P119_captionPlacement()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    int st = -1, sc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Subtitle) {
                st = t;
                sc = c;
            }
    QVERIFY(st >= 0);
    const double h = f.state.project()->height();
    const auto y = [&]() {
        return f.state.project()->tracks().at(st).clips.at(sc).transformY.keyframes().first().value;
    };
    QVERIFY(f.state.setCaptionPlacement(st, sc, QStringLiteral("top")));
    QVERIFY2(y() < h * 0.1, qPrintable(QString::number(y())));
    QCOMPARE(f.state.project()->tracks().at(st).clips.at(sc).textStyle.valign, drift::TextVAlign::Top);
    QVERIFY(f.state.setCaptionPlacement(st, sc, QStringLiteral("bottom")));
    QVERIFY(y() > h * 0.7);
    f.state.undo();
    QVERIFY(y() < h * 0.1);
    QVERIFY(!f.state.setCaptionPlacement(st, sc, QStringLiteral("left")));
}

// VEDIT P120: a caption moved to the top, applied to all captions, takes the others to the top
// too (band and alignment), not just its alignment.
void EditorStateTest::parity_P120_applyToAllCaptionsCarriesPlacement()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    f.state.addSubtitleClip(10.0);
    QList<QPair<int, int>> subs;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Subtitle)
                subs.append({t, c});
    QCOMPARE(subs.size(), 2);
    QCOMPARE(subs.at(0).first, subs.at(1).first);
    QVERIFY(f.state.setCaptionPlacement(subs.at(0).first, subs.at(0).second, QStringLiteral("top")));
    QCOMPARE(f.state.applyTextStyleToCaptions(subs.at(0).first, subs.at(0).second, QStringLiteral("track")), 1);
    const drift::Clip &other = f.state.project()->tracks().at(subs.at(1).first).clips.at(subs.at(1).second);
    QVERIFY(other.transformY.keyframes().first().value < f.state.project()->height() * 0.1);
    QCOMPARE(other.textStyle.valign, drift::TextVAlign::Top);
}

// VEDIT P121: after switching the screen to portrait (9:16), a caption put at the top is still at
// the top and inside the new screen, and a bottom one still at the bottom.
void EditorStateTest::parity_P121_captionPlacementSurvivesRatioChange()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 20.0, drift::ClipType::Video);
    f.ready();
    f.state.addSubtitleClip(0.0);
    f.state.addSubtitleClip(10.0);
    QList<QString> ids;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Subtitle)
                ids << c.id;
    QCOMPARE(ids.size(), 2);
    int t = -1, c = -1;
    QVERIFY(clipById(*f.state.project(), ids.at(0), &t));
    c = f.indexOf(t, ids.at(0));
    QVERIFY(f.state.setCaptionPlacement(t, c, QStringLiteral("top")));
    f.state.setProjectResolution(1080, 1920);
    const double w = f.state.project()->width();
    const double h = f.state.project()->height();
    QCOMPARE(int(h), 1920);
    const drift::Clip *top = clipById(*f.state.project(), ids.at(0));
    const drift::Clip *bottom = clipById(*f.state.project(), ids.at(1));
    const auto v = [](const drift::KeyframeTrack<double> &k) { return k.keyframes().first().value; };
    const QString info = QStringLiteral("top y=%1 w=%2 h=%3 | bottom y=%4 w=%5 h=%6")
                             .arg(v(top->transformY)).arg(v(top->transformW)).arg(v(top->transformH))
                             .arg(v(bottom->transformY)).arg(v(bottom->transformW)).arg(v(bottom->transformH));
    QVERIFY2(v(top->transformY) < h * 0.15, qPrintable(info));
    QVERIFY2(v(bottom->transformY) > h * 0.6, qPrintable(info));
    QVERIFY2(v(top->transformW) <= w + 1 && v(bottom->transformW) <= w + 1, qPrintable(info));
}

// VEDIT P122: a landscape photo fitted to a 16:9 screen, and a centred title, are still inside the
// screen and centred after switching to portrait 9:16 (CapCut refits them).
void EditorStateTest::parity_P122_picturesRefitOnRatioChange()
{
    ParityFixture f(1);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 900;
    f.state.project()->addAsset(a);
    f.library.syncToProject();
    f.ready();
    f.state.addClipsFromAssets({QStringLiteral("pic")});
    QVERIFY(f.state.addQuickText(QStringLiteral("thanks")));
    f.state.setProjectResolution(1080, 1920);
    const double w = 1080.0, h = 1920.0;
    QStringList bad;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips) {
            if (c.type != drift::ClipType::Image && c.type != drift::ClipType::Text)
                continue;
            const double x = c.transformX.keyframes().first().value;
            const double y = c.transformY.keyframes().first().value;
            const double cw = c.transformW.keyframes().first().value;
            const double ch = c.transformH.keyframes().first().value;
            const QString box = QStringLiteral("%1 %2,%3 %4x%5").arg(c.type == drift::ClipType::Image ? "photo" : "text")
                                    .arg(x).arg(y).arg(cw).arg(ch);
            if (cw > w + 2 || x < -2 || x + cw > w + 2)
                bad << box + QStringLiteral(" (wider than the screen)");
            else if (std::abs((x + cw / 2) - w / 2) > 4 || std::abs((y + ch / 2) - h / 2) > h * 0.25)
                bad << box + QStringLiteral(" (not centred)");
        }
    QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QStringLiteral("; "))));
}

// VEDIT P123: a slideshow photo with "slowly closer" switched to portrait: its motion is built
// again around the new screen's fit — it starts inside the portrait screen, centred — and the whole
// change is one undo.
void EditorStateTest::parity_P123_movingPhotoRefitsOnRatioChange()
{
    ParityFixture f(1);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 900;
    f.state.project()->addAsset(a);
    f.library.syncToProject();
    f.ready();
    QCOMPARE(f.state.makeSlideshow({QStringLiteral("pic")}, 3.0), 1);
    const drift::Clip *pic = nullptr;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.assetId == QStringLiteral("pic"))
                pic = &c;
    QVERIFY(pic && !pic->motionPreset.isEmpty());
    const QString id = pic->id;
    f.state.setProjectResolution(1080, 1920);
    pic = clipById(*f.state.project(), id);
    QVERIFY(pic);
    QVERIFY2(pic->motionBaseW <= 1082 && std::abs((pic->motionBaseX + pic->motionBaseW / 2) - 540) < 4,
             qPrintable(QStringLiteral("base %1,%2 %3x%4").arg(pic->motionBaseX).arg(pic->motionBaseY)
                            .arg(pic->motionBaseW).arg(pic->motionBaseH)));
    const double w0 = pic->transformW.keyframes().first().value;
    QVERIFY2(w0 <= 1082 * 1.25, qPrintable(QString::number(w0))); // starts about the screen's width
    f.state.undo();
    QCOMPARE(f.state.project()->height(), 1080);
}

// VEDIT P124: a picture-in-picture photo put in the top-right corner stays in the top-right corner,
// inside the screen, after switching to portrait.
void EditorStateTest::parity_P124_cornerPhotoStaysInCornerOnRatioChange()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    drift::MediaAsset a;
    a.id = QStringLiteral("pic");
    a.name = QStringLiteral("pic.jpg");
    a.kind = drift::MediaKind::Image;
    a.path = QStringLiteral("/nonexistent/pic.jpg");
    a.width = 1600;
    a.height = 900;
    f.state.project()->addAsset(a);
    f.library.syncToProject();
    f.ready();
    f.state.addClipFromAssetOnNewTrack(f.library.indexOfId(QStringLiteral("pic")), 0.0);
    int pt = -1;
    QString id;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (const drift::Clip &c : f.state.project()->tracks().at(t).clips)
            if (c.assetId == QStringLiteral("pic")) {
                pt = t;
                id = c.id;
            }
    QVERIFY(pt >= 0);
    f.state.placeClipOnScreen(pt, f.indexOf(pt, id), QStringLiteral("topRight"));
    f.state.setProjectResolution(1080, 1920);
    const drift::Clip *pic = clipById(*f.state.project(), id);
    const auto v = [](const drift::KeyframeTrack<double> &k) { return k.keyframes().first().value; };
    const double x = v(pic->transformX), y = v(pic->transformY), w = v(pic->transformW), h = v(pic->transformH);
    const QString info = QStringLiteral("%1,%2 %3x%4").arg(x).arg(y).arg(w).arg(h);
    QVERIFY2(x >= -1 && x + w <= 1081 && y >= -1 && y + h <= 1921, qPrintable(info)); // inside
    QVERIFY2(x + w > 1080 * 0.8 && y < 1920 * 0.2, qPrintable(info));               // top-right
}

// VEDIT P125: deleting a video and its sound together, with a gap before the next clip, closes
// the gap once (not twice) on both tracks; and an overlay's sound on the same sound track stays
// with its overlay; and a narration is never pulled onto the clip before it.
void EditorStateTest::parity_P125_deleteWithSoundRipplesOnce()
{
    ParityFixture f(3);
    f.add(0, QStringLiteral("A"), 0.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("B"), 10.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("C"), 40.0, 10.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 10.0, 10.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("c"), 40.0, 10.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            if (c.id == QStringLiteral("B") || c.id == QStringLiteral("b"))
                c.linkId = QStringLiteral("LB");
            if (c.id == QStringLiteral("C") || c.id == QStringLiteral("c"))
                c.linkId = QStringLiteral("LC");
        }
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("B"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("B")));
    f.state.deleteSelectedClip();
    const drift::Project &p = *f.state.project();
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("C"))->timelineStart), 30.0), qPrintable(layoutText(p)));
    QVERIFY2(clipById(p, QStringLiteral("c"))->timelineStart == clipById(p, QStringLiteral("C"))->timelineStart,
             qPrintable(layoutText(p)));
}

// VEDIT P126: makeRoomOnSoundTracks never pulls a narration onto the narration before it.
void EditorStateTest::parity_P126_pullBackStopsAtThePreviousClip()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("V1"), 0.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("V2"), 10.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("V3"), 20.0, 10.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("s3"), 20.0, 10.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("n1"), 12.0, 6.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("n2"), 30.0, 5.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("V3") || c.id == QStringLiteral("s3"))
                c.linkId = QStringLiteral("L3");
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("V2"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("V2")));
    f.state.deleteSelectedClip(); // V3 and s3 come back 10 s
    const drift::Project &p = *f.state.project();
    const drift::Clip *n2 = clipById(p, QStringLiteral("n2"));
    QVERIFY2(n2->timelineStart >= clipById(p, QStringLiteral("s3"))->timelineEnd()
                 && n2->timelineStart >= clipById(p, QStringLiteral("n1"))->timelineEnd(),
             qPrintable(layoutText(p)));
}

// The timeline picture plus every clip's layout (for edits that only move things on screen).
static QString layoutFingerprint(const drift::Project &p)
{
    QStringList parts{timelineFingerprint(p), QStringLiteral("%1x%2").arg(p.width()).arg(p.height())};
    for (const drift::Track &t : p.tracks())
        for (const drift::Clip &c : t.clips)
            if (!c.transformX.keyframes().isEmpty())
                parts << QStringLiteral("%1:%2,%3,%4,%5,%6").arg(c.id)
                             .arg(c.transformX.keyframes().first().value).arg(c.transformY.keyframes().first().value)
                             .arg(c.transformW.keyframes().first().value).arg(c.transformH.keyframes().first().value)
                             .arg(int(c.textStyle.valign));
    return parts.join(QLatin1Char('|'));
}

// VEDIT P127: the later edits — screen shape, caption position, inserting a photo, deleting a
// picture with its sound — also undo and redo to exactly what they first made.
void EditorStateTest::parity_P127_laterEditsRedoCleanly()
{
    struct Case { const char *name; std::function<void(ParityFixture &)> edit; };
    const QList<Case> cases{
        {"ratio", [](ParityFixture &f) { f.state.setProjectResolution(1080, 1920); }},
        {"caption", [](ParityFixture &f) {
             for (int t = 0; t < f.state.project()->tracks().size(); ++t)
                 for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
                     if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Subtitle) {
                         f.state.setCaptionPlacement(t, c, QStringLiteral("top"));
                         return;
                     }
         }},
        {"insert", [](ParityFixture &f) {
             f.state.setPlayheadUs(drift::secondsToUs(1.0));
             f.state.addClipsFromAssets({QStringLiteral("pic")});
         }},
        {"delete", [](ParityFixture &f) {
             int vt = -1;
             clipById(*f.state.project(), QStringLiteral("v2"), &vt);
             f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("v2")));
             f.state.deleteSelectedClip();
         }},
    };
    for (const Case &k : cases) {
        ParityFixture f(2);
        drift::MediaAsset a;
        a.id = QStringLiteral("pic");
        a.name = QStringLiteral("pic.jpg");
        a.kind = drift::MediaKind::Image;
        a.path = QStringLiteral("/nonexistent/pic.jpg");
        a.width = 1600;
        a.height = 900;
        f.state.project()->addAsset(a);
        f.add(0, QStringLiteral("v1"), 0.0, 4.0, drift::ClipType::Video);
        f.add(0, QStringLiteral("v2"), 4.0, 4.0, drift::ClipType::Video);
        f.add(1, QStringLiteral("s2"), 4.0, 4.0, drift::ClipType::Audio);
        f.add(1, QStringLiteral("n"), 8.0, 2.0, drift::ClipType::Audio);
        f.library.syncToProject();
        f.ready();
        for (drift::Track &t : f.state.project()->tracks())
            for (drift::Clip &c : t.clips)
                if (c.id == QStringLiteral("v2") || c.id == QStringLiteral("s2"))
                    c.linkId = QStringLiteral("L2");
        f.state.addSubtitleClip(0.0); // also makes the undo stack non-empty
        const QString start = layoutFingerprint(*f.state.project());
        k.edit(f);
        const QString made = layoutFingerprint(*f.state.project());
        QVERIFY2(made != start, k.name);
        f.state.undo();
        QVERIFY2(layoutFingerprint(*f.state.project()) == start, k.name);
        f.state.redo();
        QVERIFY2(layoutFingerprint(*f.state.project()) == made,
                 qPrintable(QString::fromLatin1(k.name) + QStringLiteral(": ") + made + QStringLiteral(" != ")
                            + layoutFingerprint(*f.state.project())));
    }
}

// VEDIT P128: a shape near the right edge stays near the right edge and inside the screen after
// switching to portrait.
void EditorStateTest::parity_P128_shapeStaysOnScreenOnRatioChange()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    f.state.addShapeClip(QStringLiteral("rectangle"), 0.0);
    QString id;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            if (c.type == drift::ClipType::Shape)
                id = c.id;
    QVERIFY(!id.isEmpty());
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == id) {
                c.transformX = {};
                c.transformY = {};
                c.transformW = {};
                c.transformH = {};
                c.transformX.setKeyframe(0, 1500.0);
                c.transformY.setKeyframe(0, 100.0);
                c.transformW.setKeyframe(0, 300.0);
                c.transformH.setKeyframe(0, 200.0);
            }
    f.state.setProjectResolution(1080, 1920);
    const drift::Clip *shape = clipById(*f.state.project(), id);
    const auto v = [](const drift::KeyframeTrack<double> &k) { return k.keyframes().first().value; };
    const double x = v(shape->transformX), w = v(shape->transformW);
    QVERIFY2(x >= 0 && x + w <= 1081 && x + w > 1080 * 0.7,
             qPrintable(QStringLiteral("%1 %2").arg(x).arg(w)));
}

// VEDIT P129: 글자 크기 버튼 — what the chips call sets the size of a title (and a caption) and
// one undo puts it back.
void EditorStateTest::parity_P129_textSizeChips()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    QVERIFY(f.state.addQuickText(QStringLiteral("thanks")));
    int tt = -1, tc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Text) {
                tt = t;
                tc = c;
            }
    QVERIFY(tt >= 0);
    const int before = f.state.project()->tracks().at(tt).clips.at(tc).textStyle.pixelSize;
    f.state.setTextStyle(tt, tc, QVariantMap{{QStringLiteral("pixelSize"), 100}});
    QCOMPARE(f.state.project()->tracks().at(tt).clips.at(tc).textStyle.pixelSize, 100);
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(tt).clips.at(tc).textStyle.pixelSize, before);
}

// VEDIT P130: 글자 색 버튼 — what a colour dot calls turns the words yellow; undo turns them back.
void EditorStateTest::parity_P130_textColourDots()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    QVERIFY(f.state.addQuickText(QStringLiteral("thanks")));
    int tt = -1, tc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Text) {
                tt = t;
                tc = c;
            }
    QVERIFY(tt >= 0);
    const QColor before = f.state.project()->tracks().at(tt).clips.at(tc).textStyle.primaryColor();
    f.state.setTextStyle(tt, tc, QVariantMap{{QStringLiteral("color"), QStringLiteral("#ffffd640")}});
    QCOMPARE(f.state.project()->tracks().at(tt).clips.at(tc).textStyle.primaryColor().name(QColor::HexArgb),
             QStringLiteral("#ffffd640"));
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(tt).clips.at(tc).textStyle.primaryColor(), before);
}

// VEDIT P131: 글자 뒤 어두운 상자 — what the chip calls turns a see-through dark box on behind a
// title, and off again.
void EditorStateTest::parity_P131_darkBoxBehindWords()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    QVERIFY(f.state.addQuickText(QStringLiteral("today")));
    int tt = -1, tc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Text) {
                tt = t;
                tc = c;
            }
    QVERIFY(tt >= 0);
    f.state.setTextStyle(tt, tc, QVariantMap{{QStringLiteral("boxEnabled"), true},
                                             {QStringLiteral("boxColor"), QStringLiteral("#99000000")}});
    const drift::TextStyle &on = f.state.project()->tracks().at(tt).clips.at(tc).textStyle;
    QVERIFY(on.boxEnabled);
    QCOMPARE(on.boxColor.alpha(), 0x99);
    f.state.setTextStyle(tt, tc, QVariantMap{{QStringLiteral("boxEnabled"), false}});
    QVERIFY(!f.state.project()->tracks().at(tt).clips.at(tc).textStyle.boxEnabled);
}

// VEDIT P132: 모든 사진·영상에 이 필터 — the warm look on one photo goes onto the other photo and
// the video too, in one undo step.
void EditorStateTest::parity_P132_filterOnEveryPicture()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("v"), 6.0, 3.0, drift::ClipType::Video);
    f.ready();
    f.state.applyClipFilter(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("warm"), 0.8);
    QCOMPARE(f.state.applyFilterToAllPictures(0, f.indexOf(0, QStringLiteral("a"))), 2);
    for (const QString &id : {QStringLiteral("b"), QStringLiteral("v")}) {
        const QVariantMap look = f.state.clipFilter(0, f.indexOf(0, id));
        QCOMPARE(look.value(QStringLiteral("id")).toString(), QStringLiteral("warm"));
        QVERIFY(nearly(look.value(QStringLiteral("strength")).toDouble(), 0.8));
    }
    f.state.undo();
    QCOMPARE(f.state.clipFilter(0, f.indexOf(0, QStringLiteral("b"))).value(QStringLiteral("id")).toString(),
             QStringLiteral("none"));
}

// VEDIT P133: 모든 사진·영상에 이 조정 — exposure +0.5 on one photo replaces the other photo's own
// contrast setting and lands on the video too; the filter on the other photo stays; one undo.
void EditorStateTest::parity_P133_adjustOnEveryPicture()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("v"), 6.0, 3.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5);
    f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("b")), QStringLiteral("adjust.contrast"), QStringLiteral("contrast"), 1.6);
    f.state.applyClipFilter(0, f.indexOf(0, QStringLiteral("b")), QStringLiteral("warm"), 1.0);
    QCOMPARE(f.state.applyAdjustToAllPictures(0, f.indexOf(0, QStringLiteral("a"))), 2);
    const QVariantMap b = f.state.clipAdjustValues(0, f.indexOf(0, QStringLiteral("b")));
    QVERIFY(nearly(b.value(QStringLiteral("adjust.tone.exposure")).toDouble(), 0.5));
    QVERIFY(!b.contains(QStringLiteral("adjust.contrast.contrast")));
    QCOMPARE(f.state.clipFilter(0, f.indexOf(0, QStringLiteral("b"))).value(QStringLiteral("id")).toString(),
             QStringLiteral("warm"));
    QVERIFY(nearly(f.state.clipAdjustValues(0, f.indexOf(0, QStringLiteral("v")))
                       .value(QStringLiteral("adjust.tone.exposure")).toDouble(), 0.5));
    f.state.undo();
    QVERIFY(nearly(f.state.clipAdjustValues(0, f.indexOf(0, QStringLiteral("b")))
                       .value(QStringLiteral("adjust.contrast.contrast")).toDouble(), 1.6));
}

// VEDIT P134: 모든 사진에 이 움직임 — "slowly closer" on the first photo gives the others the
// slideshow alternation (farther, closer, …); one undo takes it all off again.
void EditorStateTest::parity_P134_motionOnEveryPhoto()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("c"), 6.0, 3.0, drift::ClipType::Image);
    f.ready();
    f.state.applyClipMotion(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("zoomIn"));
    QCOMPARE(f.state.applyMotionToAllPhotos(0, f.indexOf(0, QStringLiteral("a"))), 2);
    QCOMPARE(f.clip(0, QStringLiteral("b"))->motionPreset, QStringLiteral("zoomOut"));
    QCOMPARE(f.clip(0, QStringLiteral("c"))->motionPreset, QStringLiteral("zoomIn"));
    f.state.undo();
    QVERIFY(f.clip(0, QStringLiteral("b"))->motionPreset.isEmpty());
    QVERIFY(f.clip(0, QStringLiteral("c"))->motionPreset.isEmpty());
}

// VEDIT P135: putting a filter and adjustments on five photos at once does not grow the timeline
// a track per photo.
void EditorStateTest::parity_P135_applyToAllKeepsTracksTidy()
{
    ParityFixture f(1);
    for (int i = 0; i < 5; ++i)
        f.add(0, QStringLiteral("p%1").arg(i), i * 3.0, 3.0, drift::ClipType::Image);
    f.ready();
    const int tracksBefore = f.state.project()->tracks().size();
    f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("p0")), QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.4);
    f.state.applyClipFilter(0, f.indexOf(0, QStringLiteral("p0")), QStringLiteral("warm"), 1.0);
    const int afterOne = f.state.project()->tracks().size();
    QCOMPARE(f.state.applyAdjustToAllPictures(0, f.indexOf(0, QStringLiteral("p0"))), 4);
    QCOMPARE(f.state.applyFilterToAllPictures(0, f.indexOf(0, QStringLiteral("p0"))), 4);
    const int afterAll = f.state.project()->tracks().size();
    QVERIFY2(afterAll <= afterOne + 1,
             qPrintable(QStringLiteral("tracks %1 -> %2 -> %3").arg(tracksBefore).arg(afterOne).arg(afterAll)));
}

// VEDIT P136: deleting a picture and its sound together pulls the narration after them on the
// sound track back with the pictures (once); and the motion alternation counts from the source.
void EditorStateTest::parity_P136_deletePairKeepsNarrationInStep()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("A"), 0.0, 10.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("p"), 10.0, 10.0, drift::ClipType::Image);
    f.add(1, QStringLiteral("a"), 0.0, 10.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("n"), 20.0, 5.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("A") || c.id == QStringLiteral("a"))
                c.linkId = QStringLiteral("LA");
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("A"), &vt);
    f.state.selectClip(vt, f.indexOf(vt, QStringLiteral("A")));
    f.state.deleteSelectedClip();
    const drift::Project &p = *f.state.project();
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("p"))->timelineStart), 0.0), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("n"))->timelineStart), 10.0), qPrintable(layoutText(p)));

    ParityFixture g(1);
    for (int i = 0; i < 4; ++i)
        g.add(0, QStringLiteral("q%1").arg(i), i * 3.0, 3.0, drift::ClipType::Image);
    g.ready();
    g.state.applyClipMotion(0, g.indexOf(0, QStringLiteral("q2")), QStringLiteral("zoomIn"));
    QCOMPARE(g.state.applyMotionToAllPhotos(0, g.indexOf(0, QStringLiteral("q2"))), 3);
    QCOMPARE(g.clip(0, QStringLiteral("q1"))->motionPreset, QStringLiteral("zoomOut"));
    QCOMPARE(g.clip(0, QStringLiteral("q3"))->motionPreset, QStringLiteral("zoomOut"));
    QCOMPARE(g.clip(0, QStringLiteral("q0"))->motionPreset, QStringLiteral("zoomIn"));
}

// VEDIT P137: 모든 사진·영상에 이 조정 twice in a row — the second time nothing changes, so it
// reports 0 instead of claiming every photo again.
void EditorStateTest::parity_P137_adjustAllCountsRealChanges()
{
    ParityFixture f(1);
    for (int i = 0; i < 3; ++i)
        f.add(0, QStringLiteral("p%1").arg(i), i * 3.0, 3.0, drift::ClipType::Image);
    f.ready();
    f.state.setClipAdjust(0, f.indexOf(0, QStringLiteral("p0")), QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.3);
    QCOMPARE(f.state.applyAdjustToAllPictures(0, f.indexOf(0, QStringLiteral("p0"))), 2);
    QCOMPARE(f.state.applyAdjustToAllPictures(0, f.indexOf(0, QStringLiteral("p0"))), 0);
}

// VEDIT P138: a hand-placed title whose letters grow (80 -> 120) keeps the same growth, in
// proportion, when the picture turns from wide to tall.
void EditorStateTest::parity_P138_growingTitleScalesOnRatioChange()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("t"), 0.0, 5.0, drift::ClipType::Text);
    f.ready();
    const int oldH = f.state.project()->height();
    const int oldW = f.state.project()->width();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            if (c.id == QStringLiteral("t")) {
                c.transformX = {};
                c.transformY = {};
                c.transformW = {};
                c.transformH = {};
                c.transformX.setKeyframe(0, oldW * 0.1);
                c.transformY.setKeyframe(0, oldH * 0.1);
                c.transformW.setKeyframe(0, oldW * 0.4);
                c.transformH.setKeyframe(0, oldH * 0.2);
                c.textStyle.pixelSize = 80;
                drift::KeyframeTrack<double> grow;
                grow.setKeyframe(0, 80.0);
                grow.setKeyframe(drift::secondsToUs(2.0), 120.0);
                c.textStyle.keyframes.insert(QStringLiteral("pixelSize"), grow);
            }
    f.state.setProjectResolution(1080, 1920);
    const double s = qMin(1080.0 / oldW, 1920.0 / oldH);
    const drift::Clip *t = clipById(*f.state.project(), QStringLiteral("t"));
    QVERIFY(t);
    QCOMPARE(t->textStyle.pixelSize, int(std::lround(80 * s)));
    const auto keys = t->textStyle.keyframes.value(QStringLiteral("pixelSize")).keyframes();
    QCOMPARE(keys.size(), 2);
    QVERIFY2(nearly(keys.first().value, 80 * s), qPrintable(QString::number(keys.first().value)));
    QVERIFY2(nearly(keys.last().value, 120 * s), qPrintable(QString::number(keys.last().value)));
    // (a brand-new project's first ratio pick is its setup, not an undo step — P127 covers undo)
}


namespace {
// Bright pixels in the picture at a time (every other pixel), with the bundled fonts loaded.
int veditLitPixels(const drift::Project *project, drift::TimeUs at)
{
    reloadFontCatalog({QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.korean/fonts"),
                       QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.essentials/fonts")});
    FrameCompositor comp;
    comp.setProject(project);
    const QImage img = comp.compositeAt(at);
    int lit = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2)
            if (qGray(img.pixel(x, y)) > 150)
                ++lit;
    return lit;
}

bool veditFindText(const drift::Project &p, int *t, int *c)
{
    for (int ti = 0; ti < p.tracks().size(); ++ti)
        for (int ci = 0; ci < p.tracks().at(ti).clips.size(); ++ci)
            if (p.tracks().at(ti).clips.at(ci).type == drift::ClipType::Text) {
                *t = ti;
                *c = ci;
                return true;
            }
    return false;
}
} // namespace

// VEDIT P140: CapCut's 기본 텍스트 — one press puts plain words in the middle of the picture at the
// playhead; they are on the picture at once (no grow-in), in a box that hugs them.
void EditorStateTest::parity_P140_defaultTextShowsAtOnce()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 10.0, drift::ClipType::Video);
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(2.0));
    f.state.addDefaultText();
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    const drift::Clip &clip = f.state.project()->tracks().at(t).clips.at(c);
    QCOMPARE(clip.textContent, QStringLiteral("Default text"));
    QVERIFY(!clip.textStyle.animation.isActive());
    QVERIFY(nearly(drift::usToSeconds(clip.timelineStart), 2.0));
    const double W = f.state.project()->width(), H = f.state.project()->height();
    const double x = clip.transformX.keyframes().first().value, w = clip.transformW.keyframes().first().value;
    const double y = clip.transformY.keyframes().first().value, h = clip.transformH.keyframes().first().value;
    QVERIFY2(w < W * 0.8, qPrintable(QStringLiteral("box %1 wide").arg(w)));
    QVERIFY2(std::abs(x + w / 2 - W / 2) < 2 && std::abs(y + h / 2 - H / 2) < 2, qPrintable(layoutText(*f.state.project())));
    ParityFixture g(1);
    g.ready();
    g.state.addDefaultText();
    QVERIFY(veditLitPixels(g.state.project(), g.state.playheadUs()) > 200);
}

// VEDIT P141: a style that grows in ("큰 제목") — after adding it, the playhead stands past the
// entrance, so the words are on the picture while they are typed (they were invisible before).
void EditorStateTest::parity_P141_growingStyleVisibleWhenAdded()
{
    ParityFixture f(1);
    f.ready();
    f.state.addTextClip(QString(), 0.0, QStringLiteral("ko-title"));
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    f.state.setTextBasics(t, c, {{QStringLiteral("text"), QStringLiteral("안녕하세요")}});
    QVERIFY2(f.state.playheadUs() > 0, "playhead stayed on the invisible first frame");
    QVERIFY(veditLitPixels(f.state.project(), f.state.playheadUs()) > 200);
}

// VEDIT P142: 테두리 · 배경 · 글로우 · 그림자 · 곡선 · 굵게 · 밑줄 — each one switches on with its colour and
// size, reads back the same, switches off keeping its settings, and one undo takes a whole
// change back.
void EditorStateTest::parity_P142_textEffectsRoundTrip()
{
    ParityFixture f(1);
    f.ready();
    f.state.addDefaultText();
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    f.state.setTextBasics(t, c, {{QStringLiteral("strokeOn"), true}, {QStringLiteral("strokeColor"), QStringLiteral("#ffe62828")},
                                 {QStringLiteral("strokeWidth"), 8},
                                 {QStringLiteral("bgOn"), true}, {QStringLiteral("bgColor"), QStringLiteral("#ffd640")},
                                 {QStringLiteral("bgOpacity"), 0.5},
                                 {QStringLiteral("glowOn"), true}, {QStringLiteral("glowColor"), QStringLiteral("#ff2f9bff")},
                                 {QStringLiteral("shadowOn"), true}, {QStringLiteral("shadowDistance"), 10},
                                 {QStringLiteral("shadowAngle"), -45},
                                 {QStringLiteral("curve"), 40}, {QStringLiteral("bold"), true},
                                 {QStringLiteral("underline"), true}, {QStringLiteral("color"), QStringLiteral("#ffffd640")}});
    QVariantMap b = f.state.textBasics(t, c);
    QCOMPARE(b.value(QStringLiteral("strokeOn")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("strokeColor")).toString(), QStringLiteral("#ffe62828"));
    QVERIFY(nearly(b.value(QStringLiteral("strokeWidth")).toDouble(), 8.0));
    QCOMPARE(b.value(QStringLiteral("bgOn")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("bgColor")).toString(), QStringLiteral("#ffd640"));
    QVERIFY(std::abs(b.value(QStringLiteral("bgOpacity")).toDouble() - 0.5) < 0.01);
    QCOMPARE(b.value(QStringLiteral("glowOn")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("glowColor")).toString(), QStringLiteral("#ff2f9bff"));
    QCOMPARE(b.value(QStringLiteral("shadowOn")).toBool(), true);
    QVERIFY(nearly(b.value(QStringLiteral("shadowDistance")).toDouble(), 10.0));
    QVERIFY(nearly(b.value(QStringLiteral("shadowAngle")).toDouble(), -45.0));
    QVERIFY(nearly(b.value(QStringLiteral("curve")).toDouble(), 40.0));
    QCOMPARE(b.value(QStringLiteral("bold")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("underline")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffd640"));
    // shadow down-right, drawn first; glow before the outline; outline before the fill
    const drift::TextStyle &s = f.state.project()->tracks().at(t).clips.at(c).textStyle;
    QCOMPARE(s.layers.first().kind, drift::TextLayerKind::Shadow);
    QVERIFY(s.layers.first().offsetX > 6 && s.layers.first().offsetY > 6);
    QCOMPARE(s.layers.last().kind, drift::TextLayerKind::Fill);
    // off keeps the settings for next time
    f.state.setTextBasics(t, c, {{QStringLiteral("strokeOn"), false}});
    b = f.state.textBasics(t, c);
    QCOMPARE(b.value(QStringLiteral("strokeOn")).toBool(), false);
    QCOMPARE(b.value(QStringLiteral("strokeColor")).toString(), QStringLiteral("#ffe62828"));
    f.state.undo(); // outline back on
    QCOMPARE(f.state.textBasics(t, c).value(QStringLiteral("strokeOn")).toBool(), true);
    f.state.undo(); // the whole first change gone in one step
    b = f.state.textBasics(t, c);
    QCOMPARE(b.value(QStringLiteral("strokeOn")).toBool(), false);
    QCOMPARE(b.value(QStringLiteral("bgOn")).toBool(), false);
    QCOMPARE(b.value(QStringLiteral("glowOn")).toBool(), false);
    QCOMPARE(b.value(QStringLiteral("shadowOn")).toBool(), false);
    QCOMPARE(b.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffffff"));
}

// VEDIT P143: ready-made looks, place and turn — "black on yellow" sets the colour and a yellow
// box and takes the outline off; X/Y are from the picture's centre (up is +) and read back;
// 왼쪽 puts the box 5% from the left; longer words widen the box around the same centre;
// ABC makes capitals.
void EditorStateTest::parity_P143_looksPlaceAndTyping()
{
    ParityFixture f(1);
    f.ready();
    f.state.addDefaultText();
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    QVERIFY(f.state.textLookPresets().size() >= 20);
    f.state.setTextBasics(t, c, {{QStringLiteral("strokeOn"), true}});
    f.state.applyReadyTextLook(t, c, QStringLiteral("blackOnYellow"));
    QVariantMap b = f.state.textBasics(t, c);
    QCOMPARE(b.value(QStringLiteral("color")).toString(), QStringLiteral("#ff111111"));
    QCOMPARE(b.value(QStringLiteral("bgOn")).toBool(), true);
    QCOMPARE(b.value(QStringLiteral("bgColor")).toString(), QStringLiteral("#ffd640"));
    QCOMPARE(b.value(QStringLiteral("strokeOn")).toBool(), false);

    f.state.setTextBasics(t, c, {{QStringLiteral("posX"), 300}, {QStringLiteral("posY"), 200},
                                 {QStringLiteral("rotation"), 15}, {QStringLiteral("opacity"), 0.5}});
    b = f.state.textBasics(t, c);
    QVERIFY(nearly(b.value(QStringLiteral("posX")).toDouble(), 300));
    QVERIFY(nearly(b.value(QStringLiteral("posY")).toDouble(), 200));
    QVERIFY(nearly(b.value(QStringLiteral("rotation")).toDouble(), 15));
    QVERIFY(nearly(b.value(QStringLiteral("opacity")).toDouble(), 0.5));

    f.state.alignTextOnCanvas(t, c, QStringLiteral("left"));
    const double W = f.state.project()->width();
    QVERIFY(nearly(f.state.project()->tracks().at(t).clips.at(c).transformX.keyframes().first().value, W * 0.05));

    f.state.alignTextOnCanvas(t, c, QStringLiteral("hcenter"));
    const drift::Clip *clip = &f.state.project()->tracks().at(t).clips.at(c);
    const double w0 = clip->transformW.keyframes().first().value;
    const double cx0 = clip->transformX.keyframes().first().value + w0 / 2;
    f.state.setTextBasics(t, c, {{QStringLiteral("text"), QStringLiteral("Happy birthday to you")}});
    clip = &f.state.project()->tracks().at(t).clips.at(c);
    const double w1 = clip->transformW.keyframes().first().value;
    QVERIFY2(w1 > w0, qPrintable(QStringLiteral("%1 -> %2").arg(w0).arg(w1)));
    QVERIFY(std::abs(clip->transformX.keyframes().first().value + w1 / 2 - cx0) < 2);
    f.state.setTextBasics(t, c, {{QStringLiteral("textCase"), QStringLiteral("upper")}});
    QCOMPARE(f.state.project()->tracks().at(t).clips.at(c).textContent, QStringLiteral("HAPPY BIRTHDAY TO YOU"));
}

// VEDIT P144: 말풍선 — every bubble draws (the picture lights up more than the bare words), puts
// its shape, colours and outline on the text, survives save/open, re-hugs the box (a speech
// bubble's tail makes it taller), and one undo / "no bubble" takes it off.
void EditorStateTest::parity_P144_speechBubbles()
{
    ParityFixture f(1);
    f.ready();
    f.state.addDefaultText();
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    const int bare = veditLitPixels(f.state.project(), f.state.playheadUs());
    const double h0 = f.state.project()->tracks().at(t).clips.at(c).transformH.keyframes().first().value;
    const QVariantList bubbles = f.state.textBubbles();
    QVERIFY(bubbles.size() >= 17);
    for (const QVariant &v : bubbles) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (id == QLatin1String("bubble-none") || id.contains(QLatin1String("black")) || id.contains(QLatin1String("dark"))
            || id.contains(QLatin1String("navy")) || id.contains(QLatin1String("red")) || id.contains(QLatin1String("green")))
            continue; // dark bubbles light fewer pixels than white words; the rest must light more
        f.state.applyTextBubble(t, c, id);
        const int lit = veditLitPixels(f.state.project(), f.state.playheadUs());
        QVERIFY2(lit > bare, qPrintable(QStringLiteral("%1: %2 <= %3").arg(id).arg(lit).arg(bare)));
    }
    f.state.applyTextBubble(t, c, QStringLiteral("bubble-speech-white"));
    const drift::TextStyle &s = f.state.project()->tracks().at(t).clips.at(c).textStyle;
    QVERIFY(s.boxEnabled);
    QCOMPARE(s.bubbleShape, QStringLiteral("speech"));
    QVERIFY(s.bubbleBorderWidth > 0);
    QCOMPARE(s.layers.size(), 1);
    QVERIFY(f.state.project()->tracks().at(t).clips.at(c).transformH.keyframes().first().value > h0 * 1.2);
    const drift::TextStyle back = drift::textStyleFromJson(drift::textStyleToJson(s));
    QCOMPARE(back.bubbleShape, s.bubbleShape);
    QCOMPARE(back.bubbleBorderColor, s.bubbleBorderColor);
    QVERIFY(nearly(back.bubbleBorderWidth, s.bubbleBorderWidth));
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(t).clips.at(c).textStyle.bubbleShape,
             QString()); // the one before (the signboard, a plain box) — one step back
    f.state.applyTextBubble(t, c, QStringLiteral("bubble-none"));
    QVERIFY(!f.state.project()->tracks().at(t).clips.at(c).textStyle.boxEnabled);
    for (const QString &shape : drift::veditBubbleShapes())
        QVERIFY2(!drift::veditBubblePath(shape, QRectF(0, 0, 300, 80), 10).boundingRect().isEmpty(), qPrintable(shape));
}

// VEDIT P145: 편집효과 — every word art draws, a 3D look puts an extrude layer sized to the
// letters (97 px words: depth ~1.5× the 64 px look's), word art drops a bubble, and "no effect"
// leaves plain words in their colour.
void EditorStateTest::parity_P145_wordArt()
{
    ParityFixture f(1);
    f.ready();
    f.state.addDefaultText();
    int t = -1, c = -1;
    QVERIFY(veditFindText(*f.state.project(), &t, &c));
    const QVariantList arts = f.state.textWordArts();
    QVERIFY(arts.size() >= 25);
    for (const QVariant &v : arts) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        f.state.applyTextWordArt(t, c, id);
        int lit = 0;
        {
            reloadFontCatalog({QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.korean/fonts"),
                               QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.essentials/fonts")});
            FrameCompositor comp;
            comp.setProject(f.state.project());
            const QImage img = comp.compositeAt(f.state.playheadUs());
            for (int y = 0; y < img.height(); y += 2)
                for (int x = 0; x < img.width(); x += 2)
                    if (qGray(img.pixel(x, y)) > 40)
                        ++lit;
        }
        QVERIFY2(lit > 200, qPrintable(QStringLiteral("%1 drew %2").arg(id).arg(lit)));
    }
    f.state.applyTextBubble(t, c, QStringLiteral("bubble-oval"));
    f.state.applyTextWordArt(t, c, QStringLiteral("art-3d-red"));
    const drift::TextStyle &s = f.state.project()->tracks().at(t).clips.at(c).textStyle;
    QVERIFY(!s.boxEnabled);
    const drift::TextShadingLayer *ex = drift::firstTextLayerOfKind(s.layers, drift::TextLayerKind::Extrude);
    QVERIFY(ex);
    QVERIFY2(nearly(ex->width, 10.0 * s.pixelSize / 64.0), qPrintable(QString::number(ex->width)));
    f.state.applyTextWordArt(t, c, QStringLiteral("art-none"));
    const drift::TextStyle &plain = f.state.project()->tracks().at(t).clips.at(c).textStyle;
    QCOMPARE(plain.layers.size(), 1);
    QCOMPARE(plain.layers.first().kind, drift::TextLayerKind::Fill);
    QCOMPARE(plain.layers.first().paint.color, QColor(255, 75, 75));
}

// VEDIT P146: 스냅은 화면 거리(약 18px)로 — 1초가 100px이면 0.18초, 많이 축소(1초 10px)해도 최대
// 1.5초, 많이 확대(1초 2000px)하면 1/60초까지. 그 거리 안의 장면 끝에만 달라붙는다.
void EditorStateTest::parity_P146_snapReachFollowsZoom()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.ready();
    const auto restore = qScopeGuard([] { drift::setSnapThresholdUs(drift::kSnapThresholdUs); });
    f.state.setSnapPixelsPerSecond(100.0);
    QVERIFY(nearly(f.state.snapReachSeconds(), 0.18));
    f.state.setSnapPixelsPerSecond(10.0);
    QVERIFY(nearly(f.state.snapReachSeconds(), 1.5));
    f.state.setSnapPixelsPerSecond(2000.0);
    QVERIFY(std::abs(f.state.snapReachSeconds() - 1.0 / 60.0) < 0.001);
    f.state.setSnapEnabled(true);
    f.state.setSnapPixelsPerSecond(100.0);
    QVERIFY(nearly(f.state.snapTime(4.15), 4.0));  // 0.15 s away: within 18 px
    QVERIFY(nearly(f.state.snapTime(4.3), 4.3));   // 0.3 s away: stays put
}

// VEDIT P147 (내보내기 점검 3회차): a red picture fading in, dragged to overlap a blue one fading
// out — exported, the file is 3.5 s, black at the very start, red in the middle of the first,
// red and blue mixed in the middle of the 0.5 s overlap, blue after it, and dark again at the end.
void EditorStateTest::parity_P147_exportedDissolveAndFades()
{
#ifndef Q_OS_WIN
    QSKIP("Windows encoders");
#else
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 2.0, drift::ClipType::Shape);
    f.add(0, QStringLiteral("b"), 2.0, 2.0, drift::ClipType::Shape);
    f.ready();
    const double W = f.state.project()->width(), H = f.state.project()->height();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            c.shapeStyle.kind = drift::ShapeKind::Rectangle;
            c.shapeStyle.setSolidFill(c.id == QStringLiteral("a") ? QColor(230, 20, 20) : QColor(20, 20, 230));
            c.shapeStyle.setStroke(0.0);
            c.transformX = {};
            c.transformY = {};
            c.transformW = {};
            c.transformH = {};
            c.transformX.setKeyframe(0, 0.0);
            c.transformY.setKeyframe(0, 0.0);
            c.transformW.setKeyframe(0, W);
            c.transformH.setKeyframe(0, H);
            if (c.id == QStringLiteral("a"))
                c.fadeInUs = drift::secondsToUs(0.5);
            else
                c.fadeOutUs = drift::secondsToUs(0.5);
        }
    f.state.moveClip(0, f.indexOf(0, QStringLiteral("b")), 1.5); // the user's drag: 0.5 s overlap
    QCOMPARE(f.state.project()->tracks().at(0).transitions.size(), 1);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 360;
    const QString out = dir.filePath(QStringLiteral("dissolve.mp4"));
    QString error;
    bool ok = false;
    const drift::Project project = *f.state.project();
    QScopedPointer<QThread> worker(QThread::create([&] { ok = Exporter::run(project, settings, out, &error); }));
    worker->start();
    QVERIFY(worker->wait(180000));
    QVERIFY2(ok, qPrintable(error));
    const MediaInfo info = MediaProbe::probe(out);
    QVERIFY(info.ok);
    QVERIFY2(std::abs(drift::usToSeconds(info.durationUs) - 3.5) < 0.15,
             qPrintable(QStringLiteral("length %1").arg(drift::usToSeconds(info.durationUs))));

    ClipReader reader;
    QVERIFY(reader.open(out));
    const auto centre = [&](double at) {
        QImage frame;
        if (!reader.readVideoFrameAt(drift::secondsToUs(at), frame, 640, 360))
            return QColor();
        return QColor(frame.convertToFormat(QImage::Format_RGB32).pixel(frame.width() / 2, frame.height() / 2));
    };
    const QString log = QStringLiteral("0.02:%1 1.0:%2 1.75:%3 2.6:%4 3.47:%5")
                            .arg(centre(0.02).name(), centre(1.0).name(), centre(1.75).name(), centre(2.6).name(),
                                 centre(3.47).name());
    const QColor start = centre(0.02), red = centre(1.0), mix = centre(1.75), blue = centre(2.6), end = centre(3.47);
    QVERIFY2(start.red() < 90, qPrintable(log));                          // fading in from black
    QVERIFY2(red.red() > 180 && red.blue() < 70, qPrintable(log));       // the red picture
    QVERIFY2(mix.red() > 60 && mix.blue() > 60, qPrintable(log));        // both, mid-dissolve
    QVERIFY2(blue.blue() > 180 && blue.red() < 70, qPrintable(log));     // the blue picture
    QVERIFY2(end.blue() < 110, qPrintable(log));                          // fading out to black
#endif
}

// VEDIT P148 (내보내기 점검 5회차): a one-minute film as people make it — a photo slowly coming
// closer (천천히 다가가기) for 5 s, captions imported from an .srt file, then a minute of a title.
// Exported at 720p: the captions are on the picture at the bottom, the photo really grows, the
// file is 60 s, and the export time is reported (and must stay under three minutes).
void EditorStateTest::parity_P148_exportedCaptionsMotionAndLength()
{
#ifndef Q_OS_WIN
    QSKIP("Windows encoders");
#else
    reloadFontCatalog({QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.korean/fonts"),
                       QStringLiteral(VEDIT_TEST_BUNDLED_DIR "/fonts.essentials/fonts")});
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // a checkerboard photo: its squares get wider as the picture comes closer
    QImage photo(1920, 1080, QImage::Format_RGB32);
    for (int y = 0; y < photo.height(); ++y)
        for (int x = 0; x < photo.width(); ++x)
            photo.setPixel(x, y, ((x / 60) + (y / 60)) % 2 ? qRgb(240, 240, 240) : qRgb(30, 90, 200));
    const QString photoPath = dir.filePath(QStringLiteral("board.png"));
    QVERIFY(photo.save(photoPath));
    const QString srt = dir.filePath(QStringLiteral("words.srt"));
    {
        QFile f(srt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("1\n00:00:06,000 --> 00:00:09,000\n오늘은 가족 여행 날\n\n").toUtf8());
    }

    ParityFixture f(1);
    f.add(0, QStringLiteral("photo"), 0.0, 5.0, drift::ClipType::Image);
    f.add(0, QStringLiteral("rest"), 5.0, 55.0, drift::ClipType::Shape);
    f.ready();
    const double W = f.state.project()->width(), H = f.state.project()->height();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            c.transformX = {};
            c.transformY = {};
            c.transformW = {};
            c.transformH = {};
            c.transformX.setKeyframe(0, 0.0);
            c.transformY.setKeyframe(0, 0.0);
            c.transformW.setKeyframe(0, W);
            c.transformH.setKeyframe(0, H);
            if (c.id == QStringLiteral("photo"))
                c.path = photoPath;
            else {
                c.shapeStyle.kind = drift::ShapeKind::Rectangle;
                c.shapeStyle.setSolidFill(QColor(20, 70, 30));
                c.shapeStyle.setStroke(0.0);
            }
        }
    f.state.applyClipMotion(0, f.indexOf(0, QStringLiteral("photo")), QStringLiteral("zoomIn"));
    QVERIFY(f.state.importSubtitleFile(QUrl::fromLocalFile(srt), 0.0));

    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 720;
    const QString out = dir.filePath(QStringLiteral("minute.mp4"));
    QString error;
    bool ok = false;
    const drift::Project project = *f.state.project();
    QElapsedTimer timer;
    timer.start();
    QScopedPointer<QThread> worker(QThread::create([&] { ok = Exporter::run(project, settings, out, &error); }));
    worker->start();
    QVERIFY(worker->wait(600000));
    const double took = timer.elapsed() / 1000.0;
    QVERIFY2(ok, qPrintable(error));
    const MediaInfo info = MediaProbe::probe(out);
    QVERIFY(info.ok);
    qInfo("VEDIT 1-minute export: %.1f s of film in %.1f s (%.1fx real time), %.1f MB",
          drift::usToSeconds(info.durationUs), took, drift::usToSeconds(info.durationUs) / qMax(0.1, took),
          QFileInfo(out).size() / 1e6);
    QVERIFY2(std::abs(drift::usToSeconds(info.durationUs) - 60.0) < 0.2,
             qPrintable(QString::number(drift::usToSeconds(info.durationUs))));
    QVERIFY2(took < 180.0, qPrintable(QStringLiteral("a minute took %1 s to export").arg(took)));

    ClipReader reader;
    QVERIFY(reader.open(out));
    const auto frameAt = [&](double at) {
        QImage img;
        reader.readVideoFrameAt(drift::secondsToUs(at), img, 1280, 720);
        return img.convertToFormat(QImage::Format_RGB32);
    };
    // the photo grows: fewer square edges across the middle row at the end than at the start
    const auto edges = [](const QImage &img) {
        int n = 0;
        const int y = img.height() / 2;
        for (int x = 1; x < img.width(); ++x)
            if (std::abs(qGray(img.pixel(x, y)) - qGray(img.pixel(x - 1, y))) > 60)
                ++n;
        return n;
    };
    const int early = edges(frameAt(0.2)), late = edges(frameAt(4.8));
    QVERIFY2(early > 0 && late < early, qPrintable(QStringLiteral("square edges %1 -> %2").arg(early).arg(late)));
    // the caption: white letters in the bottom band while it shows, none before it
    const auto whiteBottom = [](const QImage &img) {
        int n = 0;
        for (int y = img.height() * 3 / 4; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGray(img.pixel(x, y)) > 200)
                    ++n;
        return n;
    };
    const int during = whiteBottom(frameAt(7.0)), before = whiteBottom(frameAt(5.5));
    QVERIFY2(during > 300 && before < 50, qPrintable(QStringLiteral("caption white %1 (before %2)").arg(during).arg(before)));
#endif
}

// VEDIT P149: 기본 파일 이름 '월일년_ECEG' — a first save of a new project is 101626_ECEG.drift (today's
// month, day and two-digit year); a second new project the same day is 101626_ECEG_2.drift.
void EditorStateTest::parity_P149_defaultNameIsDateEceg()
{
    QDir(AppController::s_autoSaveDirOverride).removeRecursively();
    const QString today = QDate::currentDate().toString(QStringLiteral("MMddyy")) + QStringLiteral("_ECEG");
    QStringList names;
    for (int i = 0; i < 2; ++i) {
        ParityFixture f(1);
        f.add(0, QStringLiteral("a"), 0.0, 2.0);
        f.ready();
        f.state.setAutoSaveEnabled(true);
        f.state.moveClip(0, f.indexOf(0, QStringLiteral("a")), 1.0); // an edit, so there is something to save
        QVERIFY(f.state.autoSaveNow());
        names << QFileInfo(f.state.currentProjectPath()).fileName();
    }
    QCOMPARE(names.at(0), today + QStringLiteral(".drift"));
    QCOMPARE(names.at(1), today + QStringLiteral("_2.drift"));
    QDir(AppController::s_autoSaveDirOverride).removeRecursively();
}

// VEDIT P150: 앞 끝을 원본보다 더 끌어도 최대한 열린 채로 — a clip using its footage from 2 s, its start
// pulled 4 s to the left: it opens the 2 s there are (footage from 0, 5 s long) and stays opened
// when let go, instead of snapping back. Pulling further once it is fully open changes nothing.
void EditorStateTest::parity_P150_trimPastFootageStopsAtItsStart()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 5.0, 3.0, drift::ClipType::Video);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            c.srcIn = drift::secondsToUs(2.0);
            c.srcOut = drift::secondsToUs(5.0);
        }
    const int i = f.indexOf(0, QStringLiteral("a"));
    f.state.beginTrimGesture(0, i, -1);
    f.state.commitTrim(0, i, -1, 1.0); // let go 4 s to the left: 2 s further than the footage goes
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    QVERIFY2(nearly(drift::usToSeconds(a->srcIn), 0.0), qPrintable(QString::number(drift::usToSeconds(a->srcIn))));
    QVERIFY2(nearly(drift::usToSeconds(a->timelineDuration), 5.0),
             qPrintable(QString::number(drift::usToSeconds(a->timelineDuration))));
    const double startAfter = drift::usToSeconds(a->timelineStart);
    f.state.beginTrimGesture(0, i, -1);
    f.state.commitTrim(0, i, -1, 0.0); // already fully open: nothing more to give
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), startAfter));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 5.0));
}

// VEDIT P151: 뒤 끝도 같게 — footage 5 s long, the clip uses 1–3 s; its end pulled out to 10 s on the
// timeline: it opens to the end of the footage (1–5 s, 4 s long) and stays open when let go.
// Pulled again once fully open, nothing changes.
void EditorStateTest::parity_P151_trimEndPastFootageStopsAtItsEnd()
{
    ParityFixture f(1);
    drift::MediaAsset asset;
    asset.id = QStringLiteral("src");
    asset.name = QStringLiteral("five.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/nonexistent/five.mp4");
    asset.durationUs = drift::secondsToUs(5.0);
    asset.width = 1920;
    asset.height = 1080;
    f.state.project()->addAsset(asset);
    f.add(0, QStringLiteral("a"), 0.0, 2.0, drift::ClipType::Video);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips) {
            c.assetId = QStringLiteral("src");
            c.srcIn = drift::secondsToUs(1.0);
            c.srcOut = drift::secondsToUs(3.0);
        }
    const int i = f.indexOf(0, QStringLiteral("a"));
    f.state.beginTrimGesture(0, i, 1);
    f.state.commitTrim(0, i, 1, 10.0); // let go far past the end of the footage
    const drift::Clip *a = f.clip(0, QStringLiteral("a"));
    QVERIFY2(nearly(drift::usToSeconds(a->srcOut), 5.0), qPrintable(QString::number(drift::usToSeconds(a->srcOut))));
    QVERIFY2(nearly(drift::usToSeconds(a->srcIn), 1.0), qPrintable(QString::number(drift::usToSeconds(a->srcIn))));
    QVERIFY2(nearly(drift::usToSeconds(a->timelineDuration), 4.0),
             qPrintable(QString::number(drift::usToSeconds(a->timelineDuration))));
    QVERIFY(nearly(drift::usToSeconds(a->timelineStart), 0.0));
    f.state.beginTrimGesture(0, i, 1);
    f.state.commitTrim(0, i, 1, 12.0); // fully open already
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineDuration), 4.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->srcOut), 5.0));
}

// VEDIT P152: 겹치면 자동 디졸브 — pulling a clip's start left over the clip before it (ripple on or
// off) leaves the 1 s overlap as a curved dissolve, never a bare overlap.
void EditorStateTest::parity_P152_extendIntoNeighbourMakesDissolve()
{
    for (const bool ripple : {true, false}) {
        ParityFixture f(1);
        f.add(0, QStringLiteral("a"), 0.0, 3.0, drift::ClipType::Video);
        f.add(0, QStringLiteral("b"), 3.0, 3.0, drift::ClipType::Video);
        f.ready();
        f.state.setRippleEnabled(ripple);
        for (drift::Track &t : f.state.project()->tracks())
            for (drift::Clip &c : t.clips)
                if (c.id == QStringLiteral("b")) {
                    c.srcIn = drift::secondsToUs(2.0);
                    c.srcOut = drift::secondsToUs(5.0);
                }
        const int i = f.indexOf(0, QStringLiteral("b"));
        f.state.beginTrimGesture(0, i, -1);
        f.state.commitTrim(0, i, -1, 2.0);
        const drift::Track &t = f.state.project()->tracks().at(0);
        QCOMPARE(t.transitions.size(), 1);
        QCOMPARE(t.transitions.first().kindId, QStringLiteral("crossfade"));
        QVERIFY(nearly(drift::usToSeconds(t.transitions.first().durationUs), 1.0));
    }
}

// VEDIT P153: 트랙 사이에 새 트랙 — three tracks; the clip on the last one dropped on the seam between
// the first two (the red line) lands alone on a new track inserted there; the others keep their
// clips, one row further down where they were below the seam. One undo puts it all back.
void EditorStateTest::parity_P153_dropOnSeamInsertsTrack()
{
    ParityFixture f(3);
    f.add(0, QStringLiteral("top"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("mid"), 0.0, 4.0, drift::ClipType::Video);
    f.add(2, QStringLiteral("low"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    const int before = f.state.project()->tracks().size();
    f.state.dropClip(2, f.indexOf(2, QStringLiteral("low")), -1, 1.0, 1, false);
    const drift::Project &p = *f.state.project();
    QCOMPARE(p.tracks().size(), before + 1); // a new track (tests keep emptied tracks; the app tidies them)
    int lowTrack = -1;
    QVERIFY(clipById(p, QStringLiteral("low"), &lowTrack));
    QCOMPARE(lowTrack, 1);
    QCOMPARE(p.tracks().at(1).clips.size(), 1);
    int midTrack = -1, topTrack = -1;
    clipById(p, QStringLiteral("mid"), &midTrack);
    clipById(p, QStringLiteral("top"), &topTrack);
    QCOMPARE(topTrack, 0);
    QCOMPARE(midTrack, 2);
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("low"))->timelineStart), 1.0));
    f.state.undo();
    int back = -1;
    clipById(*f.state.project(), QStringLiteral("low"), &back);
    QCOMPARE(back, 2);
}

// VEDIT P154: 합성 해제 — two clips made into a composite, then undone: both come back at the same
// times on their own tracks, the composite is gone, and one undo restores the composite.
void EditorStateTest::parity_P154_ungroupCompositeRestoresClips()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 2.0, 3.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 3.0, 2.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("a")));
    f.state.addToSelection(1, f.indexOf(1, QStringLiteral("b")));
    QVERIFY(f.state.canMakeCompositeFromSelection());
    f.state.makeCompositeFromSelection();
    int ct = -1, cc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Composite) {
                ct = t;
                cc = c;
            }
    QVERIFY(ct >= 0);
    QVERIFY(f.state.ungroupComposite(ct, cc));
    const drift::Project &p = *f.state.project();
    int composites = 0;
    QList<double> starts;
    for (const drift::Track &t : p.tracks())
        for (const drift::Clip &c : t.clips) {
            if (c.type == drift::ClipType::Composite)
                ++composites;
            else
                starts << drift::usToSeconds(c.timelineStart);
        }
    QCOMPARE(composites, 0);
    std::sort(starts.begin(), starts.end());
    QCOMPARE(starts.size(), 2);
    QVERIFY(nearly(starts.at(0), 2.0));
    QVERIFY(nearly(starts.at(1), 3.0));
    f.state.undo();
    int back = 0;
    for (const drift::Track &t : f.state.project()->tracks())
        for (const drift::Clip &c : t.clips)
            back += c.type == drift::ClipType::Composite ? 1 : 0;
    QCOMPARE(back, 1);
}

// VEDIT P155: 합성 해제와 이웃 — on one row: L (0–2), a composite made of a 2–5 clip, R (5–7). Undone,
// the clip is back on that same row between L and R, with no overlap and no gap, L and R untouched.
// A composite trimmed shorter than what is inside gives back only the part that was in use.
void EditorStateTest::parity_P155_ungroupKeepsNeighboursClear()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("L"), 0.0, 2.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("m"), 2.0, 3.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("R"), 5.0, 2.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("m")));
    f.state.makeCompositeFromSelection();
    int ct = -1, cc = -1;
    for (int t = 0; t < f.state.project()->tracks().size(); ++t)
        for (int c = 0; c < f.state.project()->tracks().at(t).clips.size(); ++c)
            if (f.state.project()->tracks().at(t).clips.at(c).type == drift::ClipType::Composite) {
                ct = t;
                cc = c;
            }
    QVERIFY(ct >= 0);
    // trim the composite's end by 1 s: only 2–4 is in use now (ripple pulls R back to 4)
    f.state.trimClipRight(ct, cc, 4.0);
    const double rStart = drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("R"))->timelineStart);
    QVERIFY(f.state.ungroupComposite(ct, cc));
    const drift::Project &p = *f.state.project();
    int lt = -1, rt = -1;
    const drift::Clip *L = clipById(p, QStringLiteral("L"), &lt);
    const drift::Clip *R = clipById(p, QStringLiteral("R"), &rt);
    QVERIFY(L && R);
    QCOMPARE(lt, rt);
    const drift::Track &row = p.tracks().at(lt);
    QCOMPARE(row.clips.size(), 3);
    for (int i = 0; i + 1 < row.clips.size(); ++i)
        QVERIFY2(row.clips.at(i).timelineEnd() <= row.clips.at(i + 1).timelineStart,
                 qPrintable(layoutText(p)));
    const drift::Clip *mid = nullptr;
    for (const drift::Clip &c : row.clips)
        if (c.id != QStringLiteral("L") && c.id != QStringLiteral("R"))
            mid = &c;
    QVERIFY(mid);
    QVERIFY2(nearly(drift::usToSeconds(mid->timelineStart), 2.0), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(mid->timelineDuration), 2.0), qPrintable(layoutText(p)));
    QVERIFY(nearly(drift::usToSeconds(L->timelineStart), 0.0));
    QVERIFY(nearly(drift::usToSeconds(R->timelineStart), rStart));
    QVERIFY(row.transitions.isEmpty());
}

// VEDIT P156: 영상 트랙은 언제나 소리 트랙 위 — tracks: video, video, audio, audio. A new video track
// asked for at the very bottom lands just above the first sound track; a new sound track asked
// for at the very top lands just below the last picture track; a video clip dropped on a seam
// between the two sound tracks opens its track above them.
void EditorStateTest::parity_P156_picturesStayAboveSounds()
{
    ParityFixture f(4);
    f.add(0, QStringLiteral("v1"), 0.0, 2.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("v2"), 0.0, 2.0, drift::ClipType::Video);
    f.add(2, QStringLiteral("a1"), 0.0, 2.0, drift::ClipType::Audio);
    f.add(3, QStringLiteral("a2"), 0.0, 2.0, drift::ClipType::Audio);
    f.ready();
    drift::Project &p = *f.state.project();
    p.tracks()[0].type = p.tracks()[1].type = drift::TrackType::Video;
    p.tracks()[2].type = p.tracks()[3].type = drift::TrackType::Audio;
    QCOMPARE(drift::orderedTrackInsertIndex(p, drift::TrackType::Video, 4), 2);
    QCOMPARE(drift::orderedTrackInsertIndex(p, drift::TrackType::Audio, 0), 2);
    QCOMPARE(drift::orderedTrackInsertIndex(p, drift::TrackType::Video, 1), 1);
    QCOMPARE(drift::orderedTrackInsertIndex(p, drift::TrackType::Audio, 3), 3);
    f.state.dropClip(0, f.indexOf(0, QStringLiteral("v1")), -1, 0.0, 3, false); // aimed between a1 and a2
    int vt = -1;
    clipById(*f.state.project(), QStringLiteral("v1"), &vt);
    const QList<drift::Track> &t = f.state.project()->tracks();
    QCOMPARE(t.at(vt).type, drift::TrackType::Video);
    for (int i = 0; i < t.size(); ++i)
        for (int j = i + 1; j < t.size(); ++j)
            QVERIFY2(!(t.at(i).type == drift::TrackType::Audio && t.at(j).type != drift::TrackType::Audio),
                     qPrintable(QStringLiteral("a sound track above a picture track at %1/%2").arg(i).arg(j)));
}

namespace {
// 16-bit mono WAV of `pcm` at `sr`.
void veditWriteWav(const QString &path, const std::vector<float> &pcm, int sr)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QDataStream d(&f);
    d.setByteOrder(QDataStream::LittleEndian);
    const quint32 bytes = quint32(pcm.size() * 2);
    f.write("RIFF");
    d << quint32(36 + bytes);
    f.write("WAVEfmt ");
    d << quint32(16) << quint16(1) << quint16(1) << quint32(sr) << quint32(sr * 2) << quint16(2) << quint16(16);
    f.write("data");
    d << bytes;
    for (float v : pcm)
        d << qint16(qBound(-32767, int(std::lround(v * 32767.0f)), 32767));
}

// A room heard by a camera: speech-like bursts, cut from `from` for `seconds`, at `gain`, with hiss.
std::vector<float> veditRoomTake(double from, double seconds, double gain, double hiss, unsigned seed = 11,
                                 unsigned micSeed = 0)
{
    constexpr int sr = 16000;
    std::mt19937 r(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::normal_distribution<float> n(0.0f, 1.0f);
    std::vector<float> scene(size_t(40.0 * sr));
    size_t t = 0;
    while (t < scene.size()) {
        const size_t len = size_t((0.08 + 0.32 * u(r)) * sr);
        const double amp = 0.2 + 0.5 * u(r);
        for (size_t i = 0; i < len && t + i < scene.size(); ++i)
            scene[t + i] += float(amp * std::sin(3.14159265358979 * double(i) / len) * n(r));
        t += len + size_t((0.05 + 0.55 * u(r)) * sr);
    }
    std::mt19937 hr(seed + 1000 + micSeed); // every microphone has its own hiss
    std::vector<float> take(size_t(seconds * sr));
    for (size_t i = 0; i < take.size(); ++i) {
        const size_t j = size_t(from * sr) + i;
        take[i] = float(gain * (j < scene.size() ? scene[j] : 0.0f) + hiss * n(hr));
    }
    return take;
}
} // namespace

// VEDIT P157: 소리로 맞추기 — two cameras' sound of one room (camera B started 2.5 s later), placed on
// the timeline wrongly (A at 1 s, B at 10 s), selected together (as a marquee or Ctrl does):
// "Line up by sound" moves B to 3.5 s (A stays, the earlier one), in one undo step. A third clip
// from another room is left where it is.
void EditorStateTest::parity_P157_lineUpBySound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = dir.filePath(QStringLiteral("camA.wav")), b = dir.filePath(QStringLiteral("camB.wav")),
                  c = dir.filePath(QStringLiteral("other.wav"));
    veditWriteWav(a, veditRoomTake(0.0, 20.0, 1.0, 0.01), 16000);
    veditWriteWav(b, veditRoomTake(2.5, 15.0, 0.6, 0.03, 11, 5), 16000);
    veditWriteWav(c, veditRoomTake(0.0, 15.0, 1.0, 0.01, 77), 16000);

    ParityFixture f(3);
    f.add(0, QStringLiteral("A"), 1.0, 20.0, drift::ClipType::Audio);
    f.add(1, QStringLiteral("B"), 10.0, 15.0, drift::ClipType::Audio);
    f.add(2, QStringLiteral("C"), 30.0, 15.0, drift::ClipType::Audio);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &clip : t.clips)
            clip.path = clip.id == QStringLiteral("A") ? a : clip.id == QStringLiteral("B") ? b : c;
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("A")));
    f.state.addToSelection(1, f.indexOf(1, QStringLiteral("B")));
    f.state.addToSelection(2, f.indexOf(2, QStringLiteral("C")));
    QVERIFY(f.state.canSyncSelectionByAudio());
    QCOMPARE(f.state.syncSelectionByAudioNow(), 1);
    const drift::Project &p = *f.state.project();
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("A"))->timelineStart), 1.0));
    const double bStart = drift::usToSeconds(clipById(p, QStringLiteral("B"))->timelineStart);
    QVERIFY2(std::abs(bStart - 3.5) < 0.003, qPrintable(QString::number(bStart, 'f', 4)));
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("C"))->timelineStart), 30.0));
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("B"))->timelineStart), 10.0));

    // camera B placed first: it is the earliest so it stays, and A moves to 2.5 s after... B's
    // own sound begins 2.5 s into A's, so A starts 2.5 s before B — the pair slides to keep 0.
    ParityFixture g(2);
    g.add(0, QStringLiteral("A"), 8.0, 20.0, drift::ClipType::Audio);
    g.add(1, QStringLiteral("B"), 0.5, 15.0, drift::ClipType::Audio);
    g.ready();
    for (drift::Track &t : g.state.project()->tracks())
        for (drift::Clip &clip : t.clips)
            clip.path = clip.id == QStringLiteral("A") ? a : b;
    g.state.selectClip(0, g.indexOf(0, QStringLiteral("A")));
    g.state.addToSelection(1, g.indexOf(1, QStringLiteral("B")));
    QCOMPARE(g.state.syncSelectionByAudioNow(), 2);
    const double a2 = drift::usToSeconds(clipById(*g.state.project(), QStringLiteral("A"))->timelineStart);
    const double b2 = drift::usToSeconds(clipById(*g.state.project(), QStringLiteral("B"))->timelineStart);
    QVERIFY2(std::abs((b2 - a2) - 2.5) < 0.003 && a2 >= -1e-6 && std::abs(a2) < 0.003,
             qPrintable(QStringLiteral("A %1  B %2").arg(a2).arg(b2)));
}

// VEDIT P158 (소리로 맞추기 3회차): real camera files — two .mp4 with picture and sound (B began 2.5 s
// later). B has its sound split onto a track below; another clip already sits where B must go
// on B's row. Lined up: B moves to a fresh row just below (no overlap, no dissolve), its split
// sound moves with it, the clip in the way is untouched.
void EditorStateTest::parity_P158_lineUpRealCameras()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto camera = [&](const QString &name, double from, double seconds, double gain) {
        const QString wav = dir.filePath(name + QStringLiteral(".wav"));
        veditWriteWav(wav, veditRoomTake(from, seconds, gain, 0.02, 11, unsigned(qHash(name))), 16000);
        const QString mp4 = dir.filePath(name + QStringLiteral(".mp4"));
        QProcess p;
        p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("testsrc2=size=320x180:rate=25:duration=%1").arg(seconds),
                         QStringLiteral("-i"), wav, QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                         QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), mp4});
        p.waitForFinished(120000);
        return QFileInfo::exists(mp4) ? mp4 : QString();
    };
    const QString a = camera(QStringLiteral("camA"), 0.0, 20.0, 1.0);
    const QString b = camera(QStringLiteral("camB"), 2.5, 15.0, 0.7);
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    ParityFixture f(3);
    f.add(0, QStringLiteral("A"), 0.0, 20.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("B"), 22.0, 15.0, drift::ClipType::Video, QStringLiteral("LB"));
    f.add(1, QStringLiteral("blocker"), 1.0, 5.0, drift::ClipType::Video);
    f.add(2, QStringLiteral("Bs"), 22.0, 15.0, drift::ClipType::Audio, QStringLiteral("LB"));
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &clip : t.clips)
            clip.path = clip.id == QStringLiteral("A") ? a : (clip.id == QStringLiteral("blocker") ? a : b);
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("A")));
    f.state.addToSelection(1, f.indexOf(1, QStringLiteral("B")));
    f.state.addToSelection(2, f.indexOf(2, QStringLiteral("Bs")));
    QCOMPARE(f.state.syncSelectionByAudioNow(), 1); // B and its sound count once
    const drift::Project &p = *f.state.project();
    int bt = -1, kt = -1;
    const drift::Clip *B = clipById(p, QStringLiteral("B"), &bt);
    const drift::Clip *K = clipById(p, QStringLiteral("blocker"), &kt);
    const drift::Clip *Bs = clipById(p, QStringLiteral("Bs"));
    QVERIFY2(std::abs(drift::usToSeconds(B->timelineStart) - 2.5) < 0.01, qPrintable(layoutText(p)));
    QCOMPARE(Bs->timelineStart, B->timelineStart);
    QVERIFY(nearly(drift::usToSeconds(K->timelineStart), 1.0));
    QVERIFY2(bt != kt, "B should have moved off the row its way was blocked on");
    for (const drift::Track &t : p.tracks()) {
        QVERIFY2(t.transitions.isEmpty(), "lining up must not leave a dissolve behind");
        for (int i = 0; i + 1 < t.clips.size(); ++i)
            QVERIFY2(t.clips.at(i).timelineEnd() <= t.clips.at(i + 1).timelineStart, qPrintable(layoutText(p)));
    }
    // pictures still above sounds
    bool seenSound = false;
    for (const drift::Track &t : p.tracks()) {
        if (t.type == drift::TrackType::Audio)
            seenSound = true;
        else
            QVERIFY2(!seenSound, "a picture track ended up below a sound track");
    }
}

// VEDIT P159: 조박사님's own concert — a Sony (C0011, 11 min used) and a DJI (5 min 45 used) of the
// same show, lined up by sound: the DJI began 13.0 s before the Sony, so with the Sony staying
// at 283.18 s the DJI lands at about 270.2 s. Skipped on machines without those files.
void EditorStateTest::parity_P159_lineUpDoctorChosConcert()
{
    const QStringList sony = QDir(QStringLiteral("C:/Users/chome/Videos")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QString sonyPath, djiPath;
    for (const QString &d : sony) {
        const QString s = QStringLiteral("C:/Users/chome/Videos/%1/live/sony/C0011.MP4").arg(d);
        const QString j = QStringLiteral("C:/Users/chome/Videos/%1/live/DJI_001/DJI_20261010170959_0002_D.MP4").arg(d);
        if (QFileInfo::exists(s) && QFileInfo::exists(j)) {
            sonyPath = s;
            djiPath = j;
        }
    }
    if (sonyPath.isEmpty())
        QSKIP("the concert files are not on this machine");
    ParityFixture f(2);
    f.add(0, QStringLiteral("dji"), 496.6114, 345.008247, drift::ClipType::Video);
    f.add(1, QStringLiteral("sony"), 283.184443, 665.588237, drift::ClipType::Video);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks())
        for (drift::Clip &c : t.clips)
            c.path = c.id == QStringLiteral("dji") ? djiPath : sonyPath;
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("dji")));
    f.state.addToSelection(1, f.indexOf(1, QStringLiteral("sony")));
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(f.state.syncSelectionByAudioNow(), 1);
    const double dji = drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("dji"))->timelineStart);
    qInfo("VEDIT concert: DJI moved to %.3f s (expect ~270.2), %lld ms", dji, timer.elapsed());
    // VEDIT: pictures sit on frames now — within half a frame of the sound's match.
    QVERIFY(qAbs(drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("sony"))->timelineStart) - 283.184443) <= 0.5 / 30.0 + 1e-6);
    QVERIFY2(std::abs(dji - (283.184443 - 13.0)) < 0.1, qPrintable(QString::number(dji, 'f', 3)));
}

// VEDIT P160: 빈틈 닫기, per track — a track with it on keeps no gaps whatever the edit (clips placed
// with gaps close up from 0 at the next edit, a dissolve keeps its overlap, a separated sound follows
// its picture); a track with it off is left alone, even with its own gaps. Switching a track on
// closes it at once, in one undo step. A new project has it on the main video track only.
void EditorStateTest::parity_P160_mainTrackMagnet()
{
    AppController::s_mainTrackMagnet = true;
    AppController::s_allTracksMagnet = false;
    const auto restore = qScopeGuard([] {
        AppController::s_mainTrackMagnet = false;
        AppController::s_allTracksMagnet = true;
    });
    ParityFixture f(3);
    f.add(0, QStringLiteral("x"), 5.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("y"), 12.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("a"), 5.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 12.0, 4.0, drift::ClipType::Video, QStringLiteral("LB"));
    f.add(1, QStringLiteral("c"), 15.0, 4.0, drift::ClipType::Video); // 1 s dissolve with b
    f.add(2, QStringLiteral("bs"), 12.0, 4.0, drift::ClipType::Audio, QStringLiteral("LB"));
    f.ready();
    drift::Project &p0 = *f.state.project();
    p0.tracks()[0].type = drift::TrackType::Video;
    p0.tracks()[1].type = drift::TrackType::Video;
    p0.tracks()[2].type = drift::TrackType::Audio;
    f.state.setRippleEnabled(true);
    for (drift::Track &t : p0.tracks())
        t.magnet = false;
    f.state.setTrackMagnet(1, true);
    const drift::Project &p = *f.state.project();
    QVERIFY(p.tracks().at(1).magnet && !p.tracks().at(0).magnet);
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("a"))->timelineStart), 0.0), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("b"))->timelineStart), 4.0), qPrintable(layoutText(p)));
    QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("c"))->timelineStart), 7.0), qPrintable(layoutText(p)));
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("bs"))->timelineStart), 4.0));
    // the track with it off keeps its gap
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("x"))->timelineStart), 5.0));
    QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("y"))->timelineStart), 12.0));
    // a later move on the 빈틈 닫기 track closes up again
    f.state.moveClip(1, f.indexOf(1, QStringLiteral("c")), 20.0);
    QVERIFY2(nearly(drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("c"))->timelineStart), 8.0),
             qPrintable(layoutText(*f.state.project())));
    f.state.undo();
    f.state.undo();
    QVERIFY(!f.state.project()->tracks().at(1).magnet);
    QVERIFY(nearly(drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("b"))->timelineStart), 12.0));
    // deleting on the off track leaves its gap
    f.state.selectClip(0, f.indexOf(0, QStringLiteral("x")));
    f.state.deleteSelectedClip();
    QVERIFY(nearly(drift::usToSeconds(clipById(*f.state.project(), QStringLiteral("y"))->timelineStart), 12.0));

    // a new project: only the main (bottom-most) video track has it
    AssetLibrary lib;
    AppController g{&lib};
    g.newProject(true);
    int on = 0, main = -1;
    for (int t = 0; t < g.project()->tracks().size(); ++t) {
        if (g.project()->tracks().at(t).magnet)
            ++on;
        if (g.project()->tracks().at(t).type == drift::TrackType::Video)
            main = t;
    }
    if (main >= 0) {
        QCOMPARE(on, 1);
        QVERIFY(g.project()->tracks().at(main).magnet);
    }
}

// VEDIT P161: 전환 누르기 (CapCut's click / "+"): three clips end to end; the playhead in the back
// half of the 2nd clip → the transition lands on the 2nd|3rd join; in the front half of the 2nd →
// on the 1st|2nd join. At most half the shorter clip (a 0.6 s clip → 0.3 s). With a transition
// selected, a click on another card replaces it. One clip alone: nothing, and false.
void EditorStateTest::parity_P161_transitionClickAtPlayhead()
{
    // VEDIT (조박사님 2026-10-19, Vegas): clips that only touch have no room for a transition —
    // a card click finds nothing there; where they overlap it lands, as long as the overlap.
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("c"), 7.7, 2.0, drift::ClipType::Video); // overlaps b by 0.3 s
    f.ready();
    f.state.project()->tracks()[0].type = drift::TrackType::Video;
    f.state.clearSelection();
    f.state.setPlayheadUs(drift::secondsToUs(7.0));
    QVERIFY(f.state.addTransitionAtPlayhead(QStringLiteral("crossfade")));
    const drift::Track &t = f.state.project()->tracks().at(0);
    const drift::Transition *bc = nullptr;
    for (const drift::Transition &x : t.transitions)
        if (x.fromClipId == QStringLiteral("b") && x.toClipId == QStringLiteral("c"))
            bc = &x;
    QVERIFY(bc);
    QVERIFY2(std::abs(drift::usToSeconds(bc->durationUs) - 0.3) < 0.001,
             qPrintable(QString::number(drift::usToSeconds(bc->durationUs))));
    // selected → replaced
    QVERIFY(f.state.addTransitionAtPlayhead(QStringLiteral("dip")));
    bool dip = false;
    for (const drift::Transition &x : f.state.project()->tracks().at(0).transitions)
        dip = dip || (x.fromClipId == QStringLiteral("b") && x.kindId == QStringLiteral("dip"));
    QVERIFY(dip);
    // a|b only touch: nothing goes there.
    for (const drift::Transition &x : f.state.project()->tracks().at(0).transitions)
        QVERIFY(!(x.fromClipId == QStringLiteral("a") && x.toClipId == QStringLiteral("b")));
}

// VEDIT P162: 클립 애니메이션 콤보 (CapCut): a combo takes the place of 들어오기/나가기 and they of it;
// it is saved; it moves the picture at the start, middle and end of a 4 s clip (and is still at
// neither end of the move); 들어오기 + 나가기 share the clip (3 s in on a 4 s clip shortens a 2 s out
// to 1 s). One undo each.
void EditorStateTest::parity_P162_clipAnimCombo()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAnimation(0, 0, QStringLiteral("animIn"), {{QStringLiteral("kind"), QStringLiteral("slideLeft")}});
    QCOMPARE(f.state.project()->tracks().at(0).clips.at(0).animIn.kind, drift::ClipAnimKind::SlideLeft);
    f.state.setClipAnimCombo(0, 0, QStringLiteral("rock"));
    const drift::Clip &c = f.state.project()->tracks().at(0).clips.at(0);
    QCOMPARE(c.animCombo, QStringLiteral("rock"));
    QCOMPARE(c.animIn.kind, drift::ClipAnimKind::None);
    QVERIFY(drift::Project::fromJson(f.state.project()->toJson()).tracks().at(0).clips.at(0).animCombo
            == QStringLiteral("rock"));
    for (const QString &id : drift::clipAnimComboIds()) {
        drift::ClipAnimSample start, mid, end;
        drift::applyClipAnimCombo(id, 0, drift::secondsToUs(4.0), 1920, 1080, &start);
        drift::applyClipAnimCombo(id, drift::secondsToUs(2.3), drift::secondsToUs(4.0), 1920, 1080, &mid);
        drift::applyClipAnimCombo(id, drift::secondsToUs(4.0), drift::secondsToUs(4.0), 1920, 1080, &end);
        const auto moved = [](const drift::ClipAnimSample &s) {
            return s.opacity < 0.99 || std::abs(s.dx) > 1 || std::abs(s.dy) > 1 || std::abs(s.scale - 1) > 0.01
                   || std::abs(s.rotationDeg) > 0.5 || std::abs(s.scaleX - 1) > 0.01 || std::abs(s.scaleY - 1) > 0.01;
        };
        QVERIFY2(moved(start) && moved(end), qPrintable(id));
        QVERIFY2(mid.opacity > 0.9, qPrintable(id)); // in the middle the picture is there
    }
    // picking 들어오기 again clears the combo
    f.state.setClipAnimation(0, 0, QStringLiteral("animIn"), {{QStringLiteral("kind"), QStringLiteral("zoomIn")},
                                                               {QStringLiteral("duration"), 3.0}});
    QVERIFY(f.state.project()->tracks().at(0).clips.at(0).animCombo.isEmpty());
    f.state.setClipAnimation(0, 0, QStringLiteral("animOut"), {{QStringLiteral("kind"), QStringLiteral("zoomOut")},
                                                                {QStringLiteral("duration"), 2.0}});
    const drift::Clip &d = f.state.project()->tracks().at(0).clips.at(0);
    QVERIFY2(std::abs(drift::usToSeconds(d.animIn.durationUs) + drift::usToSeconds(d.animOut.durationUs) - 4.0) < 0.001,
             qPrintable(QStringLiteral("%1 + %2").arg(drift::usToSeconds(d.animIn.durationUs)).arg(drift::usToSeconds(d.animOut.durationUs))));
    QVERIFY(std::abs(drift::usToSeconds(d.animOut.durationUs) - 2.0) < 0.001);
    QVERIFY(std::abs(drift::usToSeconds(d.animIn.durationUs) - 2.0) < 0.001);
    f.state.undo();
    QVERIFY(std::abs(drift::usToSeconds(f.state.project()->tracks().at(0).clips.at(0).animIn.durationUs) - 3.0) < 0.001);
}

// VEDIT P163: the moving animation tiles get their pose from the real animation code — a 들어오기
// slide starts away and ends in place; a 나가기 zoom ends away; a 콤보 rock tilts at the start; "none"
// stays still; a fade tile fades.
void EditorStateTest::parity_P163_animPreviewTiles()
{
    AssetLibrary library;
    AppController s(&library);
    const QVariantMap inStart = s.clipAnimPreview(QStringLiteral("in"), QStringLiteral("slideLeft"), 0.0);
    const QVariantMap inEnd = s.clipAnimPreview(QStringLiteral("in"), QStringLiteral("slideLeft"), 1.0);
    QVERIFY(std::abs(inStart.value(QStringLiteral("dx")).toDouble()) > 10);
    QVERIFY(std::abs(inEnd.value(QStringLiteral("dx")).toDouble()) < 0.01);
    const QVariantMap outEnd = s.clipAnimPreview(QStringLiteral("out"), QStringLiteral("zoomOut"), 0.999);
    QVERIFY(std::abs(outEnd.value(QStringLiteral("scale")).toDouble() - 1.0) > 0.05
            || outEnd.value(QStringLiteral("opacity")).toDouble() < 0.5);
    const QVariantMap rock = s.clipAnimPreview(QStringLiteral("combo"), QStringLiteral("rock"), 0.05);
    QVERIFY(std::abs(rock.value(QStringLiteral("rotation")).toDouble()) > 1);
    const QVariantMap none = s.clipAnimPreview(QStringLiteral("in"), QStringLiteral("none"), 0.1);
    QCOMPARE(none.value(QStringLiteral("opacity")).toDouble(), 1.0);
    QCOMPARE(none.value(QStringLiteral("dx")).toDouble(), 0.0);
    const QVariantMap fade = s.clipAnimPreview(QStringLiteral("in"), QStringLiteral("fade"), 0.1);
    QVERIFY(fade.value(QStringLiteral("opacity")).toDouble() < 0.5);
}

// VEDIT P164: the timeline's clip model carries the animation marks — 들어오기 0.8 s and 나가기 0.6 s
// (a plain fade is not marked, it draws its own ramp), and a 콤보 id.
void EditorStateTest::parity_P164_timelineAnimMarks()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAnimation(0, 0, QStringLiteral("animIn"), {{QStringLiteral("kind"), QStringLiteral("zoomIn")},
                                                               {QStringLiteral("duration"), 0.8}});
    f.state.setClipAnimation(0, 0, QStringLiteral("animOut"), {{QStringLiteral("kind"), QStringLiteral("fade")},
                                                                {QStringLiteral("duration"), 0.6}});
    auto *model = qobject_cast<QAbstractItemModel *>(f.state.clipsModel(0));
    QVERIFY(model && model->rowCount() == 1);
    const auto row = [model]() {
        QVariantMap out;
        const QHash<int, QByteArray> names = model->roleNames();
        for (auto it = names.constBegin(); it != names.constEnd(); ++it)
            out.insert(QString::fromUtf8(it.value()), model->data(model->index(0, 0), it.key()));
        return out;
    };
    const QVariantMap m = row();
    QVERIFY2(std::abs(m.value(QStringLiteral("animInLength")).toDouble() - 0.8) < 0.001, qPrintable(QString::number(m.value(QStringLiteral("animInLength")).toDouble())));
    QCOMPARE(m.value(QStringLiteral("animOutLength")).toDouble(), 0.0);
    f.state.setClipAnimCombo(0, 0, QStringLiteral("pendulum"));
    const QVariantMap c = row();
    QCOMPARE(c.value(QStringLiteral("animComboId")).toString(), QStringLiteral("pendulum"));
    QCOMPARE(c.value(QStringLiteral("animInLength")).toDouble(), 0.0);
}

// VEDIT P165: 빈틈 닫기 is switched on by itself only on a main video track that already has no
// gaps — an older project whose bottom camera was lined up 20 s in keeps it off (its clip stays).
void EditorStateTest::parity_P165_defaultMagnetOnlyWhenGapless()
{
    AppController::s_mainTrackMagnet = true;
    AppController::s_allTracksMagnet = false;
    const auto restore = qScopeGuard([] {
        AppController::s_mainTrackMagnet = false;
        AppController::s_allTracksMagnet = true;
    });
    ParityFixture f(2);
    f.add(0, QStringLiteral("top"), 0.0, 30.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("cam"), 20.0, 10.0, drift::ClipType::Video);
    f.ready();
    for (drift::Track &t : f.state.project()->tracks()) {
        t.type = drift::TrackType::Video;
        t.magnet = false;
    }
    // reopen it as an older project would be
    const QByteArray json = f.state.serializeProjectJson();
    QVERIFY(f.state.applyProjectJson(json, nullptr));
    QVERIFY(!f.state.project()->tracks().at(1).magnet);
    f.state.moveClip(0, 0, 1.0); // any edit
    double cam = -1;
    for (const drift::Clip &c : f.state.project()->tracks().at(1).clips)
        cam = drift::usToSeconds(c.timelineStart);
    QVERIFY2(std::abs(cam - 20.0) < 0.001, qPrintable(QString::number(cam)));
}

// VEDIT P166: picking an animation plays it (CapCut): the playhead goes to the clip's start and
// playback begins; when playback stops the playhead returns to where it was.
void EditorStateTest::parity_P166_previewPickedAnimation()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 3.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setPlayheadUs(drift::secondsToUs(6.0));
    f.state.setClipAnimation(0, 0, QStringLiteral("animIn"), {{QStringLiteral("kind"), QStringLiteral("pop")}});
    f.state.previewClipAnimation(0, 0, QStringLiteral("in"));
    QVERIFY(f.state.playing());
    QVERIFY(std::abs(drift::usToSeconds(f.state.playheadUs()) - 3.0) < 0.05);
    // reaching the end of the span: back to where it was
    f.state.setPlayheadUs(drift::secondsToUs(3.9));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(drift::usToSeconds(f.state.playheadUs()) - 6.0) < 0.001, 2000);
    QVERIFY(!f.state.playing());
    // stopped by hand: the playhead stays where it stopped
    f.state.previewClipAnimation(0, 0, QStringLiteral("in"));
    f.state.setPlayheadUs(drift::secondsToUs(3.2));
    f.state.setPlaying(false);
    QTest::qWait(150);
    QVERIFY(std::abs(drift::usToSeconds(f.state.playheadUs()) - 3.2) < 0.001);
    // with the loop on, the preview still ends and the loop comes back on
    f.state.setLoopWorkAreaEnabled(true);
    f.state.previewClipAnimation(0, 0, QStringLiteral("in"));
    QVERIFY(!f.state.loopWorkAreaEnabled());
    f.state.setPlayheadUs(drift::secondsToUs(1.0)); // wrapped round
    QTRY_VERIFY_WITH_TIMEOUT(!f.state.playing(), 2000);
    QVERIFY(f.state.loopWorkAreaEnabled());
    f.state.setLoopWorkAreaEnabled(false);
}

// VEDIT P167: a 콤보 carries on across a cut — at the cut the tail is exactly where the head was
// (no restart); 빈틈 닫기 never drags a separated sound onto narration on its row (it gets its own
// row), and a sound row with 빈틈 닫기 on follows its pictures instead of fighting them.
void EditorStateTest::parity_P167_comboCutAndSoundRows()
{
    {
        ParityFixture f(1);
        f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
        f.ready();
        f.state.setClipAnimCombo(0, 0, QStringLiteral("rock"));
        f.state.splitClipAt(0, 0, 1.5);
        const drift::Track &t = f.state.project()->tracks().at(0);
        QCOMPARE(t.clips.size(), 2);
        const drift::Clip &head = t.clips.at(0), &tail = t.clips.at(1);
        QCOMPARE(tail.animCombo, QStringLiteral("rock"));
        QCOMPARE(tail.animComboOriginUs, drift::secondsToUs(1.5));
        QCOMPARE(tail.animComboSpanUs, drift::secondsToUs(4.0));
        drift::ClipAnimSample a, b;
        drift::applyClipAnimCombo(head.animCombo, drift::secondsToUs(1.5) - 1 + head.animComboOriginUs,
                                  head.animComboSpanUs, 1920, 1080, &a);
        drift::applyClipAnimCombo(tail.animCombo, tail.animComboOriginUs, tail.animComboSpanUs, 1920, 1080, &b);
        QVERIFY(std::abs(a.rotationDeg - b.rotationDeg) < 0.1 && std::abs(a.opacity - b.opacity) < 0.01);
    }
    {
        AppController::s_mainTrackMagnet = true;
        AppController::s_allTracksMagnet = false;
        const auto restore = qScopeGuard([] {
            AppController::s_mainTrackMagnet = false;
            AppController::s_allTracksMagnet = true;
        });
        ParityFixture f(2);
        f.add(0, QStringLiteral("pic"), 10.0, 4.0, drift::ClipType::Video, QStringLiteral("L"));
        f.add(1, QStringLiteral("snd"), 10.0, 4.0, drift::ClipType::Audio, QStringLiteral("L"));
        f.add(1, QStringLiteral("talk"), 0.0, 5.0, drift::ClipType::Audio);
        f.ready();
        drift::Project &p0 = *f.state.project();
        p0.tracks()[0].type = drift::TrackType::Video;
        p0.tracks()[1].type = drift::TrackType::Audio;
        for (drift::Track &t : p0.tracks())
            t.magnet = false;
        f.state.setTrackMagnet(1, true); // the sound row too — it must not fight the pictures
        f.state.setTrackMagnet(0, true);
        const drift::Project &p = *f.state.project();
        QVERIFY2(nearly(drift::usToSeconds(clipById(p, QStringLiteral("pic"))->timelineStart), 0.0), qPrintable(layoutText(p)));
        int sndTrack = -1, talkTrack = -1;
        const drift::Clip *snd = clipById(p, QStringLiteral("snd"), &sndTrack);
        clipById(p, QStringLiteral("talk"), &talkTrack);
        QVERIFY(nearly(drift::usToSeconds(snd->timelineStart), 0.0));
        QVERIFY2(sndTrack != talkTrack, qPrintable(layoutText(p)));
        QVERIFY(nearly(drift::usToSeconds(clipById(p, QStringLiteral("talk"))->timelineStart), 0.0));
    }
}

// VEDIT P168: Vegas fade types — five curves (fast, linear, slow, smooth, sharp) each from 0 to 1;
// a transition's two sides chosen apart (sound: each its own gain; picture: their proportion),
// saved and undone; a clip's fade-out curve set apart from its fade-in.
void EditorStateTest::parity_P168_vegasFadeTypes()
{
    const drift::FadeShape none;
    for (const QString &c : {QStringLiteral("equalPower"), QStringLiteral("linear"), QStringLiteral("slow"),
                             QStringLiteral("smooth"), QStringLiteral("sharp")}) {
        const drift::FadeCurve k = drift::fadeCurveFromString(c);
        QCOMPARE(drift::fadeCurveToString(k), c);
        QVERIFY2(std::abs(drift::shapedProgress(0.0, k, none)) < 1e-9, qPrintable(c));
        QVERIFY2(std::abs(drift::shapedProgress(1.0, k, none) - 1.0) < 1e-9, qPrintable(c));
    }
    QVERIFY(drift::shapedProgress(0.25, drift::FadeCurve::Slow, none) < 0.25);
    QVERIFY(drift::shapedProgress(0.25, drift::FadeCurve::EqualPower, none) > 0.25);
    QVERIFY(drift::shapedProgress(0.1, drift::FadeCurve::Sharp, none) > 0.1);

    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 3.0, 4.0, drift::ClipType::Video); // 1 s overlap
    f.ready();
    f.state.project()->tracks()[0].type = drift::TrackType::Video;
    f.state.addTransition(0, 0, QStringLiteral("crossfade"), 1.0);
    const QString id = f.state.transitionBetweenClips(0, 0).value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    f.state.setTransitionFadeTypes(0, id, QStringLiteral("slow"), QStringLiteral("equalPower"));
    const drift::Transition &t = f.state.project()->tracks().at(0).transitions.first();
    QVERIFY(t.splitCurves && t.outCurve == drift::FadeCurve::Slow && t.inCurve == drift::FadeCurve::EqualPower);
    // picture: at the middle, the fast-in side outweighs the slow-out side
    QVERIFY(drift::transitionProgress(t, drift::secondsToUs(3.5), drift::secondsToUs(3.0), drift::secondsToUs(4.0)) > 0.5);
    const QVariantMap m = f.state.transitionBetweenClips(0, 0);
    QCOMPARE(m.value(QStringLiteral("outCurve")).toString(), QStringLiteral("slow"));
    QCOMPARE(m.value(QStringLiteral("inCurve")).toString(), QStringLiteral("equalPower"));
    const drift::Project back = drift::Project::fromJson(f.state.project()->toJson());
    QVERIFY(back.tracks().at(0).transitions.first().splitCurves);
    QCOMPARE(back.tracks().at(0).transitions.first().outCurve, drift::FadeCurve::Slow);
    f.state.undo();
    QVERIFY(!f.state.project()->tracks().at(0).transitions.first().splitCurves);

    ParityFixture g(1);
    g.add(0, QStringLiteral("c"), 0.0, 4.0, drift::ClipType::Video);
    g.ready();
    g.state.setClipFade(0, 0, 1.0, 1.0);
    g.state.setClipFadeEdgeCurve(0, 0, QStringLiteral("out"), QStringLiteral("sharp"));
    g.state.setClipFadeEdgeCurve(0, 0, QStringLiteral("in"), QStringLiteral("slow"));
    const drift::Clip &c = g.state.project()->tracks().at(0).clips.at(0);
    QCOMPARE(c.fadeCurve, drift::FadeCurve::Slow);
    QCOMPARE(c.effectiveFadeOutCurve(), drift::FadeCurve::Sharp);
    QCOMPARE(c.animOut.curve, drift::FadeCurve::Sharp);
    // fade-out at 0.1 s before the end follows the sharp curve
    const double g90 = c.fadeMultiplier(drift::secondsToUs(3.9));
    QVERIFY2(std::abs(g90 - drift::shapedProgress(0.1, drift::FadeCurve::Sharp, drift::FadeShape())) < 0.02,
             qPrintable(QString::number(g90)));
    QCOMPARE(drift::Project::fromJson(g.state.project()->toJson()).tracks().at(0).clips.at(0).effectiveFadeOutCurve(),
             drift::FadeCurve::Sharp);
}

// VEDIT P169: trimming follows the pointer for video, sound, text and subtitle clips alike — a
// pointer back at the clip's own edge previews the clip's own length (not the last spot), and a
// preview step costs well under a millisecond for each kind.
void EditorStateTest::parity_P169_trimPreviewFollowsEveryKind()
{
    ParityFixture f(4);
    f.add(0, QStringLiteral("v"), 0.0, 6.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("a"), 0.0, 6.0, drift::ClipType::Audio);
    f.add(2, QStringLiteral("t"), 0.0, 6.0, drift::ClipType::Text);
    f.add(3, QStringLiteral("s"), 0.0, 6.0, drift::ClipType::Subtitle);
    f.ready();
    for (int t = 0; t < 4; ++t) {
        const QVariantMap shorter = f.state.previewTrimRight(t, 0, 4.0);
        QVERIFY2(shorter.value(QStringLiteral("ok")).toBool(), qPrintable(QString::number(t)));
        QVERIFY2(std::abs(shorter.value(QStringLiteral("duration")).toDouble() - 4.0) < 0.01,
                 qPrintable(QStringLiteral("track %1: %2").arg(t).arg(shorter.value(QStringLiteral("duration")).toDouble())));
        const QVariantMap back = f.state.previewTrimRight(t, 0, 6.0);
        QVERIFY(back.value(QStringLiteral("ok")).toBool());
        QVERIFY2(std::abs(back.value(QStringLiteral("duration")).toDouble() - 6.0) < 0.01,
                 qPrintable(QStringLiteral("track %1 back: %2").arg(t).arg(back.value(QStringLiteral("duration")).toDouble())));
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 2000; ++i)
            f.state.previewTrimRight(t, 0, 2.0 + (i % 300) / 100.0);
        const double perStepMs = timer.nsecsElapsed() / 1e6 / 2000.0;
        qInfo("trim preview, track %d: %.4f ms per step", t, perStepMs);
        QVERIFY2(perStepMs < 0.5, qPrintable(QString::number(perStepMs)));
    }
    // What the text panel re-reads after each edit of a selected text clip (timing only).
    f.state.selectClip(2, 0);
    QElapsedTimer t2;
    t2.start();
    for (int i = 0; i < 20; ++i)
        f.state.fontCatalog();
    qInfo("fontCatalog: %.2f ms", t2.nsecsElapsed() / 1e6 / 20);
    t2.restart();
    for (int i = 0; i < 20; ++i)
        f.state.selectedClipData();
    qInfo("selectedClipData (text): %.2f ms", t2.nsecsElapsed() / 1e6 / 20);
    t2.restart();
    for (int i = 0; i < 20; ++i)
        f.state.textLooks();
    qInfo("textLooks: %.2f ms", t2.nsecsElapsed() / 1e6 / 20);
    t2.restart();
    for (int i = 0; i < 20; ++i)
        f.state.textAnimationPresets(QStringLiteral("in"));
    qInfo("textAnimationPresets: %.2f ms", t2.nsecsElapsed() / 1e6 / 20);
}

// VEDIT P170: the event level (Vegas) — set on a sound clip and on a text clip, saved, undone; the
// picture's opacity follows it in the render.
void EditorStateTest::parity_P170_clipEventLevel()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("t"), 0.0, 4.0, drift::ClipType::Text);
    f.add(1, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Audio);
    f.ready();
    f.state.setClipLevel(1, 0, 0.25);
    f.state.setClipLevel(0, 0, 0.9);
    QVERIFY(std::abs(f.state.project()->tracks().at(1).clips.at(0).level - 0.25) < 1e-9);
    QVERIFY(std::abs(f.state.project()->tracks().at(0).clips.at(0).level - 0.9) < 1e-9);
    const drift::Project back = drift::Project::fromJson(f.state.project()->toJson());
    QVERIFY(std::abs(back.tracks().at(1).clips.at(0).level - 0.25) < 1e-9);
    f.state.undo();
    QVERIFY(std::abs(f.state.project()->tracks().at(0).clips.at(0).level - 1.0) < 1e-9);
    QVERIFY(std::abs(f.state.project()->tracks().at(1).clips.at(0).level - 0.25) < 1e-9);
}

// VEDIT P171: stereo sides — a clip's own choice and a track's; setting the track clears the
// clips' own choices; saved and undone.
void EditorStateTest::parity_P171_channelModes()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Audio);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Audio);
    f.ready();
    f.state.project()->tracks()[0].type = drift::TrackType::Audio;
    f.state.setClipChannelMode(0, 0, 1);
    QCOMPARE(f.state.project()->tracks().at(0).clips.at(0).channelMode, 1);
    f.state.setTrackChannelMode(0, 3);
    QCOMPARE(f.state.project()->tracks().at(0).channelMode, 3);
    QCOMPARE(f.state.project()->tracks().at(0).clips.at(0).channelMode, 0);
    const drift::Project back = drift::Project::fromJson(f.state.project()->toJson());
    QCOMPARE(back.tracks().at(0).channelMode, 3);
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().at(0).channelMode, 0);
    QCOMPARE(f.state.project()->tracks().at(0).clips.at(0).channelMode, 1);
}

// VEDIT P172: "조정 처음대로" takes CapCut's HSL, 곡선 and 컬러 휠 off too, and they show in the
// clip's adjust values while set.
void EditorStateTest::parity_P172_resetClearsHslCurvesWheels()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.hsl"), QStringLiteral("h_red"), 0.5);
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.curves"), QStringLiteral("m2"), 0.7);
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.wheels"), QStringLiteral("lift_x"), 0.3);
    const QVariantMap v = f.state.clipAdjustValues(0, 0);
    QVERIFY(nearly(v.value(QStringLiteral("adjust.hsl.h_red")).toDouble(), 0.5));
    QVERIFY(nearly(v.value(QStringLiteral("adjust.curves.m2")).toDouble(), 0.7));
    QVERIFY(nearly(v.value(QStringLiteral("adjust.wheels.lift_x")).toDouble(), 0.3));
    f.state.resetClipAdjust(0, 0);
    const QVariantMap w = f.state.clipAdjustValues(0, 0);
    QVERIFY2(!w.contains(QStringLiteral("adjust.hsl.h_red")) && !w.contains(QStringLiteral("adjust.curves.m2"))
                 && !w.contains(QStringLiteral("adjust.wheels.lift_x")),
             qPrintable(QStringList(w.keys()).join(QLatin1Char(','))));
}

// VEDIT P173: 색 맞추기 — a dim bluish scene after a warm bright one comes out close to the warm one.
void EditorStateTest::parity_P173_matchClipColor()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("No GPU compositor available");
    AssetLibrary library;
    AppController state(&library);
    drift::Project &project = *state.project();
    project.setResolution(64, 36);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});
    const auto shape = [](const QString &id, double start, QColor colour) {
        drift::Clip c;
        c.id = id;
        c.type = drift::ClipType::Shape;
        c.timelineStart = drift::secondsToUs(start);
        c.timelineDuration = drift::secondsToUs(2.0);
        c.shapeStyle.kind = drift::ShapeKind::Rectangle;
        c.shapeStyle.setSolidFill(colour);
        return c;
    };
    project.tracks()[0].clips.append(shape(QStringLiteral("warm"), 0.0, QColor(200, 140, 90)));
    project.tracks()[0].clips.append(shape(QStringLiteral("cool"), 2.0, QColor(70, 80, 120)));
    QVERIFY(state.matchClipColor(0, 1));
    FrameCompositor compositor;
    compositor.setProject(state.project());
    const QColor got = compositor.compositeAt(drift::secondsToUs(3.0)).convertToFormat(QImage::Format_RGB32).pixelColor(32, 18);
    const QColor want(200, 140, 90);
    QVERIFY2(std::abs(got.red() - want.red()) < 45 && std::abs(got.blue() - want.blue()) < 45
                 && got.red() > got.blue(),
             qPrintable(got.name()));
}

// VEDIT: finds a clip by id anywhere in the project (lanes minted by effects move tracks).
static bool veditLocate(const drift::Project &project, const QString &id, int *track, int *clip)
{
    for (int t = 0; t < project.tracks().size(); ++t)
        for (int c = 0; c < project.tracks().at(t).clips.size(); ++c)
            if (project.tracks().at(t).clips.at(c).id == id) {
                *track = t;
                *clip = c;
                return true;
            }
    return false;
}

static bool veditAnyOverlap(const drift::Project &project)
{
    for (const drift::Track &track : project.tracks())
        for (int i = 0; i < track.clips.size(); ++i)
            for (int j = i + 1; j < track.clips.size(); ++j) {
                const drift::Clip &a = track.clips.at(i);
                const drift::Clip &b = track.clips.at(j);
                if (a.timelineStart < b.timelineStart + b.timelineDuration
                    && b.timelineStart < a.timelineStart + a.timelineDuration)
                    return true;
            }
    return false;
}

// VEDIT P174 (조박사님): a pasted clip never lands on top of another. With room on the track last
// clicked it goes there; with room nowhere a new track is made for it.
void EditorStateTest::parity_P174_pasteNeverOverlaps()
{
    ParityFixture f(2);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(1, QStringLiteral("b"), 6.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.selectClip(0, 0);
    f.state.copySelection();
    f.state.noteTrackClicked(1);
    f.state.setPlayheadSeconds(1.0);
    f.state.pasteAtPlayhead();
    QCOMPARE(f.state.project()->tracks().size(), 2);
    QCOMPARE(f.state.project()->tracks().at(1).clips.size(), 2); // the clicked track had room
    QCOMPARE(f.state.project()->tracks().at(0).clips.size(), 1);
    QVERIFY(!veditAnyOverlap(*f.state.project()));
    // Both tracks busy at 2s: a third track appears rather than an overlap.
    f.state.noteTrackClicked(1);
    f.state.setPlayheadSeconds(2.0);
    f.state.pasteAtPlayhead();
    QCOMPARE(f.state.project()->tracks().size(), 3);
    QVERIFY(!veditAnyOverlap(*f.state.project()));
    f.state.undo();
    QCOMPARE(f.state.project()->tracks().size(), 2);
}

// VEDIT P175: 특성 복사 → 특성 붙여넣기 carries effects (from the clip's effect host), fades in
// seconds, level and keyframes; 길이 맞추기 stretches the keyframes to the target, off keeps them.
void EditorStateTest::parity_P175_copyPasteAttributes()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("src"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("dst"), 5.0, 8.0, drift::ClipType::Video);
    f.ready();
    {
        drift::Clip &src = f.state.project()->tracks()[0].clips[0];
        src.fadeInUs = drift::secondsToUs(1.0);
        src.level = 0.6;
        src.opacity.setKeyframe(0, 1.0);
        src.opacity.setKeyframe(src.timelineDuration, 0.2);
    }
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5);
    int t = -1, c = -1;
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("src"), &t, &c));
    f.state.copyAttributes(t, c);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("dst"), &t, &c));
    f.state.selectClip(t, c);
    QVERIFY(f.state.canPasteAttributes());
    const QVariantMap attrs = f.state.clipboardAttributes();
    QVERIFY(attrs.value(QStringLiteral("effectCount")).toInt() >= 1);

    QVariantMap options;
    for (const char *k : {"transform", "animation", "effects", "fades", "level", "volume"})
        options.insert(QString::fromLatin1(k), true);
    options.insert(QStringLiteral("replaceEffects"), true);
    options.insert(QStringLiteral("fitLength"), true);
    f.state.pasteAttributes(options);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("dst"), &t, &c));
    {
        const drift::Clip &dst = f.state.project()->tracks().at(t).clips.at(c);
        QCOMPARE(dst.fadeInUs, drift::secondsToUs(1.0));
        QVERIFY(nearly(dst.level, 0.6));
        QCOMPARE(dst.opacity.keyframes().lastKey(), drift::secondsToUs(8.0));
    }
    const QVariantMap v = f.state.clipAdjustValues(t, c);
    QVERIFY2(nearly(v.value(QStringLiteral("adjust.tone.exposure")).toDouble(), 0.5),
             qPrintable(QStringList(v.keys()).join(QLatin1Char(','))));

    f.state.undo();
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("dst"), &t, &c));
    f.state.selectClip(t, c);
    options.insert(QStringLiteral("fitLength"), false);
    f.state.pasteAttributes(options);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("dst"), &t, &c));
    QCOMPARE(f.state.project()->tracks().at(t).clips.at(c).opacity.keyframes().lastKey(),
             drift::secondsToUs(4.0));
}

// VEDIT P176 (조박사님): a sound split off a video is not linked — moving it leaves the video —
// and turns a little red once it no longer lines up with the video, back when it does.
void EditorStateTest::parity_P176_splitSoundUnlinkedAndOutOfSyncTint()
{
    AssetLibrary library;
    AppController state(&library);
    appendCombinedVideoClip(*state.project());
    state.selectClip(0, 0);
    state.separateAudioFromSelection();
    QCOMPARE(state.project()->tracks().size(), 2);
    const auto outOfSync = [&state]() {
        auto *model = qobject_cast<QAbstractItemModel *>(state.clipsModel(1));
        const QHash<int, QByteArray> roles = model->roleNames();
        return model->data(model->index(0, 0), roles.key("outOfSync")).toBool();
    };
    QVERIFY(!outOfSync());
    const drift::TimeUs videoStart = state.project()->tracks().at(0).clips.at(0).timelineStart;
    state.moveClip(1, 0, 1.5);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, videoStart); // not linked
    QVERIFY(outOfSync());
    // Undo puts it back in line.
    state.undo();
    QVERIFY(!outOfSync());
    // Saved and loaded, it still knows where it belongs.
    const drift::Project back = drift::Project::fromJson(state.project()->toJson());
    QCOMPARE(back.tracks().at(1).clips.at(0).syncId, back.tracks().at(0).clips.at(0).syncId);
    drift::Clip moved = back.tracks().at(1).clips.at(0);
    QVERIFY(!drift::soundOutOfSync(moved, {&back.tracks().at(0).clips.at(0)}));
    moved.timelineStart += drift::secondsToUs(0.5);
    QVERIFY(drift::soundOutOfSync(moved, {&back.tracks().at(0).clips.at(0)}));
    moved.srcIn += drift::secondsToUs(0.5); // head trimmed by the same half second: lined up again
    QVERIFY(!drift::soundOutOfSync(moved, {&back.tracks().at(0).clips.at(0)}));
}

// VEDIT P177 (조박사님): a video clip's effects show in its band as FX, not as a lane row; 효과 줄
// 분리 turns them into a lane of their own that no longer follows the clip.
void EditorStateTest::parity_P177_fxInBandThenSeparateLane()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    const int heightBefore = f.state.trackRowHeight(0, {});
    f.state.setClipAdjust(0, 0, QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.5);
    int t = -1, c = -1;
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    QCOMPARE(f.state.adjustmentLanes(t).size(), 0);          // no strip
    QCOMPARE(f.state.trackRowHeight(t, {}), heightBefore);   // and no extra height
    QVERIFY(f.state.separateEffectLane(t, c));
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    QCOMPARE(f.state.adjustmentLanes(t).size(), 1);
    QVERIFY(f.state.trackRowHeight(t, {}) > heightBefore);
    // Unlinked: moving the clip leaves the effect where it was.
    const int lane = f.state.adjustmentLanes(t).first().toInt();
    const drift::TimeUs fxStart = f.state.project()->tracks().at(lane).clips.at(0).timelineStart;
    f.state.moveClip(t, c, 2.0);
    QCOMPARE(f.state.project()->tracks().at(lane).clips.at(0).timelineStart, fxStart);
    QVERIFY(f.state.project()->tracks().at(lane).clips.at(0).linkedClipId.isEmpty());
}

// VEDIT P178 (조박사님): several bin items dropped together go in by when they were shot — the
// date and time in a DJI-style file name — one after another from the drop point.
void EditorStateTest::parity_P178_groupDropInShotOrder()
{
    AssetLibrary library;
    AppController state(&library);
    drift::Project *project = state.project();
    project->tracks().clear();
    project->tracks().append(drift::Track{.type = drift::TrackType::Video});
    const QStringList names{QStringLiteral("DJI_20261014020200_0016_D.MP4"),
                            QStringLiteral("DJI_20261010170959_0002_D.MP4"),
                            QStringLiteral("DJI_20261014005035_0005_D.MP4")};
    QStringList ids;
    for (int i = 0; i < names.size(); ++i) {
        drift::MediaAsset asset;
        asset.id = QStringLiteral("shot-%1").arg(i);
        asset.path = QStringLiteral("C:/nowhere/") + names.at(i);
        asset.name = names.at(i);
        asset.kind = drift::MediaKind::Image; // no probing needed
        asset.durationUs = drift::secondsToUs(2.0);
        project->assets().insert(asset.id, asset);
        project->assetOrder().append(asset.id);
        ids << asset.id;
    }
    library.syncToProject();
    const QStringList sorted = state.sortAssetIdsByShotTime(ids);
    QCOMPARE(sorted, (QStringList{QStringLiteral("shot-1"), QStringLiteral("shot-2"), QStringLiteral("shot-0")}));

    state.addClipsFromAssetsAt(ids, 3.0);
    QList<drift::Clip> placed;
    for (const drift::Track &t : project->tracks())
        for (const drift::Clip &c : t.clips)
            placed << c;
    QCOMPARE(placed.size(), 3);
    std::sort(placed.begin(), placed.end(), [](const drift::Clip &a, const drift::Clip &b) {
        return a.timelineStart < b.timelineStart;
    });
    QCOMPARE(placed.at(0).timelineStart, drift::secondsToUs(3.0));
    QCOMPARE(placed.at(0).assetId, QStringLiteral("shot-1"));
    QCOMPARE(placed.at(1).assetId, QStringLiteral("shot-2"));
    QCOMPARE(placed.at(2).assetId, QStringLiteral("shot-0"));
    QCOMPARE(placed.at(1).timelineStart, placed.at(0).timelineStart + placed.at(0).timelineDuration);
    QCOMPARE(state.selection().size(), 3); // VEDIT: all three stay selected, ready to move together
    state.undo();
    int left = 0;
    for (const drift::Track &t : project->tracks())
        left += t.clips.size();
    QCOMPARE(left, 0); // one undo step
}

// VEDIT P179 (조박사님): 끼워 넣기 — with 빈틈 메움 on, a later clip dropped on the seam before an
// earlier one goes in there; the clips after the seam move right by its length and the place it
// left closes up. One undo step.
void EditorStateTest::parity_P179_insertClipOnSeam()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 2.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 2.0, 3.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("c"), 5.0, 1.0, drift::ClipType::Video);
    f.ready();
    f.state.setTrackMagnet(0, true);
    QVERIFY(f.state.trackInsertsOnDrop(0));
    // c (1 s) goes in before b.
    f.state.insertClipBefore(0, f.indexOf(0, QStringLiteral("c")), 0, QStringLiteral("b"));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 0.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 2.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 3.0));
    QCOMPARE(f.clipCount(0), 3);
    f.state.undo();
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 2.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 5.0));
    // a (2 s) moved to before c: b closes up to 0, a lands at b's end, c after it.
    f.state.insertClipBefore(0, f.indexOf(0, QStringLiteral("a")), 0, QStringLiteral("c"));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("b")), 0.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("a")), 3.0));
    QVERIFY(nearly(f.startOf(0, QStringLiteral("c")), 5.0));
}

// VEDIT P180 (조박사님): a speed change on a clip with a 임시 렌더링 drops it at once; undo brings
// the earlier state back, and with it the same render key (so the file is found again).
void EditorStateTest::parity_P180_renderCacheFollowsEditsAndUndo()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.project()->tracks()[0].clips[0].path = QStringLiteral("C:/nowhere/v.mp4");
    f.state.setClipSpeed(0, 0, 2.0);
    RenderCache::instance().update(*f.state.project());
    const QVariantList first = RenderCache::instance().segmentsForUi();
    QCOMPARE(first.size(), 1);
    QSignalSpy spy(&RenderCache::instance(), &RenderCache::segmentsChanged);
    f.state.setClipSpeed(0, 0, 0.5);
    RenderCache::instance().update(*f.state.project());
    QVERIFY(spy.count() >= 1);
    const QVariantList second = RenderCache::instance().segmentsForUi();
    QCOMPARE(second.size(), 1);
    QVERIFY(!qFuzzyCompare(second.first().toMap().value(QStringLiteral("end")).toDouble(),
                           first.first().toMap().value(QStringLiteral("end")).toDouble()));
    f.state.undo();
    RenderCache::instance().update(*f.state.project());
    const QVariantList back = RenderCache::instance().segmentsForUi();
    QCOMPARE(back.size(), 1);
    QVERIFY(qFuzzyCompare(back.first().toMap().value(QStringLiteral("end")).toDouble(),
                          first.first().toMap().value(QStringLiteral("end")).toDouble()));
}

// VEDIT P181 (조박사님): a bin item dropped where it would overlap a clip goes on a new track at that
// spot instead; and a track's empty space offers a new track of its kind right there.
void EditorStateTest::parity_P181_binDropNeverOverlaps()
{
    AssetLibrary library;
    AppController state(&library);
    drift::Project *project = state.project();
    project->tracks().clear();
    project->tracks().append(drift::Track{.type = drift::TrackType::Video});
    drift::MediaAsset asset;
    asset.id = QStringLiteral("pic");
    asset.path = QStringLiteral("C:/nowhere/pic.png");
    asset.name = QStringLiteral("pic.png");
    asset.kind = drift::MediaKind::Image;
    asset.durationUs = drift::secondsToUs(3.0);
    project->assets().insert(asset.id, asset);
    project->assetOrder().append(asset.id);
    library.syncToProject();
    const int index = library.indexOfId(QStringLiteral("pic"));
    QVERIFY(index >= 0);
    state.addClipFromAssetAt(index, 0, 0.0);
    QCOMPARE(project->tracks().size(), 1);
    state.addClipFromAssetAt(index, 0, 1.0); // would overlap the first
    int videoTracks = 0;
    for (const drift::Track &t : project->tracks())
        if (t.type == drift::TrackType::Video) {
            ++videoTracks;
            QCOMPARE(t.clips.size(), 1);
        }
    QCOMPARE(videoTracks, 2);
    // A new track of the same kind, from the track's own empty space.
    const int before = project->tracks().size();
    state.addTrackNear(0);
    QCOMPARE(project->tracks().size(), before + 1);
}

// VEDIT P182 (CapCut): the clip's keyframe moments — every animated property together — for the
// ◆ marks on the timeline clip; dragging one moves every property's key at that moment.
void EditorStateTest::parity_P182_clipKeyframeMarksAndMove()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipKeyframe(0, 0, QStringLiteral("opacity"), 1.0, 1.0);
    f.state.setClipKeyframe(0, 0, QStringLiteral("opacity"), 3.0, 0.2);
    f.state.setClipKeyframe(0, 0, QStringLiteral("rotation"), 1.0, 30.0);
    const QVariantList marks = f.state.clipKeyframeMarks(0, 0);
    QCOMPARE(marks.size(), 2);
    QVERIFY(nearly(marks.at(0).toDouble(), 1.0));
    QVERIFY(nearly(marks.at(1).toDouble(), 3.0));
    f.state.moveClipKeyframesAt(0, 0, 1.0, 2.0);
    const QVariantList moved = f.state.clipKeyframeMarks(0, 0);
    QCOMPARE(moved.size(), 2);
    QVERIFY(nearly(moved.at(0).toDouble(), 2.0));
    const QVariantList rot = f.state.clipKeyframes(0, 0, QStringLiteral("rotation"));
    QCOMPARE(rot.size(), 1);
    QVERIFY(nearly(rot.first().toMap().value(QStringLiteral("seconds")).toDouble(), 2.0));
    QVERIFY(nearly(rot.first().toMap().value(QStringLiteral("value")).toDouble(), 30.0));
    f.state.undo();
    QVERIFY(nearly(f.state.clipKeyframeMarks(0, 0).first().toDouble(), 1.0));
}

// VEDIT P183 (CapCut): ◇ on a 조정 slider keyframes it at the playhead; once keyframed, moving the
// slider elsewhere sets a keyframe there, and the value shown follows the playhead.
void EditorStateTest::parity_P183_adjustSliderKeyframes()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    const QString fx = QStringLiteral("adjust.tone");
    const QString key = QStringLiteral("exposure");
    int t = 0, c = 0;
    f.state.setPlayheadSeconds(1.0);
    f.state.toggleClipAdjustKeyframe(t, c, fx, key);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    QVariantMap st = f.state.clipAdjustKeyState(t, c, fx, key);
    QVERIFY(st.value(QStringLiteral("animated")).toBool());
    QVERIFY(st.value(QStringLiteral("atPlayhead")).toBool());
    f.state.setPlayheadSeconds(3.0);
    f.state.setClipAdjust(t, c, fx, key, 0.8); // auto keyframe at 3 s
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    st = f.state.clipAdjustKeyState(t, c, fx, key);
    QVERIFY(st.value(QStringLiteral("atPlayhead")).toBool());
    QVERIFY(nearly(st.value(QStringLiteral("prev")).toDouble(), 1.0));
    QVERIFY(nearly(f.state.clipAdjustValues(t, c).value(fx + QStringLiteral(".") + key).toDouble(), 0.8));
    f.state.setPlayheadSeconds(1.0);
    QVERIFY(nearly(f.state.clipAdjustValues(t, c).value(fx + QStringLiteral(".") + key).toDouble(), 0.0));
    f.state.setPlayheadSeconds(3.0);
    f.state.toggleClipAdjustKeyframe(t, c, fx, key); // out again
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    QVERIFY(!f.state.clipAdjustKeyState(t, c, fx, key).value(QStringLiteral("atPlayhead")).toBool());
}

// VEDIT P184 (CapCut): ◇ for 크기 keys width, height and position together; and a keyframed value
// changed where it has no keyframe gains one there (it used to be refused — the picture could not
// be dragged once a clip had keyframes).
void EditorStateTest::parity_P184_scaleKeyframeAndAutoKey()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    const QStringList scale{QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("x"), QStringLiteral("y")};
    f.state.setPlayheadSeconds(0.5);
    f.state.toggleClipPropsKeyframe(0, 0, scale);
    f.state.setPlayheadSeconds(2.5);
    f.state.toggleClipPropsKeyframe(0, 0, scale);
    QVariantMap st = f.state.clipPropsKeyState(0, 0, scale);
    QVERIFY(st.value(QStringLiteral("animated")).toBool());
    QVERIFY(st.value(QStringLiteral("atPlayhead")).toBool());
    QVERIFY(nearly(st.value(QStringLiteral("prev")).toDouble(), 0.5));
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("width")).size(), 2);
    // Moving the picture at 1.5 s, where nothing is keyed, adds keys there instead of refusing.
    f.state.setPlayheadSeconds(1.5);
    f.state.previewSetClipRect(0, 0, 10.0, 20.0, 300.0, 200.0);
    f.state.commitPreviewDrag();
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("width")).size(), 3);
    // ◇ at a keyed moment takes it out again.
    f.state.setPlayheadSeconds(2.5);
    f.state.toggleClipPropsKeyframe(0, 0, scale);
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("width")).size(), 2);
}

// VEDIT P185 (조박사님): clicking a transition selects the transition, not the clip under it, and
// Delete then removes only the transition — both clips stay.
void EditorStateTest::parity_P185_deleteSelectedTransitionOnly()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.addTransition(0, 0, QStringLiteral("crossfade"), 1.0);
    QVERIFY(!f.state.transitionBetweenClips(0, 0).isEmpty());
    f.state.selectTransition(0, 0);
    QCOMPARE(f.state.selectedTransitionTrack(), 0);
    f.state.deleteSelectedClip();
    QCOMPARE(f.clipCount(0), 2);
    // Gone — or, where the clips still overlap, back to the automatic plain crossfade.
    const QVariantMap after = f.state.transitionBetweenClips(0, 0);
    QVERIFY(after.isEmpty() || after.value(QStringLiteral("autoOverlap")).toBool());
}

// VEDIT P186 (조박사님, Vegas): a transition goes only where two clips overlap, exactly as long as
// the overlap; clips that only touch have no room for one.
void EditorStateTest::parity_P186_transitionOnlyInOverlap()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);   // touches a
    f.add(0, QStringLiteral("c"), 7.0, 4.0, drift::ClipType::Video);   // overlaps b by 1 s
    f.ready();
    QVERIFY(!f.state.addTransitionOnOverlap(0, f.indexOf(0, QStringLiteral("a")), QStringLiteral("wipe_left")));
    const QVariantMap none = f.state.transitionBetweenClips(0, f.indexOf(0, QStringLiteral("a")));
    QVERIFY(none.isEmpty() || none.value(QStringLiteral("autoOverlap")).toBool());
    QVERIFY(f.state.addTransitionOnOverlap(0, f.indexOf(0, QStringLiteral("b")), QStringLiteral("crossfade")));
    const QVariantMap made = f.state.transitionBetweenClips(0, f.indexOf(0, QStringLiteral("b")));
    QVERIFY(!made.isEmpty());
    QVERIFY(nearly(made.value(QStringLiteral("duration")).toDouble(), 1.0));
    QCOMPARE(f.state.applyTransitionToAllOverlaps(0, QStringLiteral("crossfade")), 1);
}

// VEDIT P187 (조박사님): 속성 모두 지우기 — effects, keyframes, speed, fades, level, opacity all go
// back to how the clip came in; the media and its place stay. One undo step.
void EditorStateTest::parity_P187_clearClipAttributes()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 4.0, drift::ClipType::Video);
    f.ready();
    f.state.setClipSpeed(0, 0, 2.0);
    int t = 0, c = 0;
    f.state.setClipAdjust(t, c, QStringLiteral("adjust.tone"), QStringLiteral("exposure"), 0.6);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    f.state.setClipKeyframe(t, c, QStringLiteral("opacity"), 0.5, 0.3);
    f.state.setClipKeyframe(t, c, QStringLiteral("opacity"), 1.5, 0.9);
    f.state.project()->tracks()[t].clips[c].fadeInUs = drift::secondsToUs(0.5);
    f.state.project()->tracks()[t].clips[c].level = 0.4;
    f.state.clearClipAttributes(t, c);
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    const drift::Clip &clip = f.state.project()->tracks().at(t).clips.at(c);
    QVERIFY(nearly(clip.speed, 1.0));
    QCOMPARE(clip.timelineDuration, drift::secondsToUs(4.0));
    QVERIFY(clip.opacity.keyframes().size() <= 1);
    QCOMPARE(clip.fadeInUs, drift::TimeUs(0));
    QVERIFY(nearly(clip.level, 1.0));
    QVERIFY(f.state.clipAdjustValues(t, c).isEmpty());
    f.state.undo();
    QVERIFY(veditLocate(*f.state.project(), QStringLiteral("v"), &t, &c));
    QVERIFY(nearly(f.state.project()->tracks().at(t).clips.at(c).level, 0.4));
}

// VEDIT P188 (조박사님, Vegas volume envelope): a point added on the line is selected and the
// playhead goes to it; Delete then removes the point, not the clip.
void EditorStateTest::parity_P188_volumeEnvelopePoints()
{
    ParityFixture f(1, drift::TrackType::Audio);
    f.add(0, QStringLiteral("s"), 0.0, 6.0, drift::ClipType::Audio);
    f.ready();
    f.state.addVolumeKey(0, 0, 2.0, 0.5);
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("volume")).size(), 1);
    QCOMPARE(f.state.selectedVolumeKey().value(QStringLiteral("clipId")).toString(), QStringLiteral("s"));
    QVERIFY(nearly(f.state.playheadSeconds(), 2.0));
    f.state.addVolumeKey(0, 0, 4.0, 1.0);
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("volume")).size(), 2);
    f.state.deleteSelectedClip(); // the picked point (4 s), not the clip
    QCOMPARE(f.clipCount(0), 1);
    const QVariantList keys = f.state.clipKeyframes(0, 0, QStringLiteral("volume"));
    QCOMPARE(keys.size(), 1);
    QVERIFY(nearly(keys.first().toMap().value(QStringLiteral("seconds")).toDouble(), 2.0));
    QVERIFY(f.state.selectedVolumeKey().isEmpty());
}

// VEDIT P189 (조박사님): the speed slider keeps the shot's first and last frames, changes only the
// length of the clip it is on, and leaves the other clips' speed alone.
void EditorStateTest::parity_P189_speedSliderOnlyThisClip()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 4.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("c"), 8.0, 4.0, drift::ClipType::Video);
    f.ready();
    const int b = f.indexOf(0, QStringLiteral("b"));
    f.state.selectClip(0, b);
    f.state.beginPreviewDrag(QStringLiteral("Speed"));
    f.state.previewSetClipSpeed(0, b, 1.5);
    f.state.previewSetClipSpeed(0, b, 2.0);
    f.state.commitPreviewDrag();
    const drift::Clip *cb = f.clip(0, QStringLiteral("b"));
    QVERIFY(nearly(cb->speed, 2.0));
    QVERIFY2(nearly(drift::usToSeconds(cb->timelineDuration), 2.0),
             qPrintable(QString::number(drift::usToSeconds(cb->timelineDuration))));
    QVERIFY(nearly(drift::usToSeconds(cb->srcOut - cb->srcIn), 4.0));
    QVERIFY(nearly(f.clip(0, QStringLiteral("a"))->speed, 1.0));
    QVERIFY(nearly(f.clip(0, QStringLiteral("c"))->speed, 1.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("c"))->timelineDuration), 4.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("c"))->timelineStart), 6.0));
    f.state.undo();
    QVERIFY(nearly(f.clip(0, QStringLiteral("b"))->speed, 1.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineDuration), 4.0));
}

// VEDIT P190 (조박사님, CapCut keyframes on the clip): a ◆ added on the clip's key line keys every
// value at once and is picked; Delete then takes out that ◆ (all its values), not the clip.
void EditorStateTest::parity_P190_clipKeyLineDiamonds()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("v"), 0.0, 6.0, drift::ClipType::Video);
    f.ready();
    f.state.addClipKeyframesAt(0, 0, 2.0);
    QVariantList marks = f.state.clipKeyframeMarks(0, 0);
    QCOMPARE(marks.size(), 1); // no stray ◆ at the clip's start
    QVERIFY(nearly(marks.first().toDouble(), 2.0));
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("x")).size(), 1);
    QCOMPARE(f.state.clipKeyframes(0, 0, QStringLiteral("opacity")).size(), 1);
    QCOMPARE(f.state.selectedVolumeKey().value(QStringLiteral("clipId")).toString(), QStringLiteral("v"));
    f.state.addClipKeyframesAt(0, 0, 4.0);
    QCOMPARE(f.state.clipKeyframeMarks(0, 0).size(), 2);
    f.state.deleteSelectedClip(); // the picked ◆ (4 s), not the clip
    QCOMPARE(f.clipCount(0), 1);
    marks = f.state.clipKeyframeMarks(0, 0);
    QCOMPARE(marks.size(), 1);
    QVERIFY(nearly(marks.first().toDouble(), 2.0));
    f.state.deleteClipKeyframesAt(0, 0, 2.0);
    QCOMPARE(f.state.clipKeyframeMarks(0, 0).size(), 0);
    QCOMPARE(f.clipCount(0), 1);
}

// VEDIT P191 (조박사님, Vegas): dragging an end of the overlap changes the transition's length and
// leaves the clips' far ends where they were.
void EditorStateTest::parity_P191_overlapEdgeDrag()
{
    ParityFixture f(1);
    f.add(0, QStringLiteral("a"), 0.0, 4.0, drift::ClipType::Video);
    f.add(0, QStringLiteral("b"), 3.0, 4.0, drift::ClipType::Video); // 1 s overlap
    f.ready();
    const int a = f.indexOf(0, QStringLiteral("a"));
    const int b = f.indexOf(0, QStringLiteral("b"));
    f.state.beginPreviewDrag(QStringLiteral("t"));
    f.state.previewOverlapEdge(0, a, b, 1, 3.5); // right end in: the outgoing clip ends earlier
    f.state.commitPreviewDrag();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 3.5));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineStart), 0.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 3.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineEnd()), 7.0));
    f.state.beginPreviewDrag(QStringLiteral("t"));
    f.state.previewOverlapEdge(0, a, b, -1, 3.2); // left end in: the incoming clip starts later
    f.state.commitPreviewDrag();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 3.2));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineEnd()), 7.0));
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("a"))->timelineEnd()), 3.5));
    f.state.undo();
    QVERIFY(nearly(drift::usToSeconds(f.clip(0, QStringLiteral("b"))->timelineStart), 3.0));
}
