#include "ClipAnimation.h"

#include <QEasingCurve>

#include <algorithm>
#include <cmath>

namespace drift {

QString clipAnimKindToString(ClipAnimKind kind)
{
    switch (kind) {
    case ClipAnimKind::None:
        return QStringLiteral("none");
    case ClipAnimKind::Fade:
        return QStringLiteral("fade");
    case ClipAnimKind::SlideUp:
        return QStringLiteral("slideUp");
    case ClipAnimKind::SlideDown:
        return QStringLiteral("slideDown");
    case ClipAnimKind::SlideLeft:
        return QStringLiteral("slideLeft");
    case ClipAnimKind::SlideRight:
        return QStringLiteral("slideRight");
    case ClipAnimKind::ZoomIn:
        return QStringLiteral("zoomIn");
    case ClipAnimKind::ZoomOut:
        return QStringLiteral("zoomOut");
    case ClipAnimKind::Pop:
        return QStringLiteral("pop");
    case ClipAnimKind::SpinCW:
        return QStringLiteral("spinCW");
    case ClipAnimKind::SpinCCW:
        return QStringLiteral("spinCCW");
    case ClipAnimKind::Bounce:
        return QStringLiteral("bounce");
    case ClipAnimKind::Swing:
        return QStringLiteral("swing");
    case ClipAnimKind::Shake:
        return QStringLiteral("shake");
    case ClipAnimKind::Drop:
        return QStringLiteral("drop");
    case ClipAnimKind::ZoomSpin:
        return QStringLiteral("zoomSpin");
    case ClipAnimKind::FlyBig:
        return QStringLiteral("flyBig");
    case ClipAnimKind::FlipH:
        return QStringLiteral("flipH");
    case ClipAnimKind::FlipV:
        return QStringLiteral("flipV");
    case ClipAnimKind::Jelly:
        return QStringLiteral("jelly");
    case ClipAnimKind::StretchH:
        return QStringLiteral("stretchH");
    case ClipAnimKind::StretchV:
        return QStringLiteral("stretchV");
    case ClipAnimKind::RiseUp:
        return QStringLiteral("riseUp");
    case ClipAnimKind::Whirl:
        return QStringLiteral("whirl");
    case ClipAnimKind::Elastic:
        return QStringLiteral("elastic");
    case ClipAnimKind::Diagonal:
        return QStringLiteral("diagonal");
    case ClipAnimKind::WhooshLeft:
        return QStringLiteral("whooshLeft");
    case ClipAnimKind::WhooshRight:
        return QStringLiteral("whooshRight");
    case ClipAnimKind::Blink:
        return QStringLiteral("blink");
    case ClipAnimKind::Turn3D:
        return QStringLiteral("turn3d");
    case ClipAnimKind::Tilt3D:
        return QStringLiteral("tilt3d");
    case ClipAnimKind::GlowIn:
        return QStringLiteral("glowIn");
    case ClipAnimKind::GlitchIn:
        return QStringLiteral("glitchIn");
    case ClipAnimKind::WipeIn:
        return QStringLiteral("wipeIn");
    case ClipAnimKind::BlurIn:
        return QStringLiteral("blurIn");
    case ClipAnimKind::FadeRise:
        return QStringLiteral("fadeRise");
    case ClipAnimKind::Squeeze:
        return QStringLiteral("squeeze");
    case ClipAnimKind::RollIn:
        return QStringLiteral("rollIn");
    case ClipAnimKind::Heartbeat:
        return QStringLiteral("heartbeat");
    case ClipAnimKind::Spin3D:
        return QStringLiteral("spin3d");
    case ClipAnimKind::FlipUp3D:
        return QStringLiteral("flipUp3d");
    case ClipAnimKind::ZoomBlurIn:
        return QStringLiteral("zoomBlurIn");
    case ClipAnimKind::FlashIn:
        return QStringLiteral("flashIn");
    }
    return QStringLiteral("none");
}

ClipAnimKind clipAnimKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("fade"))
        return ClipAnimKind::Fade;
    if (kind == QStringLiteral("slideUp"))
        return ClipAnimKind::SlideUp;
    if (kind == QStringLiteral("slideDown"))
        return ClipAnimKind::SlideDown;
    if (kind == QStringLiteral("slideLeft"))
        return ClipAnimKind::SlideLeft;
    if (kind == QStringLiteral("slideRight"))
        return ClipAnimKind::SlideRight;
    if (kind == QStringLiteral("zoomIn"))
        return ClipAnimKind::ZoomIn;
    if (kind == QStringLiteral("zoomOut"))
        return ClipAnimKind::ZoomOut;
    if (kind == QStringLiteral("pop"))
        return ClipAnimKind::Pop;
    if (kind == QStringLiteral("spinCW"))
        return ClipAnimKind::SpinCW;
    if (kind == QStringLiteral("spinCCW"))
        return ClipAnimKind::SpinCCW;
    if (kind == QStringLiteral("bounce"))
        return ClipAnimKind::Bounce;
    if (kind == QStringLiteral("swing"))
        return ClipAnimKind::Swing;
    if (kind == QStringLiteral("shake"))
        return ClipAnimKind::Shake;
    if (kind == QStringLiteral("drop"))
        return ClipAnimKind::Drop;
    if (kind == QStringLiteral("zoomSpin"))
        return ClipAnimKind::ZoomSpin;
    if (kind == QStringLiteral("flyBig"))
        return ClipAnimKind::FlyBig;
    if (kind == QStringLiteral("flipH"))
        return ClipAnimKind::FlipH;
    if (kind == QStringLiteral("flipV"))
        return ClipAnimKind::FlipV;
    if (kind == QStringLiteral("jelly"))
        return ClipAnimKind::Jelly;
    if (kind == QStringLiteral("stretchH"))
        return ClipAnimKind::StretchH;
    if (kind == QStringLiteral("stretchV"))
        return ClipAnimKind::StretchV;
    if (kind == QStringLiteral("riseUp"))
        return ClipAnimKind::RiseUp;
    if (kind == QStringLiteral("whirl"))
        return ClipAnimKind::Whirl;
    if (kind == QStringLiteral("elastic"))
        return ClipAnimKind::Elastic;
    if (kind == QStringLiteral("diagonal"))
        return ClipAnimKind::Diagonal;
    if (kind == QStringLiteral("whooshLeft"))
        return ClipAnimKind::WhooshLeft;
    if (kind == QStringLiteral("whooshRight"))
        return ClipAnimKind::WhooshRight;
    if (kind == QStringLiteral("blink"))
        return ClipAnimKind::Blink;
    if (kind == QStringLiteral("turn3d"))
        return ClipAnimKind::Turn3D;
    if (kind == QStringLiteral("tilt3d"))
        return ClipAnimKind::Tilt3D;
    if (kind == QStringLiteral("glowIn"))
        return ClipAnimKind::GlowIn;
    if (kind == QStringLiteral("glitchIn"))
        return ClipAnimKind::GlitchIn;
    if (kind == QStringLiteral("wipeIn"))
        return ClipAnimKind::WipeIn;
    if (kind == QStringLiteral("blurIn"))
        return ClipAnimKind::BlurIn;
    if (kind == QStringLiteral("fadeRise"))
        return ClipAnimKind::FadeRise;
    if (kind == QStringLiteral("squeeze"))
        return ClipAnimKind::Squeeze;
    if (kind == QStringLiteral("rollIn"))
        return ClipAnimKind::RollIn;
    if (kind == QStringLiteral("heartbeat"))
        return ClipAnimKind::Heartbeat;
    if (kind == QStringLiteral("spin3d"))
        return ClipAnimKind::Spin3D;
    if (kind == QStringLiteral("flipUp3d"))
        return ClipAnimKind::FlipUp3D;
    if (kind == QStringLiteral("zoomBlurIn"))
        return ClipAnimKind::ZoomBlurIn;
    if (kind == QStringLiteral("flashIn"))
        return ClipAnimKind::FlashIn;
    return ClipAnimKind::None;
}

