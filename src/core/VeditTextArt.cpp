#include "VeditTextArt.h"

#include "TextLook.h"

#include <QCoreApplication>
#include <QtMath>

#include <cmath>

namespace drift {

namespace {

QString label(const char *s)
{
    return QCoreApplication::translate("TextStyle", s);
}

TextShadingLayer gradientFill(std::initializer_list<QColor> colours, double angle = 90.0)
{
    TextShadingLayer fill = solidFillLayer(Qt::white);
    fill.paint.kind = TextPaintKind::Gradient;
    fill.paint.gradient.stops.clear();
    const int n = int(colours.size());
    int i = 0;
    for (const QColor &c : colours)
        fill.paint.gradient.stops.append({n > 1 ? double(i++) / (n - 1) : 0.0, c});
    fill.paint.gradient.angle = angle;
    return fill;
}

TextShadingLayer extrude(const QColor &c, double depth)
{
    TextShadingLayer layer = solidFillLayer(c, QStringLiteral("extrude"));
    layer.kind = TextLayerKind::Extrude;
    layer.width = depth;
    layer.extrudeSteps = 10;
    layer.extrudeAngle = 45.0;
    layer.extrudeDarken = 0.35;
    return layer;
}

TextShadingLayer hardShadow(const QColor &c, double dx, double dy, const QString &id)
{
    return shadowLayer(c, dx, dy, 0.0, 1.0, id);
}

QList<TextShadingLayer> neon(const QColor &c)
{
    return {glowLayer(c, 38.0, 0.5, QStringLiteral("glowouter")), glowLayer(c, 14.0, 0.9, QStringLiteral("glow")),
            strokeLayer(2.0, c), solidFillLayer(QColor(255, 255, 255, 235))};
}

// Picture cards draw a preset at its own letter size; the looks are designed at 64 px, which
// comes out a few pixels tall on a card. Grow everything 4× so the cards can be told apart —
// applying scales back to the clip's own letters either way.
void grow(TextStyle &s, double f)
{
    s.pixelSize = int(std::lround(s.pixelSize * f));
    for (TextShadingLayer &l : s.layers) {
        l.width *= f;
        l.blur *= f;
        l.spread *= f;
    }
    s.boxPadding *= f;
    s.boxRadius *= f;
    s.bubbleBorderWidth *= f;
}

TextPreset art(const char *id, const char *name, const QList<TextShadingLayer> &layers)
{
    TextStyle s;
    s.fontFamily = QStringLiteral("검은고딕");
    s.fontWeight = 400;
    s.pixelSize = 64;
    s.layers = layers;
    grow(s, 4.0);
    return {QLatin1String(id), label(name), s, QStringLiteral("글자")};
}

TextPreset bubble(const char *id, const char *name, const char *shape, const QColor &fill, const QColor &border,
                  const QColor &words)
{
    TextStyle s;
    s.fontFamily = QStringLiteral("Pretendard");
    s.fontWeight = 700;
    s.pixelSize = 56;
    s.layers = {solidFillLayer(words)};
    s.boxEnabled = true;
    s.boxColor = fill;
    s.boxPadding = 20.0;
    s.boxRadius = 16.0;
    s.bubbleShape = QLatin1String(shape);
    s.bubbleBorderColor = border;
    s.bubbleBorderWidth = border.alpha() > 0 ? 4.0 : 0.0;
    grow(s, 2.6); // a bubble reaches well past its words; keep it inside the card
    return {QLatin1String(id), label(name), s, QStringLiteral("안녕!")};
}

} // namespace

QStringList veditBubbleShapes()
{
    return {QStringLiteral("speech"), QStringLiteral("speechRight"), QStringLiteral("thought"),
            QStringLiteral("cloud"), QStringLiteral("shout"), QStringLiteral("oval"),
            QStringLiteral("round"), QStringLiteral("ribbon"), QStringLiteral("tag")};
}

QPainterPath veditBubblePath(const QString &shape, const QRectF &b, double radius)
{
    QPainterPath path;
    const double w = b.width(), h = b.height();
    const double l = b.left(), t = b.top(), r = b.right(), btm = b.bottom();
    const QPointF c = b.center();
    if (shape == QLatin1String("round")) {
        path.addRoundedRect(b, h / 2.0, h / 2.0);
    } else if (shape == QLatin1String("oval")) {
        path.addEllipse(c, w * 0.62, h * 0.78);
    } else if (shape == QLatin1String("speech") || shape == QLatin1String("speechRight")) {
        const double rr = qMin(h * 0.45, w * 0.25);
        path.addRoundedRect(b, rr, rr);
        const bool right = shape == QLatin1String("speechRight");
        const double base = qMin(w * 0.14, h * 0.5);
        const double x0 = right ? r - w * 0.22 - base : l + w * 0.22;
        const double tipX = right ? x0 + base + h * 0.22 : x0 - h * 0.22;
        QPainterPath tail;
        tail.moveTo(x0, btm - rr * 0.5);
        tail.lineTo(tipX, btm + h * 0.48);
        tail.lineTo(x0 + base, btm - rr * 0.5);
        tail.closeSubpath();
        path = path.united(tail);
    } else if (shape == QLatin1String("thought")) {
        const QRectF body = b.adjusted(-h * 0.18, -h * 0.12, h * 0.18, h * 0.12);
        QPainterPath cloud;
        cloud.addRoundedRect(body, body.height() * 0.5, body.height() * 0.5);
        const int puffs = qMax(3, int(std::lround(body.width() / (body.height() * 0.7))));
        for (int i = 0; i < puffs; ++i) {
            const double x = body.left() + body.width() * (i + 0.5) / puffs;
            QPainterPath p;
            p.addEllipse(QPointF(x, body.top() + body.height() * 0.08), body.height() * 0.32, body.height() * 0.26);
            p.addEllipse(QPointF(x, body.bottom() - body.height() * 0.08), body.height() * 0.32, body.height() * 0.26);
            cloud = cloud.united(p);
        }
        path = cloud;
        path.addEllipse(QPointF(l + w * 0.16, body.bottom() + h * 0.30), h * 0.15, h * 0.13);
        path.addEllipse(QPointF(l + w * 0.08, body.bottom() + h * 0.58), h * 0.09, h * 0.08);
    } else if (shape == QLatin1String("cloud")) {
        QPainterPath cloud;
        cloud.addRoundedRect(b, h * 0.3, h * 0.3);
        const int puffs = qMax(3, int(std::lround(w / (h * 0.55))));
        for (int i = 0; i < puffs; ++i) {
            const double x = l + w * (i + 0.5) / puffs;
            const double rad = w / puffs * 0.62;
            QPainterPath p;
            p.addEllipse(QPointF(x, t), rad, rad * 0.9);
            p.addEllipse(QPointF(x, btm), rad, rad * 0.9);
            cloud = cloud.united(p);
        }
        QPainterPath sides;
        sides.addEllipse(QPointF(l, c.y()), h * 0.42, h * 0.48);
        sides.addEllipse(QPointF(r, c.y()), h * 0.42, h * 0.48);
        path = cloud.united(sides);
    } else if (shape == QLatin1String("shout")) {
        const int spikes = 16;
        QPolygonF poly;
        for (int i = 0; i < spikes * 2; ++i) {
            const double a = M_PI * 2.0 * i / (spikes * 2) - M_PI / 2.0;
            const bool outer = i % 2 == 0;
            const double jitter = outer ? (i % 6 == 0 ? 1.12 : (i % 6 == 2 ? 0.96 : 1.04)) : 1.0;
            const double rx = (outer ? w * 0.70 : w * 0.56) * jitter;
            const double ry = (outer ? h * 1.10 : h * 0.80) * jitter;
            poly << QPointF(c.x() + std::cos(a) * rx, c.y() + std::sin(a) * ry);
        }
        path.addPolygon(poly);
        path.closeSubpath();
    } else if (shape == QLatin1String("ribbon")) {
        const double e = h * 0.55, n = h * 0.25;
        QPolygonF poly{QPointF(l - e, t), QPointF(r + e, t), QPointF(r + e - n, c.y()), QPointF(r + e, btm),
                       QPointF(l - e, btm), QPointF(l - e + n, c.y())};
        path.addPolygon(poly);
        path.closeSubpath();
    } else if (shape == QLatin1String("tag")) {
        const double p = h * 0.45;
        QPolygonF poly{QPointF(l - p, c.y()), QPointF(l, t), QPointF(r, t), QPointF(r, btm), QPointF(l, btm)};
        path.addPolygon(poly);
        path.closeSubpath();
        path.addEllipse(QPointF(l - p * 0.35, c.y()), h * 0.07, h * 0.07);
        path.setFillRule(Qt::OddEvenFill);
    } else {
        path.addRoundedRect(b, radius, radius);
    }
    return path;
}

const QList<TextPreset> &veditBubblePresets()
{
    static const QList<TextPreset> presets = [] {
        const QColor none(0, 0, 0, 0);
        return QList<TextPreset>{
            bubble("bubble-speech-white", QT_TRANSLATE_NOOP("TextStyle", "White speech bubble"), "speech", Qt::white, QColor(20, 20, 20), QColor(20, 20, 20)),
            bubble("bubble-speech-yellow", QT_TRANSLATE_NOOP("TextStyle", "Yellow speech bubble"), "speechRight", QColor(255, 214, 64), QColor(20, 20, 20), QColor(20, 20, 20)),
            bubble("bubble-speech-black", QT_TRANSLATE_NOOP("TextStyle", "Black speech bubble"), "speech", QColor(17, 17, 17), Qt::white, Qt::white),
            bubble("bubble-speech-mint", QT_TRANSLATE_NOOP("TextStyle", "Mint speech bubble"), "speechRight", QColor(178, 240, 210), QColor(30, 120, 80), QColor(20, 70, 50)),
            bubble("bubble-thought", QT_TRANSLATE_NOOP("TextStyle", "Thought bubble"), "thought", Qt::white, QColor(90, 90, 90), QColor(30, 30, 30)),
            bubble("bubble-thought-pink", QT_TRANSLATE_NOOP("TextStyle", "Pink thought bubble"), "thought", QColor(255, 214, 236), QColor(220, 80, 150), QColor(140, 30, 90)),
            bubble("bubble-cloud-blue", QT_TRANSLATE_NOOP("TextStyle", "Blue cloud"), "cloud", QColor(200, 232, 255), QColor(30, 70, 140), QColor(20, 50, 110)),
            bubble("bubble-cloud-white", QT_TRANSLATE_NOOP("TextStyle", "White cloud"), "cloud", Qt::white, none, QColor(40, 40, 40)),
            bubble("bubble-shout-yellow", QT_TRANSLATE_NOOP("TextStyle", "Shout (yellow)"), "shout", QColor(255, 222, 50), QColor(220, 30, 30), QColor(210, 20, 20)),
            bubble("bubble-shout-red", QT_TRANSLATE_NOOP("TextStyle", "Shout (red)"), "shout", QColor(225, 35, 35), QColor(255, 214, 64), Qt::white),
            bubble("bubble-oval", QT_TRANSLATE_NOOP("TextStyle", "Round bubble"), "oval", Qt::white, QColor(20, 20, 20), QColor(20, 20, 20)),
            bubble("bubble-oval-yellow", QT_TRANSLATE_NOOP("TextStyle", "Yellow round bubble"), "oval", QColor(255, 236, 150), QColor(120, 80, 20), QColor(90, 55, 10)),
            bubble("bubble-pill-dark", QT_TRANSLATE_NOOP("TextStyle", "Dark pill"), "round", QColor(0, 0, 0, 200), none, Qt::white),
            bubble("bubble-pill-pink", QT_TRANSLATE_NOOP("TextStyle", "Pink pill"), "round", QColor(255, 79, 163), none, Qt::white),
            bubble("bubble-ribbon-red", QT_TRANSLATE_NOOP("TextStyle", "Red ribbon"), "ribbon", QColor(200, 30, 45), none, Qt::white),
            bubble("bubble-ribbon-navy", QT_TRANSLATE_NOOP("TextStyle", "Navy ribbon"), "ribbon", QColor(25, 40, 90), none, QColor(255, 214, 64)),
            bubble("bubble-tag-green", QT_TRANSLATE_NOOP("TextStyle", "Green tag"), "tag", QColor(30, 150, 70), none, Qt::white),
            bubble("bubble-sign", QT_TRANSLATE_NOOP("TextStyle", "Signboard"), "", QColor(250, 245, 230), QColor(110, 70, 30), QColor(80, 45, 15)),
        };
    }();
    return presets;
}

const QList<TextPreset> &veditWordArtPresets()
{
    static const QList<TextPreset> presets = [] {
        TextShadingLayer chrome = solidFillLayer(Qt::white);
        chrome.paint.kind = TextPaintKind::Effect;
        chrome.paint.effect.id = QStringLiteral("chrome");
        chrome.paint.effect.params.insert(QStringLiteral("colorA"), VectorSlotValue::fromColor(QColor(240, 245, 255)));
        chrome.paint.effect.params.insert(QStringLiteral("colorB"), VectorSlotValue::fromColor(QColor(40, 50, 70)));
        chrome.paint.effect.params.insert(QStringLiteral("bands"), VectorSlotValue::fromScalar(2));

        TextShadingLayer holo = solidFillLayer(Qt::white);
        holo.paint.kind = TextPaintKind::Gradient;
        for (const TextGradientPreset &p : textGradientPresets())
            if (p.id == QLatin1String("holo"))
                holo.paint.gradient = p.gradient;
        holo.paint.gradient.angle = 20;
        holo.paint.gradient.offsetSpeed = 0.15;
        holo.paint.gradient.repeat = true;
        holo.paint.gradient.space = TextGradientSpace::Glyph;

        TextShadingLayer gold = gradientFill({QColor(255, 246, 190), QColor(245, 183, 0), QColor(168, 107, 0)});
        TextShadingLayer sheen = solidFillLayer(Qt::white, QStringLiteral("shine"));
        sheen.paint.kind = TextPaintKind::Effect;
        sheen.paint.color = QColor(0, 0, 0, 0);
        sheen.paint.effect.id = QStringLiteral("shine");
        sheen.paint.effect.params.insert(QStringLiteral("speed"), VectorSlotValue::fromScalar(0.5));
        sheen.paint.effect.params.insert(QStringLiteral("width"), VectorSlotValue::fromScalar(0.25));
        sheen.paint.effect.params.insert(QStringLiteral("angle"), VectorSlotValue::fromScalar(20));
        sheen.paint.effect.params.insert(QStringLiteral("intensity"), VectorSlotValue::fromScalar(0.9));
        sheen.blend = BlendMode::Screen;

        TextShadingLayer hollowFill = solidFillLayer(Qt::white);
        hollowFill.enabled = false;

        TextShadingLayer glitchA = solidFillLayer(QColor(255, 0, 255), QStringLiteral("glitcha"));
        glitchA.offsetX = -3;
        glitchA.blend = BlendMode::Screen;
        glitchA.opacity = 0.8;
        TextShadingLayer glitchB = solidFillLayer(QColor(0, 255, 255), QStringLiteral("glitchb"));
        glitchB.offsetX = 3;
        glitchB.blend = BlendMode::Screen;
        glitchB.opacity = 0.8;

        const QColor black(20, 20, 20);
        return QList<TextPreset>{
            art("art-gold", QT_TRANSLATE_NOOP("TextStyle", "Gold"),
                {shadowLayer(Qt::black, 0, 3, 4, 0.5), strokeLayer(2, QColor(90, 58, 0)), gold}),
            art("art-silver", QT_TRANSLATE_NOOP("TextStyle", "Silver metal"),
                {shadowLayer(Qt::black, 0, 3, 4, 0.5), strokeLayer(1, QColor(42, 42, 42)), chrome}),
            art("art-rainbow", QT_TRANSLATE_NOOP("TextStyle", "Rainbow"),
                {strokeLayer(4, Qt::white), gradientFill({QColor(255, 59, 48), QColor(255, 149, 0), QColor(255, 214, 10),
                                                          QColor(52, 199, 89), QColor(10, 132, 255), QColor(175, 82, 222)}, 0)}),
            art("art-sunset", QT_TRANSLATE_NOOP("TextStyle", "Sunset"),
                {strokeLayer(3, Qt::white), gradientFill({QColor(255, 213, 107), QColor(255, 107, 107), QColor(193, 53, 132)})}),
            art("art-ocean", QT_TRANSLATE_NOOP("TextStyle", "Ocean"),
                {strokeLayer(2, QColor(0, 58, 87)), gradientFill({QColor(161, 255, 255), QColor(0, 131, 176)})}),
            art("art-candy", QT_TRANSLATE_NOOP("TextStyle", "Candy"),
                {glowLayer(QColor(255, 79, 163), 10, 0.7), strokeLayer(6, Qt::white),
                 gradientFill({QColor(255, 182, 225), QColor(255, 79, 163)})}),
            art("art-mint", QT_TRANSLATE_NOOP("TextStyle", "Mint"),
                {strokeLayer(3, QColor(45, 106, 79)), gradientFill({QColor(212, 252, 121), QColor(150, 230, 161)})}),
            art("art-fire", QT_TRANSLATE_NOOP("TextStyle", "Fire"),
                {glowLayer(QColor(255, 120, 0), 14, 0.8), gradientFill({QColor(255, 240, 120), QColor(255, 140, 0), QColor(220, 30, 0)})}),
            art("art-ice", QT_TRANSLATE_NOOP("TextStyle", "Ice"),
                {glowLayer(QColor(160, 231, 255), 10, 0.8), strokeLayer(1.5, QColor(58, 123, 213)),
                 gradientFill({Qt::white, QColor(160, 231, 255)})}),
            art("art-neon-pink", QT_TRANSLATE_NOOP("TextStyle", "Pink neon"), neon(QColor(255, 60, 220))),
            art("art-neon-blue", QT_TRANSLATE_NOOP("TextStyle", "Blue neon"), neon(QColor(47, 155, 255))),
            art("art-neon-green", QT_TRANSLATE_NOOP("TextStyle", "Green neon"), neon(QColor(57, 255, 20))),
            art("art-3d-red", QT_TRANSLATE_NOOP("TextStyle", "3D red"),
                {extrude(QColor(120, 0, 0), 10), strokeLayer(2, Qt::white), solidFillLayer(QColor(255, 75, 75))}),
            art("art-3d-yellow", QT_TRANSLATE_NOOP("TextStyle", "3D yellow"),
                {extrude(QColor(140, 95, 0), 10), strokeLayer(2, black), solidFillLayer(QColor(255, 214, 64))}),
            art("art-3d-blue", QT_TRANSLATE_NOOP("TextStyle", "3D blue"),
                {extrude(QColor(10, 30, 90), 10), strokeLayer(2, Qt::white), solidFillLayer(QColor(79, 195, 247))}),
            art("art-hollow-white", QT_TRANSLATE_NOOP("TextStyle", "Hollow white"), {strokeLayer(3, Qt::white), hollowFill}),
            art("art-hollow-yellow", QT_TRANSLATE_NOOP("TextStyle", "Hollow yellow"),
                {strokeLayer(3, QColor(255, 214, 64)), hollowFill}),
            art("art-comic", QT_TRANSLATE_NOOP("TextStyle", "Comic"),
                {hardShadow(black, 6, 6, QStringLiteral("shadow")), strokeLayer(7, black), solidFillLayer(QColor(255, 222, 50))}),
            art("art-retro", QT_TRANSLATE_NOOP("TextStyle", "Retro"),
                {hardShadow(QColor(47, 95, 200), 8, 8, QStringLiteral("shadowb")),
                 hardShadow(QColor(230, 60, 60), 4, 4, QStringLiteral("shadowa")), solidFillLayer(QColor(255, 233, 168))}),
            art("art-echo", QT_TRANSLATE_NOOP("TextStyle", "Echo"),
                {shadowLayer(QColor(255, 79, 163), 12, 12, 0, 0.3, QStringLiteral("echo3")),
                 shadowLayer(QColor(255, 79, 163), 8, 8, 0, 0.5, QStringLiteral("echo2")),
                 shadowLayer(QColor(255, 79, 163), 4, 4, 0, 0.8, QStringLiteral("echo1")), solidFillLayer(Qt::white)}),
            art("art-glitch", QT_TRANSLATE_NOOP("TextStyle", "Glitch"), {glitchA, glitchB, solidFillLayer(Qt::white)}),
            art("art-holo", QT_TRANSLATE_NOOP("TextStyle", "Hologram"), {glowLayer(Qt::white, 8, 0.4), holo}),
            art("art-shine", QT_TRANSLATE_NOOP("TextStyle", "Shining gold"),
                {shadowLayer(Qt::black, 0, 3, 4, 0.5), strokeLayer(2, QColor(90, 58, 0)), gold, sheen}),
            art("art-crown", QT_TRANSLATE_NOOP("TextStyle", "Crown gold"),
                {glowLayer(QColor(142, 36, 255), 16, 0.8), strokeLayer(3, QColor(59, 10, 87)), gold}),
        };
    }();
    return presets;
}

bool applyVeditBubble(TextStyle &style, const QString &id)
{
    if (id == QLatin1String("bubble-none")) {
        style.boxEnabled = false;
        style.bubbleShape.clear();
        style.bubbleBorderWidth = 0.0;
        return true;
    }
    for (const TextPreset &p : veditBubblePresets()) {
        if (p.id != id)
            continue;
        const double k = style.pixelSize / double(qMax(1, p.style.pixelSize));
        style.boxEnabled = true;
        style.boxColor = p.style.boxColor;
        style.boxPadding = p.style.boxPadding * k;
        style.boxRadius = p.style.boxRadius * k;
        style.bubbleShape = p.style.bubbleShape;
        style.bubbleBorderColor = p.style.bubbleBorderColor;
        style.bubbleBorderWidth = p.style.bubbleBorderWidth * k;
        style.layers = p.style.layers; // the words in the bubble's own colour, plain
        style.packId.clear();
        style.lookId.clear();
        return true;
    }
    return false;
}

bool applyVeditWordArt(TextStyle &style, const QString &id)
{
    if (id == QLatin1String("art-none")) {
        QColor keep = Qt::white;
        if (const TextShadingLayer *fill = firstTextLayerOfKind(style.layers, TextLayerKind::Fill))
            if (fill->paint.kind == TextPaintKind::Solid)
                keep = fill->paint.color;
        style.layers = {solidFillLayer(keep)};
        style.packId.clear();
        style.lookId.clear();
        return true;
    }
    for (const TextPreset &p : veditWordArtPresets()) {
        if (p.id != id)
            continue;
        // Widths and blurs are pixels at the look's own 64 px letters; offsets follow the size.
        const double k = style.pixelSize / double(qMax(1, p.style.pixelSize));
        style.layers = p.style.layers;
        for (TextShadingLayer &l : style.layers) {
            l.width *= k;
            l.blur *= k;
            l.spread *= k;
        }
        style.boxEnabled = false; // word art stands on its own, like CapCut's
        style.bubbleShape.clear();
        style.packId.clear();
        style.lookId.clear();
        return true;
    }
    return false;
}

} // namespace drift
