#pragma once

#include <QString>

namespace drift {

// VEDIT: CapCut's 글자 읽어주기, offline — the text is spoken by a voice installed with Windows
// (SAPI; the Korean "Heami" ships with Korean Windows) into a 16-bit mono WAV file. A Korean voice
// is picked when the text has Hangul, otherwise the system default. Elsewhere: unavailable.
bool speechSynthesisAvailable();
bool synthesizeSpeechToWav(const QString &text, const QString &wavPath, QString *error = nullptr);

} // namespace drift