QString clipAnimEaseToString(ClipAnimEase ease)
{
    switch (ease) {
    case ClipAnimEase::Linear:
        return QStringLiteral("linear");
    case ClipAnimEase::EaseInOut:
        return QStringLiteral("easeInOut");
    case ClipAnimEase::Back:
        return QStringLiteral("back");
    case ClipAnimEase::EaseOut:
        return QStringLiteral("easeOut");
    }
    return QStringLiteral("easeOut");
}

ClipAnimEase clipAnimEaseFromString(const QString &ease)
{
    if (ease == QStringLiteral("linear"))
        return ClipAnimEase::Linear;
    if (ease == QStringLiteral("easeInOut"))
        return ClipAnimEase::EaseInOut;
    if (ease == QStringLiteral("back"))
        return ClipAnimEase::Back;
    return ClipAnimEase::EaseOut;
}

FadeCurve clipAnimEaseToCurve(ClipAnimEase ease)
{
    switch (ease) {
    case ClipAnimEase::Linear:
        return FadeCurve::Linear;
    case ClipAnimEase::EaseInOut:
        return FadeCurve::Smooth;
    case ClipAnimEase::Back:
        return FadeCurve::Smooth;
    case ClipAnimEase::EaseOut:
        return FadeCurve::EqualPower;
    }
    return FadeCurve::Smooth;
}

