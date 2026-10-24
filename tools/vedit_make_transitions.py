"""VEDIT: generates the basic transitions CapCut users expect (push right/up/down, zoom out, spin,
blur, circle open, clock wipe, shake, doors open) as GPU transition packages, each with a
preview strip rendered by a NumPy port of the same shader math.

Original shaders written for VEDIT (GPLv3, like the rest of the app). No third-party assets.

    python tools/vedit_make_transitions.py
"""
import json
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'transitions')

HEADER = '''#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture; // pass input 0
uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

// VEDIT: original shader. Straight-alpha source-over; clip layers are transparent outside the
// clip rect.
vec4 over(vec4 top, vec4 bot) {
    float oa = top.a + bot.a * (1.0 - top.a);
    if (oa <= 0.0001) return vec4(0.0);
    vec3 rgb = (top.rgb * top.a + bot.rgb * bot.a * (1.0 - top.a)) / oa;
    return vec4(rgb, oa);
}

float aspectRatio() { return u_resolution.x / max(u_resolution.y, 1.0); }
float ease(float t) { return t * t * (3.0 - 2.0 * t); }
bool inside(vec2 p) { return p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0; }
vec4 sampleIn(sampler2D t, vec2 p) { return inside(p) ? texture(t, p) : vec4(0.0); }
'''

