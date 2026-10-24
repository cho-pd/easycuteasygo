#include "VoiceRecorder.h"

#include "AudioFileWriter.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace drift {

VoiceRecorder::VoiceRecorder(QObject *parent)
    : QObject(parent)
{
}

VoiceRecorder::~VoiceRecorder()
{
    cancel();
}

bool VoiceRecorder::microphoneAvailable()
{
    return !QMediaDevices::defaultAudioInput().isNull();
}

bool VoiceRecorder::start(QString *errorOut)
{
    if (m_source)
        return true;
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        if (errorOut)
            *errorOut = tr("No microphone was found");
        return false;
    }

    // Ask for float at 48 kHz; fall back to whatever the device prefers. Mono when it offers it —
    // a voice has nothing to say in stereo, and a mono clip draws one tall waveform lane.
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            format = device.preferredFormat();
    }
    if (format.sampleFormat() != QAudioFormat::Float && format.sampleFormat() != QAudioFormat::Int16
        && format.sampleFormat() != QAudioFormat::Int32) {
        if (errorOut)
            *errorOut = tr("The microphone uses a format this app cannot record");
        return false;
    }

    m_channels = qMax(1, format.channelCount());
    m_rate = format.sampleRate() > 0 ? format.sampleRate() : 48000;
    m_sampleFormat = int(format.sampleFormat());
    m_frames = 0;
    m_level = 0.0;
    m_holding = false; // VEDIT
    m_pending.clear();

    QDir().mkpath(generatedAudioDir());
    m_path = QDir(generatedAudioDir())
                 .filePath(QStringLiteral("voice-%1.flac")
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    m_writer = std::make_unique<AudioFileWriter>();
    QString error;
    if (!m_writer->open(m_path, m_rate, m_channels, &error)) {
        m_writer.reset();
        if (errorOut)
            *errorOut = error;
        return false;
    }

    m_source = new QAudioSource(device, format, this);
    m_io = m_source->start();
    if (!m_io || m_source->error() != QAudio::NoError) {
        if (errorOut)
            *errorOut = tr("The microphone could not be opened — check that Windows lets apps use it");
        cancel();
        return false;
    }
    connect(m_io, &QIODevice::readyRead, this, &VoiceRecorder::readAvailable);
    return true;
}

void VoiceRecorder::readAvailable()
{
    if (!m_io || !m_writer)
        return;
    m_pending += m_io->readAll();
    const int bytesPerSample = m_sampleFormat == int(QAudioFormat::Int16) ? 2 : 4;
    const int bytesPerFrame = bytesPerSample * m_channels;
    const int frames = int(m_pending.size() / bytesPerFrame);
    if (frames <= 0)
        return;
    // Whole frames only; a frame cut in half by the device waits for the rest of it.
    const QByteArray bytes = m_pending.left(frames * bytesPerFrame);
    m_pending.remove(0, frames * bytesPerFrame);
    std::vector<float> pcm(size_t(frames) * size_t(m_channels));
    const char *src = bytes.constData();
    float peak = 0.0f;
    for (int i = 0; i < frames * m_channels; ++i) {
        float v = 0.0f;
        if (m_sampleFormat == int(QAudioFormat::Float)) {
            std::memcpy(&v, src + i * 4, 4);
        } else if (m_sampleFormat == int(QAudioFormat::Int16)) {
            qint16 s;
            std::memcpy(&s, src + i * 2, 2);
            v = float(s) / 32768.0f;
        } else {
            qint32 s;
            std::memcpy(&s, src + i * 4, 4);
            v = float(double(s) / 2147483648.0);
        }
        pcm[size_t(i)] = v;
        peak = std::max(peak, std::fabs(v));
    }
    if (m_holding) { // VEDIT: before the picture moves — metered only
        m_level = qBound(0.0, double(peak), 1.0);
        emit levelChanged();
        return;
    }
    QString error;
    if (!m_writer->writeFrames(pcm.data(), frames, &error)) {
        // Stopped first, so whoever hears `failed` already sees isRecording() false.
        cancel();
        emit failed(error);
        return;
    }
    m_frames += frames;
    m_level = qBound(0.0, double(peak), 1.0);
    emit levelChanged();
}

void VoiceRecorder::markSyncPoint()
{
    if (!m_holding)
        return;
    readAvailable(); // what came in up to now is still before the mark
    m_holding = false;
}

double VoiceRecorder::seconds() const
{
    return m_rate > 0 ? double(m_frames) / double(m_rate) : 0.0;
}

QString VoiceRecorder::stop()
{
    if (!m_source)
        return {};
    readAvailable(); // whatever is still buffered
    if (!m_source)
        return {}; // the last write failed and cancelled the recording
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
    m_io = nullptr;
    m_level = 0.0;
    emit levelChanged();
    QString path;
    QString error;
    // Under a third of a second is a mis-click, not a recording.
    if (m_writer && m_frames >= m_rate / 3 && m_writer->finish(&error))
        path = m_path;
    else if (m_writer)
        m_writer->abort();
    m_writer.reset();
    return path;
}

void VoiceRecorder::cancel()
{
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
        m_io = nullptr;
    }
    if (m_writer) {
        m_writer->abort();
        m_writer.reset();
    }
    m_level = 0.0;
}

} // namespace drift