ClipAnimEase clipAnimCurveToEase(FadeCurve curve)
{
    switch (curve) {
    case FadeCurve::Linear:
        return ClipAnimEase::Linear;
    case FadeCurve::Smooth:
        return ClipAnimEase::EaseInOut;
    case FadeCurve::EqualPower:
        return ClipAnimEase::EaseOut;
    case FadeCurve::Custom:
        return ClipAnimEase::EaseInOut;
    case FadeCurve::Bezier:
        // The legacy ease enum predates both hand-drawn modes and has no member for either.
        return ClipAnimEase::EaseInOut;
    }
    return ClipAnimEase::EaseOut;
}

QJsonObject clipAnimationToJson(const ClipAnimation &anim)
{
    QJsonObject object{
        {QStringLiteral("kind"), clipAnimKindToString(anim.kind)},
        {QStringLiteral("durationUs"), static_cast<double>(anim.durationUs)},
        {QStringLiteral("ease"), clipAnimEaseToString(anim.ease)},
        {QStringLiteral("curve"), fadeCurveToString(anim.curve)},
    };
    if (anim.curve == FadeCurve::Custom && !anim.shape.isEmpty())
        object.insert(QStringLiteral("shape"), anim.shape.toJson());
    return object;
}

ClipAnimation clipAnimationFromJson(const QJsonObject &object)
{
    ClipAnimation anim;
    anim.kind = clipAnimKindFromString(object.value(QStringLiteral("kind")).toString());
    if (object.contains(QStringLiteral("durationUs")))
        anim.durationUs = static_cast<TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
    else if (object.contains(QStringLiteral("duration")))
        anim.durationUs = secondsToUs(object.value(QStringLiteral("duration")).toDouble(0.5));
    anim.ease = clipAnimEaseFromString(object.value(QStringLiteral("ease")).toString());
    if (object.contains(QStringLiteral("curve"))) {
        anim.curve = fadeCurveFromString(object.value(QStringLiteral("curve")).toString());
    } else {
        // Older projects only stored ease — map into the shared style system.
        anim.curve = clipAnimEaseToCurve(anim.ease);
    }
    if (object.contains(QStringLiteral("shape")))
        anim.shape = FadeShape::fromJson(object.value(QStringLiteral("shape")));
    anim.ease = clipAnimCurveToEase(anim.curve);
    return anim;
}

