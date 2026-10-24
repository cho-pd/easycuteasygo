#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QAudioSource;
class QIODevice;

namespace drift {

class AudioFileWriter;

// VEDIT: records the default microphone to a FLAC file under generatedAudioDir() — CapCut's
// 녹음 (voiceover). Lives on the GUI thread: QAudioSource delivers in small pushes and the FLAC
// encode of a voice-rate stream is cheap.
class VoiceRecorder : public QObject
{
    Q_OBJECT

public:
    explicit VoiceRecorder(QObject *parent = nullptr);
    ~VoiceRecorder() override;

    // False with `errorOut` set when there is no microphone or it will not open.
    bool start(QString *errorOut);
    // Finishes the file and returns its path; empty when nothing usable was recorded.
    QString stop();
    void cancel();

    bool isRecording() const { return m_source != nullptr; }
    double seconds() const;
    // VEDIT (Codex 검토 5): while set, what the microphone hears is metered but not kept. The
    // picture starts moving a moment after the microphone does (the preview's preroll); keeping
    // that moment put the voice ahead of the picture by as much. markSyncPoint() starts the file.
    void holdUntilSyncPoint() { m_holding = true; }
    void markSyncPoint();
    // Peak of the last delivery, 0..1, for the level meter.
    double level() const { return m_level; }

    static bool microphoneAvailable();

signals:
    void levelChanged();
    void failed(const QString &message);

private:
    void readAvailable();

    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    std::unique_ptr<AudioFileWriter> m_writer;
    QString m_path;
    QByteArray m_pending; // bytes of a frame split across two deliveries
    int m_channels = 1;
    int m_rate = 48000;
    int m_sampleFormat = 0; // QAudioFormat::SampleFormat
    qint64 m_frames = 0;
    double m_level = 0.0;
    bool m_holding = false; // VEDIT
};

} // namespace drift
