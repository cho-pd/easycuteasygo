#pragma once

// VEDIT: CapCut's 말풍선 (speech bubbles) and 편집효과 (word art) for text clips, made here from
// plain shapes and the existing shading layers — no CapCut assets.

#include "TextStyle.h"

#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace drift {

// The bubble drawn behind the words. `box` is the words' ink grown by the padding; the shape may
// reach past it (a tail, a cloud's puffs, a shout's spikes). An empty or unknown shape is the
// plain rounded box.
QStringList veditBubbleShapes();
QPainterPath veditBubblePath(const QString &shape, const QRectF &box, double radius);

// Ready-made bubbles ("bubble-…") and word art ("art-…"). They are not in textPresets() — the
// text page lists them on its own tabs — but textPresetForId() finds them, so the style
// thumbnails draw them like any other preset.
const QList<TextPreset> &veditBubblePresets();
const QList<TextPreset> &veditWordArtPresets();

// Puts a bubble / word art on a style, keeping its words, font, size and place. The look's
// sizes are scaled to the style's own letter size. "bubble-none" / "art-none" take it off.
bool applyVeditBubble(TextStyle &style, const QString &id);
bool applyVeditWordArt(TextStyle &style, const QString &id);

} // namespace drift