// settled: 0 = fully away, 1 = in place. entering flips slide/spin departure direction.
void applyKind(ClipAnimKind kind, double settled, bool entering, double layoutW, double layoutH,
               FadeCurve curve, const FadeShape &shape, ClipAnimSample *out)
{
    if (kind == ClipAnimKind::None || kind == ClipAnimKind::Fade || !out)
        return;

    // VEDIT (조박사님: movements too plain): the default curve is CapCut's — quick off the mark
    // and a long soft landing (quartic ease-out; an exit mirrors it, so it speeds away). The other
    // curves keep their own shapes.
    const double p = qBound(0.0, settled, 1.0);
    const double a = curve == FadeCurve::Smooth ? 1.0 - std::pow(1.0 - p, 4.0)
                                                : shapedProgress(p, curve, shape);
    const double away = 1.0 - a;
    // Further to travel than before (35 %), and fully visible after the first third of the move
    // rather than fading the whole way: the movement is the effect, not a dissolve.
    const double travelX = 0.5 * layoutW;
    const double travelY = 0.5 * layoutH;
    const double seen = qBound(0.0, p * 3.0, 1.0);
    const double sign = entering ? 1.0 : -1.0;

    switch (kind) {
    case ClipAnimKind::None:
    case ClipAnimKind::Fade:
        break;
    case ClipAnimKind::SlideUp:
        out->dy += sign * away * travelY;
        out->opacity *= seen;
        break;
    case ClipAnimKind::SlideDown:
        out->dy -= sign * away * travelY;
        out->opacity *= seen;
        break;
    case ClipAnimKind::SlideLeft:
        out->dx += sign * away * travelX;
        out->opacity *= seen;
        break;
    case ClipAnimKind::SlideRight:
        out->dx -= sign * away * travelX;
        out->opacity *= seen;
        break;
    case ClipAnimKind::ZoomIn:
        out->scale *= 0.3 + 0.7 * a;
        out->opacity *= seen;
        break;
    case ClipAnimKind::ZoomOut:
        out->scale *= entering ? (1.8 - 0.8 * a) : (1.0 + 0.8 * away);
        out->opacity *= seen;
        break;
    case ClipAnimKind::Pop:
        out->scale *= 0.6 + 0.4 * a;
        out->opacity *= qBound(0.0, a, 1.0);
        break;
    case ClipAnimKind::SpinCW:
        out->rotationDeg += sign * away * -90.0;
        out->opacity *= a;
        break;
    case ClipAnimKind::SpinCCW:
        out->rotationDeg += sign * away * 90.0;
        out->opacity *= a;
        break;
    case ClipAnimKind::Bounce: {
        const double b = QEasingCurve(QEasingCurve::OutBounce).valueForProgress(a);
        out->dy += sign * (1.0 - b) * travelY;
        out->opacity *= qBound(0.0, a * 4.0, 1.0);
        break;
    }
    // VEDIT: damped swings use the raw progress so the wobble count does not depend on the curve.
    case ClipAnimKind::Swing: {
        const double t = qBound(0.0, settled, 1.0);
        out->rotationDeg += 18.0 * std::exp(-4.0 * t) * std::cos(3.0 * 6.28318530718 * t) * (1.0 - t);
        out->opacity *= qBound(0.0, t * 5.0, 1.0);
        break;
    }
    case ClipAnimKind::Shake: {
        const double t = qBound(0.0, settled, 1.0);
        out->dx += 0.04 * layoutW * (1.0 - t) * std::sin(6.0 * 6.28318530718 * t);
        out->opacity *= qBound(0.0, t * 5.0, 1.0);
        break;
    }
    case ClipAnimKind::Drop: {
        const double b = QEasingCurve(QEasingCurve::OutBounce).valueForProgress(a);
        out->dy -= sign * (1.0 - b) * travelY;
        out->opacity *= qBound(0.0, a * 4.0, 1.0);
        break;
    }
    case ClipAnimKind::ZoomSpin:
        out->scale *= 0.2 + 0.8 * a;
        out->rotationDeg += sign * away * 180.0;
        out->opacity *= a;
        break;
    case ClipAnimKind::FlyBig:
        out->scale *= 1.0 + 0.8 * away;
        out->opacity *= a;
        break;
    // VEDIT: card flips turn in real 3D now (they used to squash flat); edge-on they vanish.
    case ClipAnimKind::FlipH:
        out->yawDeg += sign * away * 90.0;
        out->opacity *= away > 0.98 ? 0.0 : 1.0;
        break;
    case ClipAnimKind::FlipV:
        out->pitchDeg += sign * away * 90.0;
        out->opacity *= away > 0.98 ? 0.0 : 1.0;
        break;
    case ClipAnimKind::Turn3D:
        out->yawDeg += sign * away * -75.0;
        out->dx += sign * away * 0.3 * layoutW;
        out->opacity *= seen;
        break;
    case ClipAnimKind::Tilt3D:
        out->pitchDeg += sign * away * 70.0;
        out->dy -= sign * away * 0.15 * layoutH;
        out->opacity *= seen;
        break;
    // VEDIT: special entrances (an exit plays them backwards).
    case ClipAnimKind::GlowIn:
        out->flash += 1.6 * away * away;
        out->scale *= 1.0 + 0.12 * away;
        out->opacity *= qBound(0.0, p * 4.0, 1.0);
        break;
    case ClipAnimKind::GlitchIn: {
        // Steps of a jittery cut-in: sharp on/off flicker early, the breakage fading as it settles.
        const double t = qBound(0.0, settled, 1.0);
        const double step = std::floor(t * 14.0);
        const double jitter = std::sin(step * 12.9898) * 43758.5453;
        const double r = jitter - std::floor(jitter); // 0..1 per step
        const double broken = 1.0 - t;
        out->split += broken * (r > 0.5 ? 0.03 : -0.03);
        out->slice += broken * (r > 0.3 ? 1.0 : 0.3);
        out->dx += broken * (r - 0.5) * 0.06 * layoutW;
        out->opacity *= (t < 0.6 && r < 0.25) ? 0.2 : 1.0;
        break;
    }
    case ClipAnimKind::WipeIn:
        out->reveal *= a;
        break;
    case ClipAnimKind::BlurIn:
        out->blurPx += 0.04 * layoutH * away;
        out->scale *= 1.0 + 0.15 * away;
        out->opacity *= qBound(0.0, p * 2.5, 1.0);
        break;
    // VEDIT overhaul stage 7.
    case ClipAnimKind::FadeRise:
        out->dy += sign * away * 0.08 * layoutH;
        out->opacity *= a;
        break;
    case ClipAnimKind::Squeeze: {
        const double t = qBound(0.0, settled, 1.0);
        const double wob = std::exp(-4.0 * t) * std::cos(2.0 * 6.28318530718 * t) * (1.0 - t);
        out->scaleX *= 1.0 + 0.45 * wob;
        out->scaleY *= 1.0 - 0.45 * wob;
        out->opacity *= qBound(0.0, t * 6.0, 1.0);
        break;
    }
    case ClipAnimKind::RollIn:
        out->dx -= sign * away * 0.6 * layoutW;
        out->rotationDeg += sign * away * -360.0;
        out->opacity *= seen;
        break;
    case ClipAnimKind::Heartbeat: {
        const double t = qBound(0.0, settled, 1.0);
        const double beat = std::sin(t * 2.0 * 6.28318530718) * (1.0 - t);
        out->scale *= (0.5 + 0.5 * a) * (1.0 + 0.18 * qMax(0.0, beat));
        out->opacity *= qBound(0.0, t * 5.0, 1.0);
        break;
    }
    case ClipAnimKind::Spin3D:
        out->yawDeg += sign * away * 360.0;
        out->scale *= 0.5 + 0.5 * a;
        out->opacity *= seen;
        break;
    case ClipAnimKind::FlipUp3D:
        out->pitchDeg += sign * away * -80.0;
        out->dy += sign * away * 0.35 * layoutH;
        out->opacity *= seen;
        break;
    case ClipAnimKind::ZoomBlurIn:
        out->scale *= 1.0 + 0.9 * away;
        out->blurPx += 0.05 * layoutH * away;
        out->opacity *= qBound(0.0, p * 3.0, 1.0);
        break;
    case ClipAnimKind::FlashIn:
        out->flash += 2.0 * away * away;
        out->opacity *= qBound(0.0, p * 5.0, 1.0);
        break;
    case ClipAnimKind::Jelly: {
        const double t = qBound(0.0, settled, 1.0);
        const double wob = 0.35 * std::exp(-3.5 * t) * std::cos(2.5 * 6.28318530718 * t) * (1.0 - t);
        out->scaleX *= 1.0 + wob;
        out->scaleY *= 1.0 - wob;
        out->scale *= 0.7 + 0.3 * qMin(1.0, t * 3.0);
        out->opacity *= qBound(0.0, t * 6.0, 1.0);
        break;
    }
    case ClipAnimKind::StretchH:
        out->scaleX *= 0.05 + 0.95 * a;
        out->opacity *= qBound(0.0, a * 3.0, 1.0);
        break;
    case ClipAnimKind::StretchV:
        out->scaleY *= 0.05 + 0.95 * a;
        out->opacity *= qBound(0.0, a * 3.0, 1.0);
        break;
    case ClipAnimKind::RiseUp: {
        const double b = QEasingCurve(QEasingCurve::OutBack).valueForProgress(qBound(0.0, settled, 1.0));
        out->dy += sign * (1.0 - b) * layoutH * 0.6;
        out->opacity *= qBound(0.0, settled * 4.0, 1.0);
        break;
    }
    case ClipAnimKind::Whirl:
        out->rotationDeg += sign * away * 720.0;
        out->scale *= 0.1 + 0.9 * a;
        out->opacity *= qBound(0.0, a * 2.0, 1.0);
        break;
    case ClipAnimKind::Elastic: {
        const double e = QEasingCurve(QEasingCurve::OutElastic).valueForProgress(qBound(0.0, settled, 1.0));
        out->scale *= qMax(0.0, e);
        out->opacity *= qBound(0.0, settled * 5.0, 1.0);
        break;
    }
    case ClipAnimKind::Diagonal:
        out->dx -= sign * away * travelX;
        out->dy += sign * away * travelY;
        out->opacity *= a;
        break;
    case ClipAnimKind::WhooshLeft:
        out->dx -= sign * away * layoutW;
        break;
    case ClipAnimKind::WhooshRight:
        out->dx += sign * away * layoutW;
        break;
    case ClipAnimKind::Blink: {
        const double t = qBound(0.0, settled, 1.0);
        // Three short flickers, then on for good.
        const double flicker = t > 0.75 ? 1.0 : (std::fmod(t * 8.0, 2.0) < 1.0 ? 0.15 : 1.0);
        out->opacity *= flicker;
        break;
    }
    }
}

