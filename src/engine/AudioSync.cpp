#include "AudioSync.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace drift {

namespace {

using cd = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

void fft(std::vector<cd> &a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / double(len) * (inverse ? 1.0 : -1.0);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const cd u = a[i + j];
                const cd v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (cd &x : a)
            x /= double(n);
}

// 16 kHz -> 4 kHz by averaging fours (enough for where sounds land; much less to transform).
std::vector<double> decimate4(const std::vector<float> &pcm, size_t from = 0, size_t count = size_t(-1))
{
    const size_t end = std::min(pcm.size(), count == size_t(-1) ? pcm.size() : from + count);
    std::vector<double> out;
    out.reserve((end - std::min(end, from)) / 4 + 1);
    for (size_t i = from; i + 4 <= end; i += 4)
        out.push_back((double(pcm[i]) + pcm[i + 1] + pcm[i + 2] + pcm[i + 3]) * 0.25);
    return out;
}

struct ChunkAnswer
{
    double lagSeconds = 0.0; // where `other`'s start sits in `reference`
    double ratio = 0.0;      // peak over the best elsewhere
};

} // namespace

AudioSyncResult findAudioLag(const std::vector<float> &reference, const std::vector<float> &other,
                             int sampleRate, double maxLagSeconds)
{
    AudioSyncResult out;
    if (sampleRate <= 0 || reference.size() < size_t(sampleRate) * 3 || other.size() < size_t(sampleRate) * 3)
        return out;
    const int fs = sampleRate / 4; // the working rate

    // GCC-PHAT: every frequency weighted alike, only the phase — where a sound arrives — counts.
    // Different microphones, levels, rooms and tone colour drop out; a shared moment lines up.
    // (조박사님's two concert cameras: the loudness-onset match found the right 13 s but could not
    // trust it; this finds it from every piece alike.)
    const std::vector<double> ref = decimate4(reference);
    // Several pieces of `other`, each matched on its own against all of `reference`: when they
    // agree, it is the same event; when they scatter, the sounds are not the same.
    const size_t otherLen = other.size();
    const size_t chunk = std::min(otherLen, std::clamp<size_t>(otherLen / 3, size_t(sampleRate) * 8,
                                                              size_t(sampleRate) * 30));
    const int pieces = int(std::clamp<size_t>(otherLen / std::max<size_t>(1, chunk), 1, 5));
    size_t n = 1;
    while (n < ref.size() + chunk / 4 + 1)
        n <<= 1;
    std::vector<cd> fr(n);
    for (size_t i = 0; i < ref.size(); ++i)
        fr[i] = ref[i];
    fft(fr, false);

    std::vector<ChunkAnswer> answers;
    for (int p = 0; p < pieces; ++p) {
        const size_t start = pieces == 1 ? 0 : (otherLen - chunk) * size_t(p) / size_t(pieces - 1);
        const std::vector<double> piece = decimate4(other, start, chunk);
        std::vector<cd> fo(n);
        for (size_t i = 0; i < piece.size(); ++i)
            fo[i] = piece[i];
        fft(fo, false);
        for (size_t i = 0; i < n; ++i) {
            const cd r = fr[i] * std::conj(fo[i]);
            const double m = std::abs(r);
            fo[i] = m > 1e-12 ? r / m : cd(0.0, 0.0);
        }
        fft(fo, true);
        // index i is lag k: piece sample j matches reference sample j + k
        const double maxK = maxLagSeconds > 0 ? maxLagSeconds * fs + double(start / 4) : 1e300;
        long bestK = 0;
        double best = -1e300;
        for (size_t i = 0; i < n; ++i) {
            const long k = i < n / 2 ? long(i) : long(i) - long(n);
            if (std::abs(double(k)) > maxK)
                continue;
            const double v = fo[i].real();
            if (v > best) {
                best = v;
                bestK = k;
            }
        }
        double second = 1e-12;
        for (size_t i = 0; i < n; ++i) {
            const long k = i < n / 2 ? long(i) : long(i) - long(n);
            if (std::abs(k - bestK) > fs / 4 && std::abs(double(k)) <= maxK)
                second = std::max(second, fo[i].real());
        }
        answers.push_back({double(bestK) / fs - double(start) / sampleRate, best / second});
    }

    // The answer most pieces agree on (within 60 ms).
    int bestVotes = 0;
    double sum = 0.0, ratioSum = 0.0;
    for (const ChunkAnswer &a : answers) {
        int votes = 0;
        double s = 0.0, rs = 0.0;
        for (const ChunkAnswer &b : answers)
            if (std::abs(a.lagSeconds - b.lagSeconds) <= 0.06) {
                ++votes;
                s += b.lagSeconds;
                rs += b.ratio;
            }
        if (votes > bestVotes || (votes == bestVotes && rs > ratioSum)) {
            bestVotes = votes;
            sum = s;
            ratioSum = rs;
        }
    }
    const double rough = sum / std::max(1, bestVotes);
    const double meanRatio = ratioSum / std::max(1, bestVotes);
    const double agreement = double(bestVotes) / double(answers.size());
    out.confidence = std::clamp(agreement * std::min(1.0, (meanRatio - 1.0) / 1.5), 0.0, 1.0);
    // Several pieces must agree; a single piece has to stand out on its own.
    out.ok = answers.size() >= 2 ? (bestVotes >= 2 && agreement >= 0.5 && meanRatio >= 1.25)
                                 : meanRatio >= 2.0;

    // The pieces' agreeing answers, averaged: a quarter of a millisecond at the 4 kHz working rate
    // — finer than any frame. (A waveform refinement used to follow; between two different
    // microphones it could pull the answer off rather than sharpen it.)
    out.lagSeconds = std::round(rough * sampleRate) / sampleRate;
    return out;
}

} // namespace drift
