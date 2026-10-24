#include "AudioOutputChannel.h"

#include <QAudioSink>
#include <cmath> // VEDIT: start ramp
#include <QDebug>
#include <QDateTime> // VEDIT: trace
#include <QDir> // VEDIT: trace
#include <QFile> // VEDIT: trace

#include <cstring>

namespace {

QString describeSinkError(QAudio::Error error)
{
    switch (error) {
    case QAudio::OpenError:
        return AudioOutputChannel::tr(
            "The audio device could not be opened. Another program may be using it exclusively.");
    case QAudio::IOError:
        return AudioOutputChannel::tr("The audio device stopped responding.");
    case QAudio::FatalError:
        return AudioOutputChannel::tr("The audio device was disconnected.");
    case QAudio::NoError:
        break;
    default:
        break; // UnderrunError is deprecated and no backend emits it
    }
    return QString();
}

} // namespace

namespace drift {

QAudioFormat negotiateAudioFormat(const QAudioDevice &device, int preferredRate)
{
    if (device.isNull())
        return QAudioFormat();

    QAudioFormat format;
    format.setSampleRate(preferredRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (device.isFormatSupported(format))
        return format;

    // Same shape at a rate the device admits to. Worth trying on its own because a device that
    // only runs at 44100 is far more common than one that cannot do float stereo at all, and
    // keeping float stereo means the mixer's output reaches the sink untouched.
    const QAudioFormat preferred = device.preferredFormat();
    format.setSampleRate(preferred.sampleRate());
    if (device.isFormatSupported(format))
        return format;

    return preferred;
}

void writeInterleaved(const float *stereo, int frames, const QAudioFormat &format, char *dst)
{
    const int channels = format.channelCount();
    const int bytesPerSample = format.bytesPerSample();
    if (!stereo || !dst || frames <= 0 || channels <= 0 || bytesPerSample <= 0)
        return;

    for (int frame = 0; frame < frames; ++frame) {
        const float left = stereo[frame * 2];
        const float right = stereo[frame * 2 + 1];
        for (int channel = 0; channel < channels; ++channel) {
            float value = 0.0f;
            if (channels == 1)
                value = 0.5f * (left + right);
            else if (channel == 0)
                value = left;
            else if (channel == 1)
                value = right;

            value = qBound(-1.0f, value, 1.0f);
            char *out = dst + static_cast<qsizetype>(frame * channels + channel) * bytesPerSample;
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const auto sample = static_cast<quint8>(value * 127.0f + 128.0f);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Int16: {
                const auto sample = static_cast<qint16>(value * 32767.0f);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Int32: {
                // Through double: 2147483647.0f rounds up to 2^31 as a float, which overflows the
                // cast at full scale.
                const auto sample = static_cast<qint32>(static_cast<double>(value) * 2147483647.0);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Float:
                std::memcpy(out, &value, sizeof(value));
                break;
            default:
                std::memset(out, 0, static_cast<size_t>(bytesPerSample));
                break;
            }
        }
    }
}

} // namespace drift

AudioPullDevice::AudioPullDevice(AudioOutputChannel *channel)
    : m_channel(channel)
{
    open(QIODevice::ReadOnly);
}

qint64 AudioPullDevice::readData(char *data, qint64 maxlen)
{
    if (!m_channel || !m_channel->m_fill || maxlen <= 0)
        return 0;

    const int bytesPerFrame = m_channel->m_format.bytesPerFrame();
    if (bytesPerFrame <= 0)
        return 0;

    const int frames = static_cast<int>(maxlen / bytesPerFrame);
    if (frames <= 0)
        return 0;

    // VEDIT: paused — silence keeps the device running (see AudioOutputChannel::pause), but only
    // a sliver of it (about 20 ms) is ever queued: a full buffer of silence (250 ms) had to play out
    // before the sound came back, and play started with a hitch (조박사님: "멈칫 하고 출발").
    if (m_channel->m_paused.load(std::memory_order_acquire)) {
        const int keepBytes = qMax(bytesPerFrame, m_channel->m_format.bytesForDuration(20000));
        if (QAudioSink *sink = m_channel->m_sink) {
            const qint64 queued = qMax<qint64>(0, sink->bufferSize() - sink->bytesFree());
            if (queued >= keepBytes)
                return 0;
        }
        const int silentFrames = qMin(frames, qMax(1, keepBytes / bytesPerFrame));
        std::memset(data, 0, static_cast<size_t>(silentFrames) * bytesPerFrame);
        return static_cast<qint64>(silentFrames) * bytesPerFrame;
    }

    if (m_channel->m_direct) {
        const int filled = m_channel->m_fill(reinterpret_cast<float *>(data), frames);
        m_channel->applyStartRamp(reinterpret_cast<float *>(data), filled); // VEDIT
        return static_cast<qint64>(filled) * bytesPerFrame;
    }

    m_channel->m_scratch.resize(frames * 2);
    const int filled = m_channel->m_fill(m_channel->m_scratch.data(), frames);
    m_channel->applyStartRamp(m_channel->m_scratch.data(), filled); // VEDIT
    drift::writeInterleaved(m_channel->m_scratch.constData(), filled, m_channel->m_format, data);
    return static_cast<qint64>(filled) * bytesPerFrame;
}

qint64 AudioPullDevice::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

qint64 AudioPullDevice::bytesAvailable() const
{
    return (1 << 20) + QIODevice::bytesAvailable();
}

AudioOutputChannel::AudioOutputChannel(const QString &threadName, QObject *parent)
    : QObject(parent)
    , m_pull(new AudioPullDevice(this))
{
    // The sink and its pull device run on their own thread so that refills — which synchronously
    // decode and mix audio — never block the GUI thread.
    m_pull->moveToThread(&m_thread);
    m_thread.setObjectName(threadName);
    m_thread.start();

    // Fires for a new or removed device *and* for the default one changing, which on Windows is
    // every headphone jack, HDMI monitor and Bluetooth headset.
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &AudioOutputChannel::onAudioOutputsChanged);
}

AudioOutputChannel::~AudioOutputChannel()
{
    m_running.store(false, std::memory_order_release);
    QMetaObject::invokeMethod(m_pull, [this] { destroySink(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_pull;
    m_pull = nullptr;
}

void AudioOutputChannel::setFillCallback(FillCallback fill)
{
    m_fill = std::move(fill);
}

void AudioOutputChannel::setPreferredSampleRate(int rate)
{
    if (rate <= 0 || rate == m_preferredRate)
        return;

    m_preferredRate = rate;
    // Without this the sink kept whatever rate the first project opened at, so a 44.1 kHz project
    // played back through a sink still running at 48 kHz.
    if (m_haveSink)
        rebuild();
}

void AudioOutputChannel::setDeviceId(const QByteArray &id)
{
    if (id == m_requestedId)
        return;

    m_requestedId = id;
    if (m_haveSink || m_running.load(std::memory_order_acquire))
        rebuild();
}

// VEDIT: a raised-cosine fade over the first 12 ms after start() — inaudible as a fade, but it
// removes the click a sound starting mid-waveform makes.
void AudioOutputChannel::applyStartRamp(float *stereo, int frames)
{
    if (m_rampRestart.exchange(false, std::memory_order_acq_rel))
        m_rampPos = 0;
    const int length = qMax(1, m_sampleRate.load(std::memory_order_acquire) * 12 / 1000);
    if (!stereo || frames <= 0 || m_rampPos >= length)
        return;
    for (int i = 0; i < frames && m_rampPos < length; ++i, ++m_rampPos) {
        const float t = float(m_rampPos) / float(length);
        const float g = 0.5f - 0.5f * std::cos(t * 3.14159265f);
        stereo[i * 2] *= g;
        stereo[i * 2 + 1] *= g;
    }
}

void AudioOutputChannel::start()
{
    m_recovered = false;
    m_rampRestart.store(true, std::memory_order_release); // VEDIT: fade in from silence
    m_running.store(true, std::memory_order_release);

    m_resumeQueuedUs.store(0, std::memory_order_release); // VEDIT
    if (!m_haveSink) {
        m_paused.store(false, std::memory_order_release);
        rebuild(); // builds and, seeing m_running, starts
        return;
    }

    // VEDIT: a paused device is still running — let the sound back in, and note how much silence
    // is still queued ahead of it so the playhead waits for it (else the picture runs ahead and
    // the start of the music seems missing). Blocking, but it only reads the sink's buffer.
    if (m_paused.load(std::memory_order_acquire)) {
        bool resumed = false;
        QMetaObject::invokeMethod(
            m_pull,
            [this, &resumed] {
                if (!m_paused.load(std::memory_order_acquire) || !m_sink || !m_sinkStarted
                    || m_sink->state() == QAudio::StoppedState)
                    return;
                const qint64 queuedBytes = qMax<qint64>(0, m_sink->bufferSize() - m_sink->bytesFree());
                m_resumeQueuedUs.store(m_sink->format().durationForBytes(int(queuedBytes)),
                                       std::memory_order_release);
                // VEDIT (temporary diagnosis: "멈칫" at play): how much silence a resume waits out.
                if (QFile f(QDir::temp().filePath(QStringLiteral("vedit-audio-trace.log")));
                    f.open(QIODevice::Append | QIODevice::Text))
                    f.write(QStringLiteral("%1 resume queued=%2ms buffer=%3ms\n")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                                .arg(m_resumeQueuedUs.load() / 1000)
                                .arg(m_sink->format().durationForBytes(int(m_sink->bufferSize())) / 1000)
                                .toUtf8());
                m_paused.store(false, std::memory_order_release);
                resumed = true;
                emit m_pull->readyRead(); // VEDIT: a sink gone idle on the short silence pulls again
            },
            Qt::BlockingQueuedConnection);
        if (resumed)
            return;
    }

    // Never block the GUI on sink I/O: start() may pull the first buffer, which opens and seeks
    // media, and that must stay on the audio thread.
    QMetaObject::invokeMethod(
        m_pull,
        [this] {
            // VEDIT: a paused device is still running — just let the sound back in.
            const bool resume = m_paused.load(std::memory_order_acquire) && m_sink && m_sinkStarted
                                && m_sink->state() != QAudio::StoppedState;
            m_paused.store(false, std::memory_order_release);
            if (resume)
                return;
            if (m_sink) {
                m_sink->start(m_pull);
                m_sinkStarted = true;
            }
        },
        Qt::QueuedConnection);
}

void AudioOutputChannel::setMuted(bool muted)
{
    m_muted.store(muted, std::memory_order_release);
    QMetaObject::invokeMethod(
        m_pull,
        [this, muted] {
            if (m_sink)
                m_sink->setVolume(muted ? 0.0 : 1.0);
        },
        Qt::QueuedConnection);
}

void AudioOutputChannel::pause()
{
    m_running.store(false, std::memory_order_release);
    if (!m_haveSink)
        return;
    // Blocking: the flag is set on the audio thread, so a refill already in progress finishes
    // before this returns and none starts after it.
    QMetaObject::invokeMethod(
        m_pull,
        [this] {
            if (m_sink && m_sinkStarted && m_sink->state() != QAudio::StoppedState)
                m_paused.store(true, std::memory_order_release);
            else if (m_sink)
                m_sink->stop();
        },
        Qt::BlockingQueuedConnection);
}

void AudioOutputChannel::stop()
{
    m_running.store(false, std::memory_order_release);
    // Blocking, so no further refill can run once this returns and the caller is free to reset the
    // state those refills read.
    QMetaObject::invokeMethod(
        m_pull,
        [this] {
            m_paused.store(false, std::memory_order_release); // VEDIT
            if (m_sink)
                m_sink->stop();
            m_sinkStarted = false; // VEDIT
        },
        Qt::BlockingQueuedConnection);
}

qint64 AudioOutputChannel::processedUSecs() const
{
    return m_sink ? m_sink->processedUSecs() : 0;
}

QAudioDevice AudioOutputChannel::resolveDevice() const
{
    if (m_requestedId.isEmpty())
        return QMediaDevices::defaultAudioOutput();

    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        if (device.id() == m_requestedId)
            return device;
    }
    // The chosen device is gone (unplugged, or a profile switch renamed it). Fall back to the
    // default without forgetting the choice, so re-plugging it picks it up again.
    return QMediaDevices::defaultAudioOutput();
}

void AudioOutputChannel::rebuild()
{
    // Blocking, and first: with the old sink closed no refill can be in flight, so the rate below
    // — which the fill callback reads — can be swapped without racing a running mix.
    QMetaObject::invokeMethod(m_pull, [this] { destroySink(); }, Qt::BlockingQueuedConnection);

    const QAudioDevice device = resolveDevice();
    const QAudioFormat format = drift::negotiateAudioFormat(device, m_preferredRate);
    if (device.isNull() || !format.isValid()) {
        m_haveSink = false;
        m_activeId.clear();
        emit errorOccurred(device.isNull()
                               ? tr("No audio output device is available.")
                               : tr("The audio device does not support playback of this project."));
        return;
    }

    m_activeId = device.id();
    m_haveSink = true;
    // Before the sink is built, so the caller has re-anchored its clock and mixer by the time the
    // first buffer is pulled at the new rate.
    if (m_sampleRate.exchange(format.sampleRate(), std::memory_order_acq_rel) != format.sampleRate())
        emit sampleRateChanged();

    QMetaObject::invokeMethod(
        m_pull, [this, device, format] { buildSink(device, format); }, Qt::QueuedConnection);
}

void AudioOutputChannel::buildSink(const QAudioDevice &device, const QAudioFormat &format)
{
    destroySink();

    m_format = format;
    m_direct = format.sampleFormat() == QAudioFormat::Float && format.channelCount() == 2;
    m_sink = new QAudioSink(device, format);
    if (m_muted.load(std::memory_order_acquire)) // VEDIT: a rebuilt sink keeps the mute
        m_sink->setVolume(0.0);
    // Keep the platform default buffer: a too-small buffer starves the pull loop and makes
    // playback run slow. Callers anchor the playhead to processedUSecs(), so buffer depth doesn't
    // affect sync.

    // Context is the sink, so this runs on the audio thread and dies with it.
    connect(m_sink, &QAudioSink::stateChanged, m_sink, [this](QAudio::State state) {
        if (state != QAudio::StoppedState || !m_running.load(std::memory_order_acquire))
            return;
        const QString message = describeSinkError(m_sink->error());
        if (message.isEmpty())
            return;
        QMetaObject::invokeMethod(
            this, [this, message] { handleSinkError(message); }, Qt::QueuedConnection);
    });

    if (m_running.load(std::memory_order_acquire)) {
        m_sink->start(m_pull);
        m_sinkStarted = true; // VEDIT
    }
}

void AudioOutputChannel::destroySink()
{
    if (!m_sink)
        return;
    m_sink->stop();
    delete m_sink;
    m_sink = nullptr;
    m_sinkStarted = false; // VEDIT
}

void AudioOutputChannel::onAudioOutputsChanged()
{
    if (!m_haveSink)
        return; // nothing open; the next start() negotiates against whatever is there then

    const QAudioDevice target = resolveDevice();
    if (!target.isNull() && target.id() == m_activeId)
        return;

    rebuild();
}

void AudioOutputChannel::handleSinkError(const QString &message)
{
    qWarning("AudioOutputChannel: %s", qPrintable(message));

    if (!m_recovered) {
        // Reopening is usually enough — the endpoint moved, or the session was invalidated under
        // us — and there is nothing to tell the user about a gap they did not hear. rebuild()
        // reports it if that fails too.
        m_recovered = true;
        rebuild();
        return;
    }

    emit errorOccurred(message);
}