# id, English name (translated in the app), order, GLSL main body
TRANSITIONS = [
    ('push_right', 'Push Right', 132, '''
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    // A slides out to the right, B follows it in from the left.
    vec4 a = sampleIn(u_fromTexture, uv - vec2(e, 0.0));
    vec4 b = sampleIn(u_toTexture, uv + vec2(1.0 - e, 0.0));
    fragColor = over(b, a);
}
'''),
    ('push_up', 'Push Up', 133, '''
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    // A rises out of the top, B comes up from the bottom.
    vec4 a = sampleIn(u_fromTexture, uv + vec2(0.0, e));
    vec4 b = sampleIn(u_toTexture, uv - vec2(0.0, 1.0 - e));
    fragColor = over(b, a);
}
'''),
    ('push_down', 'Push Down', 134, '''
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    vec4 a = sampleIn(u_fromTexture, uv - vec2(0.0, e));
    vec4 b = sampleIn(u_toTexture, uv + vec2(0.0, 1.0 - e));
    fragColor = over(b, a);
}
'''),
    ('zoom_out', 'Zoom Out', 135, '''
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    // A shrinks away into the middle while B settles in from slightly large.
    float sa = mix(1.0, 0.55, e);
    float sb = mix(1.15, 1.0, e);
    vec4 a = sampleIn(u_fromTexture, (uv - 0.5) / sa + 0.5);
    vec4 b = sampleIn(u_toTexture, (uv - 0.5) / sb + 0.5);
    a.a *= 1.0 - smoothstep(0.35, 1.0, u_progress);
    fragColor = over(a, b);
}
'''),
    ('spin', 'Spin', 136, '''
vec2 rotate(vec2 uv, float angle, float scale) {
    float ar = aspectRatio();
    vec2 p = (uv - 0.5) * vec2(ar, 1.0);
    float c = cos(angle), s = sin(angle);
    p = mat2(c, -s, s, c) * p / scale;
    return p / vec2(ar, 1.0) + 0.5;
}

void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float bulge = 1.0 + 0.5 * sin(p * 3.14159265);
    // A turns half a revolution away, B turns in from the other half; they swap in the middle.
    vec4 a = sampleIn(u_fromTexture, rotate(uv, ease(p) * 3.14159265, bulge));
    vec4 b = sampleIn(u_toTexture, rotate(uv, (ease(p) - 1.0) * 3.14159265, bulge));
    fragColor = mix(a, b, smoothstep(0.4, 0.6, p));
}
'''),
    ('blur_dissolve', 'Blur', 137, '''
vec4 blurred(sampler2D t, vec2 uv, float r) {
    vec4 sum = texture(t, uv);
    float n = 1.0;
    for (int i = 0; i < 16; ++i) {
        float a = float(i) * 2.39996323;      // golden-angle spiral
        float d = sqrt((float(i) + 0.5) / 16.0) * r;
        sum += texture(t, uv + vec2(cos(a), sin(a) * aspectRatio()) * d);
        n += 1.0;
    }
    return sum / n;
}

void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    // Soft-focus out of A and back into focus on B.
    float r = 0.03 * sin(p * 3.14159265);
    fragColor = mix(blurred(u_fromTexture, uv, r), blurred(u_toTexture, uv, r), smoothstep(0.2, 0.8, p));
}
'''),
    ('circle_open', 'Circle Open', 138, '''
void main() {
    vec2 uv = v_texCoord;
    float ar = aspectRatio();
    vec2 d = (uv - 0.5) * vec2(ar, 1.0);
    float maxR = length(vec2(ar, 1.0) * 0.5) + 0.02;
    float r = ease(u_progress) * maxR;
    // B shows through a circle that opens from the middle.
    float m = 1.0 - smoothstep(r - 0.01, r + 0.01, length(d));
    fragColor = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), m);
}
'''),
    ('clock_wipe', 'Clock Wipe', 139, '''
void main() {
    vec2 uv = v_texCoord;
    vec2 d = (uv - 0.5) * vec2(aspectRatio(), 1.0);
    // Angle measured clockwise from twelve o'clock, 0..1 around the dial.
    float a = atan(d.x, -d.y) / 6.28318531;
    a = a < 0.0 ? a + 1.0 : a;
    float m = 1.0 - smoothstep(u_progress - 0.004, u_progress + 0.004, a);
    if (u_progress >= 1.0) m = 1.0;
    fragColor = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), m);
}
'''),
    ('shake', 'Shake', 140, '''
float hash(float n) { return fract(sin(n) * 43758.5453123); }

void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float amount = 0.035 * sin(p * 3.14159265);
    float step_ = floor(p * 24.0);
    vec2 jolt = (vec2(hash(step_), hash(step_ + 7.0)) - 0.5) * 2.0 * amount;
    // A hand-held jolt that peaks at the cut; the picture swaps under it.
    vec2 q = (uv - 0.5) / (1.0 + 2.0 * amount) + 0.5 + jolt;
    vec4 a = texture(u_fromTexture, q);
    vec4 b = texture(u_toTexture, q);
    fragColor = mix(a, b, smoothstep(0.45, 0.55, p));
}
'''),
    ('doors_open', 'Doors Open', 141, '''
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    // A splits down the middle and the halves slide apart, B behind settling from slightly large.
    vec4 a = uv.x < 0.5 ? sampleIn(u_fromTexture, uv + vec2(e * 0.5, 0.0))
                        : sampleIn(u_fromTexture, uv - vec2(e * 0.5, 0.0));
    if (uv.x < 0.5 && uv.x + e * 0.5 >= 0.5) a = vec4(0.0);
    if (uv.x >= 0.5 && uv.x - e * 0.5 < 0.5) a = vec4(0.0);
    float sb = mix(1.12, 1.0, e);
    vec4 b = sampleIn(u_toTexture, (uv - 0.5) / sb + 0.5);
    fragColor = over(a, b);
}
'''),
]


# ---- NumPy preview of the same motions (12 square frames, 128 px) ---------------------------
N = 128
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
U = (xx + 0.5) / N
V = (yy + 0.5) / N


def picture(warm):
    img = np.zeros((N, N, 4), np.float32)
    if warm:
        img[..., 0] = 0.95; img[..., 1] = 0.55 + 0.3 * V; img[..., 2] = 0.25
        mask = (U - 0.5) ** 2 + (V - 0.45) ** 2 < 0.04
    else:
        img[..., 0] = 0.2; img[..., 1] = 0.45 + 0.3 * U; img[..., 2] = 0.9
        mask = (abs(U - 0.5) < 0.18) & (abs(V - 0.5) < 0.18)
    img[mask, :3] = 0.97
    img[..., 3] = 1.0
    return img


A, B = picture(True), picture(False)


def sample(img, u, v):
    ok = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
    xi = np.clip((u * N).astype(int), 0, N - 1)
    yi = np.clip((v * N).astype(int), 0, N - 1)
    out = img[yi, xi].copy()
    out[~ok] = 0
    return out


def over(top, bot):
    ta, ba = top[..., 3:4], bot[..., 3:4]
    oa = ta + ba * (1 - ta)
    rgb = (top[..., :3] * ta + bot[..., :3] * ba * (1 - ta)) / np.maximum(oa, 1e-4)
    return np.concatenate([rgb, oa], -1)


