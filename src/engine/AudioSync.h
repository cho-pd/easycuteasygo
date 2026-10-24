#pragma once

// VEDIT: 소리로 맞추기 — find how far apart two recordings of the same moment are (two cameras,
// a phone and a recorder), from their sound alone. Offline, no model: an onset envelope finds
// the rough lag over the whole length, the waveform itself refines it to about a millisecond.

#include <vector>

namespace drift {

struct AudioSyncResult
{
    bool ok = false;
    // Where `other` sits against `reference`, in seconds: sample t of `other` is the sound at
    // t + lagSeconds of `reference`. Put other's start lagSeconds after reference's start and
    // they line up.
    double lagSeconds = 0.0;
    // 0..1: how clearly the best match stands out from the next best (and how alike the two
    // sounds are there). Below about 0.25 the answer is not trustworthy.
    double confidence = 0.0;
};

// Both buffers are mono at `sampleRate` (16 kHz from readMono16k). `maxLagSeconds` limits the
// search (<= 0: anything the lengths allow).
AudioSyncResult findAudioLag(const std::vector<float> &reference, const std::vector<float> &other,
                             int sampleRate, double maxLagSeconds = 0.0);

} // namespace drift
