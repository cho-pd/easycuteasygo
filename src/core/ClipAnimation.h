#pragma once

#include "FadeShape.h"
#include "Time.h"

#include <QJsonObject>
#include <QString>

namespace drift {

// CapCut-style body intro/outro for a whole clip (video/image/shape/text).
// Fade is one kind among slide/zoom/pop — same duration + style (curve) as the rest.
enum class ClipAnimKind {
    None,
    Fade,
    SlideUp,
    SlideDown,
    SlideLeft,
    SlideRight,
    ZoomIn,
    ZoomOut,
    Pop,
    SpinCW,
    SpinCCW,
    Bounce,
    // VEDIT: CapCut-style extras.
    Swing,    // rocks in on a pivot and settles
    Shake,    // shudders side to side, dying away
    Drop,     // falls from above and lands with a bump
    ZoomSpin, // grows from small while turning half a turn
    FlyBig,   // comes in from large (as if close to the lens)
    FlipH, // turns over side to side, like a card
    FlipV, // turns over top to bottom
    Jelly, // squashes and stretches, wobbling to rest
    StretchH, // opens out sideways from a thin line
    StretchV, // opens out from a thin line top to bottom
    RiseUp, // shoots up from below, a little past, and settles
    Whirl, // two full turns while growing
    Elastic, // pops with a springy overshoot
    Diagonal, // slides in from the lower left corner
    WhooshLeft, // flies the whole width in from the left
    WhooshRight, // flies the whole width in from the right
    Blink, // flickers on like a sign
    // VEDIT (조박사님: movements too plain) — real 3D turns, drawn in perspective.
    Turn3D, // swings in from the side like a door, turning on its upright axis
    Tilt3D, // falls forward into place from leaning back, turning on its level axis
    // VEDIT: special entrances — drawn by passes on the picture, not by moving it.
    GlowIn,   // bursts in with a flash of light that dies away
    GlitchIn, // jumps in broken: colours pulled apart, bands shoved, flickering
    WipeIn,   // wiped on from the left with a soft edge
    BlurIn,   // comes into focus from a blur, settling from a little large
    // VEDIT overhaul stage 7: more kinds from the new tools (3D, blur, flash).
    FadeRise, // rises a little while fading in
    Squeeze, // squashed wide and flat, springs to shape
    RollIn, // rolls in from the left like a wheel
    Heartbeat, // two quick beats before it settles
    Spin3D, // a full turn on its upright axis while growing
    FlipUp3D, // rises from below, turning on its level axis
    ZoomBlurIn, // rushes in from large and blurred
    FlashIn // a white flash that fades to the picture
};

// Legacy ease names kept for project JSON compatibility. New UI uses FadeCurve.
enum class ClipAnimEase { Linear, EaseOut, EaseInOut, Back };

struct ClipAnimation
{
    ClipAnimKind kind = ClipAnimKind::None;
    TimeUs durationUs = 500000; // 0.5s
    ClipAnimEase ease = ClipAnimEase::EaseOut; // legacy
    FadeCurve curve = FadeCurve::Smooth;
    FadeShape shape; // when curve == Custom
};

struct ClipAnimSample
{
    double opacity = 1.0;
    double dx = 0.0;
    double dy = 0.0;
    double scale = 1.0;
    double rotationDeg = 0.0;
    // VEDIT: separate width/height factors on top of `scale` (flips, stretches, jelly).
    double scaleX = 1.0;
    double scaleY = 1.0;
    // VEDIT: 3D turns, degrees — about the upright (yaw) and level (pitch) axes through the
    // picture's centre, drawn with perspective by the compositor.
    double yawDeg = 0.0;
    double pitchDeg = 0.0;
    // VEDIT: special entrances (see vedit.internal.reveal): blur in px of the layout height,
    // a light flash (0..), a colour split and band shove (glitch), and how much is wiped on (1 = all).
    double blurPx = 0.0;
    double flash = 0.0;
    double split = 0.0;
    double slice = 0.0;
    double reveal = 1.0;
};

QString clipAnimKindToString(ClipAnimKind kind);
ClipAnimKind clipAnimKindFromString(const QString &kind);

QString clipAnimEaseToString(ClipAnimEase ease);
ClipAnimEase clipAnimEaseFromString(const QString &ease);

FadeCurve clipAnimEaseToCurve(ClipAnimEase ease);
ClipAnimEase clipAnimCurveToEase(FadeCurve curve);

QJsonObject clipAnimationToJson(const ClipAnimation &anim);
ClipAnimation clipAnimationFromJson(const QJsonObject &object);

// Settled sample for the clip at `timelineUs`. `layoutW`/`layoutH` are canvas pixels
// (already render-scaled) so slide travel matches what the user sees.
// Fade kind is a no-op here — opacity comes from Clip::fadeMultiplier.
ClipAnimSample evaluateClipAnimation(TimeUs timelineStart, TimeUs timelineDuration,
                                     const ClipAnimation &animIn, const ClipAnimation &animOut,
                                     TimeUs timelineUs, double layoutW, double layoutH);

// VEDIT: CapCut's 콤보 — one movement over the whole clip (it comes in, keeps moving, goes out),
// used instead of 들어오기/나가기. Ids: rock, zoomInOut, rotateDown, slideThrough, bounceInOut,
// spinZoom, pendulum, squash, flipThrough, floatThrough. Unknown or empty ids do nothing.
QStringList clipAnimComboIds();
bool isClipAnimCombo(const QString &id);
void applyClipAnimCombo(const QString &id, TimeUs rel, TimeUs duration, double layoutW, double layoutH,
                        ClipAnimSample *out);

} // namespace drift