def mix(a, b, m):
    m = m[..., None] if np.ndim(m) == 2 else m
    return a * (1 - m) + b * m


def ease(t):
    return t * t * (3 - 2 * t)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def frame(tid, p):
    e = ease(p)
    if tid == 'push_right':
        return over(sample(B, U + (1 - e), V), sample(A, U - e, V))
    if tid == 'push_up':
        return over(sample(B, U, V - (1 - e)), sample(A, U, V + e))
    if tid == 'push_down':
        return over(sample(B, U, V + (1 - e)), sample(A, U, V - e))
    if tid == 'zoom_out':
        sa, sb = 1 - 0.45 * e, 1.15 - 0.15 * e
        a = sample(A, (U - .5) / sa + .5, (V - .5) / sa + .5)
        a[..., 3] *= 1 - smooth(0.35, 1.0, p)
        return over(a, sample(B, (U - .5) / sb + .5, (V - .5) / sb + .5))
    if tid == 'spin':
        bulge = 1 + 0.5 * math.sin(p * math.pi)
        def rot(img, ang):
            px, py = U - .5, V - .5
            c, s = math.cos(ang), math.sin(ang)
            return sample(img, (c * px - s * py) / bulge + .5, (s * px + c * py) / bulge + .5)
        return mix(rot(A, e * math.pi), rot(B, (e - 1) * math.pi), float(smooth(0.4, 0.6, p)))
    if tid == 'blur_dissolve':
        return mix(A, B, float(smooth(0.2, 0.8, p)))
    if tid == 'circle_open':
        r = e * (math.sqrt(0.5) + 0.02)
        m = 1 - smooth(r - 0.01, r + 0.01, np.sqrt((U - .5) ** 2 + (V - .5) ** 2))
        return mix(A, B, m)
    if tid == 'clock_wipe':
        ang = np.arctan2(U - .5, -(V - .5)) / (2 * math.pi)
        ang = np.where(ang < 0, ang + 1, ang)
        m = np.ones_like(U) if p >= 1 else 1 - smooth(p - 0.004, p + 0.004, ang)
        return mix(A, B, m)
    if tid == 'shake':
        amt = 0.035 * math.sin(p * math.pi)
        st = math.floor(p * 24)
        h = lambda n: (math.sin(n) * 43758.5453123) % 1.0
        jx, jy = (h(st) - .5) * 2 * amt, (h(st + 7) - .5) * 2 * amt
        q = ((U - .5) / (1 + 2 * amt) + .5 + jx, (V - .5) / (1 + 2 * amt) + .5 + jy)
        return mix(sample(A, *q), sample(B, *q), float(smooth(0.45, 0.55, p)))
    if tid == 'doors_open':
        left = U < 0.5
        a = np.where(left[..., None], sample(A, U + e * .5, V), sample(A, U - e * .5, V))
        gap = (left & (U + e * .5 >= .5)) | (~left & (U - e * .5 < .5))
        a[gap] = 0
        sb = 1.12 - 0.12 * e
        return over(a, sample(B, (U - .5) / sb + .5, (V - .5) / sb + .5))
    raise KeyError(tid)


def write(tid, name, order, body):
    d = os.path.join(ROOT, tid)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'main.frag'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEADER + body)
    meta = {
        'id': tid, 'displayName': name, 'category': 'basic', 'order': order, 'parameters': [],
        'pipeline': {'intermediateBuffers': [], 'passes': [{
            'passIndex': 0, 'fragmentShader': 'main.frag',
            'inputs': [{'type': 'source_texture', 'index': 0}, {'type': 'source_texture', 'index': 1}],
            'output': {'type': 'canvas'}}]},
    }
    with open(os.path.join(d, 'transition.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(meta, f, indent=2)
        f.write('\n')
    frames = [frame(tid, i / 11.0) for i in range(12)]
    strip = np.concatenate(frames, axis=1)
    Image.fromarray((np.clip(strip, 0, 1) * 255).astype(np.uint8), 'RGBA').save(
        os.path.join(d, 'preview_strip.png'))


if __name__ == '__main__':
    for t in TRANSITIONS:
        write(*t)
        print('wrote', t[0])