QStringList clipAnimComboIds()
{
    return {QStringLiteral("rock"),        QStringLiteral("zoomInOut"),   QStringLiteral("rotateDown"),
            QStringLiteral("slideThrough"), QStringLiteral("bounceInOut"), QStringLiteral("spinZoom"),
            QStringLiteral("pendulum"),    QStringLiteral("squash"),      QStringLiteral("flipThrough"),
            QStringLiteral("floatThrough")};
}

bool isClipAnimCombo(const QString &id)
{
    return !id.isEmpty() && clipAnimComboIds().contains(id);
}

void applyClipAnimCombo(const QString &id, TimeUs rel, TimeUs duration, double layoutW, double layoutH,
                        ClipAnimSample *out)
{
    if (!out || duration <= 0 || !isClipAnimCombo(id))
        return;
    constexpr double kPi = 3.14159265358979323846;
    const double d = static_cast<double>(duration) / 1e6;
    const double r = qBound(0.0, static_cast<double>(rel) / 1e6, d);
    // Coming in over the first quarter (at most a second), going out over the last.
    const double edge = qMax(0.05, qMin(0.25 * d, 1.0));
    const auto smooth = [](double t) {
        t = qBound(0.0, t, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    const double ein = smooth(r / edge);        // 0 = away, 1 = in place
    const double eout = smooth((d - r) / edge); // 1 = in place, 0 = gone at the very end
    const double fade = qMin(ein, eout);

    if (id == QLatin1String("rock")) {
        out->rotationDeg += -15.0 * (1.0 - ein) + 15.0 * (1.0 - eout) + 5.0 * std::sin(2.0 * kPi * r / 1.2);
        out->opacity *= fade;
    } else if (id == QLatin1String("zoomInOut")) {
        out->scale *= (0.5 + 0.5 * ein) * (1.0 + 0.5 * (1.0 - eout))
                      * (1.0 + 0.06 * std::pow(std::sin(kPi * r / 2.0), 2.0));
        out->opacity *= fade;
    } else if (id == QLatin1String("rotateDown")) {
        out->rotationDeg += -90.0 * (1.0 - ein) + 90.0 * (1.0 - eout);
        out->dy += -0.4 * layoutH * (1.0 - ein) + 0.5 * layoutH * (1.0 - eout);
        out->opacity *= fade;
    } else if (id == QLatin1String("slideThrough")) {
        out->dx += -0.5 * layoutW * (1.0 - ein) + 0.5 * layoutW * (1.0 - eout);
        out->opacity *= fade;
    } else if (id == QLatin1String("bounceInOut")) {
        const double t = qBound(0.0, r / edge, 1.0);
        const double bounce = 1.0 - (1.0 - t) * std::abs(std::cos(t * 3.0 * kPi)); // lands with bumps
        out->dy += -0.4 * layoutH * (1.0 - bounce) - 0.4 * layoutH * (1.0 - eout)
                   - 0.02 * layoutH * std::abs(std::sin(kPi * r / 0.6)) * ein * eout;
        out->opacity *= qMin(smooth(t * 3.0), eout);
    } else if (id == QLatin1String("spinZoom")) {
        out->scale *= (0.2 + 0.8 * ein) * (0.2 + 0.8 * eout);
        out->rotationDeg += -180.0 * (1.0 - ein) + 180.0 * (1.0 - eout);
        out->opacity *= fade;
    } else if (id == QLatin1String("pendulum")) {
        const double s = std::sin(2.0 * kPi * r / 1.6);
        out->rotationDeg += 10.0 * s;
        out->dx += 0.02 * layoutW * s;
        out->dy -= 0.01 * layoutH * std::abs(s);
        out->opacity *= fade;
    } else if (id == QLatin1String("squash")) {
        const double s = std::sin(2.0 * kPi * r / 0.9);
        out->scaleX *= 1.0 + 0.1 * s;
        out->scaleY *= 1.0 - 0.1 * s;
        out->scale *= (0.6 + 0.4 * ein) * (0.6 + 0.4 * eout);
        out->opacity *= fade;
    } else if (id == QLatin1String("flipThrough")) {
        out->scaleX *= qMax(0.02, ein);  // turns in side to side
        out->scaleY *= qMax(0.02, eout); // turns away top to bottom
    } else if (id == QLatin1String("floatThrough")) {
        out->dy += 0.3 * layoutH * (1.0 - ein) - 0.3 * layoutH * (1.0 - eout)
                   + 0.03 * layoutH * std::sin(2.0 * kPi * r / 2.0);
        out->opacity *= fade;
    }
}

ClipAnimSample evaluateClipAnimation(TimeUs timelineStart, TimeUs timelineDuration,
                                     const ClipAnimation &animIn, const ClipAnimation &animOut,
                                     TimeUs timelineUs, double layoutW, double layoutH)
{
    ClipAnimSample sample;
    if (timelineDuration <= 0)
        return sample;

    const TimeUs rel = qBound(TimeUs{0}, timelineUs - timelineStart, timelineDuration);

    if (animIn.kind != ClipAnimKind::None && animIn.kind != ClipAnimKind::Fade
        && animIn.durationUs > 0 && rel < animIn.durationUs) {
        const double settled = static_cast<double>(rel) / static_cast<double>(animIn.durationUs);
        applyKind(animIn.kind, settled, true, layoutW, layoutH, animIn.curve, animIn.shape, &sample);
    }

    if (animOut.kind != ClipAnimKind::None && animOut.kind != ClipAnimKind::Fade
        && animOut.durationUs > 0) {
        const TimeUs fromEnd = timelineDuration - rel;
        if (fromEnd < animOut.durationUs) {
            const double settled =
                static_cast<double>(fromEnd) / static_cast<double>(animOut.durationUs);
            applyKind(animOut.kind, settled, false, layoutW, layoutH, animOut.curve, animOut.shape,
                      &sample);
        }
    }

    return sample;
}

} // namespace drift
