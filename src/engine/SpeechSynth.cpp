#include "engine/SpeechSynth.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmreg.h>
#include <sapi.h>
#include <string>
#endif

namespace drift {

#ifdef Q_OS_WIN
namespace {

struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

template <typename T>
struct ComRef
{
    T *p = nullptr;
    ComRef() = default;
    ComRef(const ComRef &) = delete;
    ComRef &operator=(const ComRef &) = delete;
    ~ComRef()
    {
        if (p)
            p->Release();
    }
    void **out() { return reinterpret_cast<void **>(&p); }
    T *operator->() const { return p; }
};

bool hasHangul(const QString &text)
{
    for (const QChar ch : text) {
        const char16_t u = ch.unicode();
        if ((u >= 0xAC00 && u <= 0xD7A3) || (u >= 0x3131 && u <= 0x318E))
            return true;
    }
    return false;
}

// The first installed voice whose attributes match (Language=412 is Korean), or null. The caller
// owns the returned reference.
ISpObjectToken *voiceFor(const wchar_t *attributes)
{
    ComRef<ISpObjectTokenCategory> category;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                IID_ISpObjectTokenCategory, category.out()))
        || FAILED(category->SetId(SPCAT_VOICES, FALSE)))
        return nullptr;
    ComRef<IEnumSpObjectTokens> tokens;
    if (FAILED(category->EnumTokens(attributes, nullptr, &tokens.p)) || !tokens.p)
        return nullptr;
    ISpObjectToken *token = nullptr;
    if (tokens->Next(1, &token, nullptr) != S_OK)
        return nullptr;
    return token;
}

} // namespace

bool speechSynthesisAvailable()
{
    ComScope com;
    ComRef<ISpVoice> voice;
    return SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, voice.out()));
}

bool synthesizeSpeechToWav(const QString &text, const QString &wavPath, QString *error)
{
    const auto fail = [error](const char *why) {
        if (error)
            *error = QString::fromLatin1(why);
        return false;
    };
    if (text.trimmed().isEmpty())
        return fail("no text");
    QDir().mkpath(QFileInfo(wavPath).absolutePath());
    ComScope com;
    ComRef<ISpVoice> voice;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, voice.out())))
        return fail("no speech engine");
    if (hasHangul(text)) {
        ComRef<ISpObjectToken> korean;
        korean.p = voiceFor(L"Language=412");
        if (!korean.p)
            return fail("no Korean voice");
        voice->SetVoice(korean.p);
    }
    voice->SetRate(-1); // a touch slower than the default: easier to follow

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    ComRef<ISpStream> stream;
    if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, stream.out())))
        return fail("no speech stream");
    const std::wstring path = QDir::toNativeSeparators(wavPath).toStdWString();
    if (FAILED(stream->BindToFile(path.c_str(), SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &format, 0)))
        return fail("cannot write the sound file");
    voice->SetOutput(stream.p, TRUE);
    const std::wstring words = text.toStdWString();
    const HRESULT spoken = voice->Speak(words.c_str(), SPF_IS_NOT_XML, nullptr);
    voice->WaitUntilDone(INFINITE);
    voice->SetOutput(nullptr, FALSE);
    stream->Close();
    if (FAILED(spoken))
        return fail("speaking failed");
    if (QFileInfo(wavPath).size() <= 1000)
        return fail("the voice made no sound");
    return true;
}
#else
bool speechSynthesisAvailable()
{
    return false;
}

bool synthesizeSpeechToWav(const QString &, const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("not available on this system");
    return false;
}
#endif

} // namespace drift
