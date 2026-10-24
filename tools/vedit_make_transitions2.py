"""VEDIT: the second set of transitions CapCut users reach for — camera moves (pull in, pull out,
axis rotate, cube turn), light (white flash, glare, light leak, film burn) and shapes (split open,
heart open, squeeze, vortex). GPU transition packages with preview strips from a NumPy port of the
same math, like tools/vedit_make_transitions.py (whose helpers this reuses).

Original shaders written for VEDIT (GPLv3, like the rest of the app). No third-party assets.

    python tools/vedit_make_transitions2.py
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vedit_make_transitions import (HEADER, ROOT, N, U, V, A, B, sample, over, mix, ease,  # noqa: E402
                                    smooth)

EXTRA = '''
vec2 mirrorUV(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}
float window(float p) { return p < 0.5 ? smoothstep(0.0, 0.5, p) : 1.0 - smoothstep(0.5, 1.0, p); }
'''

# id, English name (translated in the app), category, order, GLSL main body
TRANSITIONS = [
    ('pull_in', 'Pull In', 'camera', 150, '''
// The camera rushes into A with a strong zoom blur that peaks at the cut (brightest in the
// middle), then B settles back out of it. (Matched against CapCut's zoom transition: less zoom,
// much more blur than a plain scale.)
vec4 zoomed(sampler2D t, float z, float blur) {
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 12; ++i) {
        float k = 1.0 + blur * float(i) / 11.0;
        acc += texture(t, mirrorUV((v_texCoord - 0.5) / (z * k) + 0.5));
    }
    return acc / 12.0;
}
void main() {
    float p = u_progress;
    float za = 1.0 + 1.2 * ease(clamp(p * 2.0, 0.0, 1.0));
    float zb = 2.2 - 1.2 * ease(clamp(p * 2.0 - 1.0, 0.0, 1.0));
    float s = sin(p * 3.14159265);
    float blur = 0.38 * s * s;
    vec4 col = mix(zoomed(u_fromTexture, za, blur), zoomed(u_toTexture, zb, blur), smoothstep(0.45, 0.55, p));
    float c = 1.0 - smoothstep(0.0, 0.6, length((v_texCoord - 0.5) * vec2(aspectRatio(), 1.0)));
    col.rgb = min(col.rgb + vec3(0.35) * s * s * c, vec3(1.0));
    fragColor = col;
}
'''),
    ('pull_out', 'Pull Out', 'camera', 151, '''
// The camera pulls back from A (its edges mirror round), and B arrives the same way.
vec4 zoomed(sampler2D t, float z, float blur) {
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 8; ++i) {
        float k = 1.0 - blur * float(i) / 7.0;
        acc += texture(t, mirrorUV((v_texCoord - 0.5) / (z * k) + 0.5));
    }
    return acc / 8.0;
}
void main() {
    float p = u_progress;
    float za = mix(1.0, 0.4, ease(clamp(p * 2.0, 0.0, 1.0)));
    float zb = mix(0.4, 1.0, ease(clamp(p * 2.0 - 1.0, 0.0, 1.0)));
    float blur = 0.12 * sin(p * 3.14159265);
    fragColor = mix(zoomed(u_fromTexture, za, blur), zoomed(u_toTexture, zb, blur), smoothstep(0.42, 0.58, p));
}
'''),
    ('axis_rotate', 'Axis Rotate', 'camera', 152, '''
// A turns away about the upright axis in perspective; B turns in from the other side.
vec4 turned(sampler2D t, float ang) {
    vec2 c = v_texCoord - 0.5;
    float d = 0.9;
    float den = cos(ang) - c.x * sin(ang) * d;
    if (den <= 0.001) return vec4(0.0);
    float u = c.x / den;
    float v = c.y * (1.0 + u * sin(ang) * d);
    return sampleIn(t, vec2(u, v) + 0.5);
}
void main() {
    float p = u_progress;
    float e = ease(p);
    vec4 col = e < 0.5 ? turned(u_fromTexture, e * 3.14159265) : turned(u_toTexture, (e - 1.0) * 3.14159265);
    fragColor = over(col, vec4(0.0, 0.0, 0.0, 1.0));
}
'''),
    ('cube_turn', 'Cube Turn', 'camera', 153, '''
// A real box: A on its front, B on its right side. The camera backs off a little while the box
// turns left in perspective, so the box floats on black (matched against CapCut's cube).
vec3 rotY(vec3 v, float th) { return vec3(v.x * cos(th) - v.z * sin(th), v.y, v.x * sin(th) + v.z * cos(th)); }
void main() {
    float e = ease(u_progress);
    float a = aspectRatio();
    vec2 sp = (v_texCoord - 0.5) * vec2(a, 1.0);
    float f = 2.0;
    float back = 1.0 + 0.5 * sin(3.14159265 * e);
    vec3 C = vec3(0.0, 0.0, (f + a * 0.5) * back);
    vec3 dir = vec3(sp, f);
    float th = e * 1.5707963;
    vec3 o = rotY(-C, th);
    vec3 d = rotY(dir, th);
    d = d + vec3(1e-6);
    vec3 ext = vec3(a * 0.5, 0.5, a * 0.5);
    vec3 t1 = (-ext - o) / d, t2 = (ext - o) / d;
    vec3 tmin = min(t1, t2), tmax = max(t1, t2);
    float tn = max(max(tmin.x, tmin.y), tmin.z);
    float tf = min(min(tmax.x, tmax.y), tmax.z);
    vec4 col = vec4(0.0, 0.0, 0.0, 1.0);
    if (tn < tf && tn > 0.0) {
        vec3 L = o + d * tn;
        if (abs(L.z + ext.z) < 1e-3 * a + 1e-4) {
            vec4 t = sampleIn(u_fromTexture, vec2(L.x / a + 0.5, L.y + 0.5));
            t.rgb *= max(0.25, cos(th));
            col = over(t, col);
        } else if (abs(L.x - ext.x) < 1e-3 * a + 1e-4) {
            vec4 t = sampleIn(u_toTexture, vec2(L.z / a + 0.5, L.y + 0.5));
            t.rgb *= max(0.25, sin(th));
            col = over(t, col);
        }
    }
    fragColor = col;
}
'''),
    ('white_flash', 'White Flash', 'light', 180, '''
// Bars of white light flash across the picture three times; the picture changes under the
// middle flash (matched against CapCut's white flash).
float pulse(float p, float at) { float x = (p - at) / 0.055; return exp(-x * x); } // (pow of a negative is undefined)
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    vec4 col = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), step(0.5, p));
    float f1 = pulse(p, 0.3), f2 = pulse(p, 0.5), f3 = pulse(p, 0.68);
    float band = f1 * smoothstep(0.28, 0.0, abs(uv.y - 0.18))
               + f2 * smoothstep(0.45, 0.05, abs(uv.y - 0.6))
               + f3 * smoothstep(0.3, 0.0, abs(uv.y - 0.45));
    float w = clamp(band + 0.35 * max(max(f1, f2), f3), 0.0, 1.0);
    fragColor = vec4(mix(col.rgb, vec3(1.0), w), col.a);
}
'''),
    ('glare', 'Glare', 'light', 181, '''
// A slanted band of light sweeps across; behind it is B.
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float s = mix(-0.25, 1.25, ease(p));
    float coord = (uv.x + uv.y * 0.5) / 1.5;
    float m = smoothstep(s + 0.04, s - 0.04, coord);
    vec4 col = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), m);
    float d = coord - s;
    float glow = exp(-d * d / 0.003) * 0.9 + 0.25 * window(p);
    fragColor = vec4(min(col.rgb + vec3(1.0, 0.97, 0.9) * glow, vec3(1.0)), col.a);
}
'''),
    ('light_leak', 'Light Leak', 'light', 182, '''
// Warm light floods in from the left until the picture is nearly burnt out to white; the picture
// changes under it and the light leaves through pink (matched against CapCut's light leak).
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float a = aspectRatio();
    vec4 col = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), smoothstep(0.45, 0.55, p));
    float k = sin(p * 3.14159265);
    vec2 c = vec2(mix(-0.25, 0.55, p), mix(0.3, 0.75, p));
    vec2 dd = (uv - c) * vec2(a, 1.0);
    float g = exp(-dot(dd, dd) / 0.35);
    vec3 tint = mix(vec3(1.0, 0.82, 0.4), vec3(1.0, 0.45, 0.8), smoothstep(0.45, 0.75, p));
    col.rgb = min(col.rgb + tint * g * 1.8 * k, vec3(1.0));
    col.rgb = mix(col.rgb, vec3(1.0, 0.97, 0.88), 0.8 * pow(k, 4.0));
    fragColor = col;
}
'''),
    ('film_burn', 'Film Burn', 'light', 183, '''
// The frame burns away in glowing patches, B shows through the holes.
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float n = vnoise(uv * vec2(4.0 * aspectRatio(), 4.0)) * 0.7 + vnoise(uv * 11.0) * 0.3;
    float front = p * 1.25 - 0.1;
    vec4 a = texture(u_fromTexture, uv), b = texture(u_toTexture, uv);
    float burnt = smoothstep(front - 0.02, front - 0.12, n);  // 1 = already burnt through
    float rim = smoothstep(front + 0.06, front, n) * (1.0 - burnt);
    vec4 col = mix(a, b, burnt);
    vec3 fire = mix(vec3(1.0, 0.45, 0.1), vec3(1.0, 0.95, 0.75), rim);
    fragColor = vec4(mix(col.rgb, fire, rim), max(col.a, rim));
}
'''),
    ('split_open', 'Split Open', 'basic', 142, '''
// A splits along the middle, the top half goes up and the bottom half down; B behind settles in.
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    float sb = 1.12 - 0.12 * e;
    vec4 b = sampleIn(u_toTexture, (uv - 0.5) / sb + 0.5);
    vec4 a = uv.y < 0.5 ? sampleIn(u_fromTexture, uv + vec2(0.0, e * 0.5))
                        : sampleIn(u_fromTexture, uv - vec2(0.0, e * 0.5));
    if ((uv.y < 0.5 && uv.y + e * 0.5 >= 0.5) || (uv.y >= 0.5 && uv.y - e * 0.5 < 0.5)) a = vec4(0.0);
    fragColor = over(a, b);
}
'''),
    ('heart_open', 'Heart Open', 'basic', 143, '''
// B opens out of a growing heart in the middle.
void main() {
    vec2 uv = v_texCoord;
    float e = ease(u_progress);
    vec2 q = (uv - vec2(0.5, 0.52)) * vec2(aspectRatio(), -1.0) / max(e * 1.9, 0.0001);
    q.y += 0.25;
    float h = q.x * q.x + q.y * q.y - 1.0; // (pow() of a negative is undefined in GLSL)
    float f = h * h * h - q.x * q.x * q.y * q.y * q.y;
    float m = e >= 0.999 ? 1.0 : smoothstep(0.02, -0.02, f);
    fragColor = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), m);
}
'''),
    ('squeeze', 'Squeeze', 'basic', 144, '''
// A is squeezed thin into the middle, then B springs out of it.
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    vec4 col;
    if (p < 0.5) {
        float w = 1.0 - ease(p * 2.0);
        col = sampleIn(u_fromTexture, vec2((uv.x - 0.5) / max(w, 0.0001) + 0.5, uv.y));
    } else {
        float w = ease(p * 2.0 - 1.0);
        col = sampleIn(u_toTexture, vec2((uv.x - 0.5) / max(w, 0.0001) + 0.5, uv.y));
    }
    fragColor = over(col, vec4(0.0, 0.0, 0.0, 1.0));
}
'''),
    ('vortex', 'Vortex', 'basic', 145, '''
// A is twisted into a whirl and B unwinds out of it.
vec4 swirl(sampler2D t, float amount) {
    vec2 c = (v_texCoord - 0.5) * vec2(aspectRatio(), 1.0);
    float r = length(c);
    float ang = amount * pow(max(0.0, 1.0 - r / 0.9), 2.0);
    float s = sin(ang), co = cos(ang);
    vec2 q = vec2(co * c.x - s * c.y, s * c.x + co * c.y) / vec2(aspectRatio(), 1.0) + 0.5;
    return texture(t, mirrorUV(q));
}
void main() {
    float p = u_progress;
    float k = 9.0 * sin(p * 3.14159265);
    fragColor = mix(swirl(u_fromTexture, k), swirl(u_toTexture, -k), smoothstep(0.4, 0.6, p));
}
'''),
    ('paint_swirl', 'Paint Swirl', 'basic', 146, '''
// Pink paint swirls in from the left in curling bands until it covers A, then swirls away the
// same way to show B (after CapCut's pink swirl).
float field(vec2 uv) {
    vec2 q = (uv - vec2(-0.15, 0.5)) * vec2(aspectRatio(), 1.0);
    float r = length(q) / (aspectRatio() + 0.3);
    float ang = atan(q.y, q.x);
    return r + 0.09 * sin(ang * 3.0 + r * 14.0);
}
void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    float F = field(uv);
    vec3 paint = vec3(1.0, 0.78, 0.85);
    vec4 col;
    if (p < 0.5) {
        float cover = ease(p * 2.0) * 1.25;
        float m = smoothstep(cover, cover - 0.02, F);
        col = vec4(mix(texture(u_fromTexture, uv).rgb, paint, m), 1.0);
    } else {
        float open = ease(p * 2.0 - 1.0) * 1.25;
        float m = smoothstep(open - 0.02, open, F);
        col = vec4(mix(texture(u_toTexture, uv).rgb, paint, m), 1.0);
    }
    fragColor = col;
}
'''),
    ('tv_off', 'TV Off and On', 'light', 184, '''
// Like an old television: A squeezes into a bright line and a dot and goes dark; B comes on the
// same way (after CapCut's film change).
vec4 tube(sampler2D t, float k) {
    // k: 0 = full picture, 1 = gone
    float sy = mix(1.0, 0.006, smoothstep(0.0, 0.65, k));
    float sx = mix(1.0, 0.0, smoothstep(0.6, 1.0, k));
    vec2 q = (v_texCoord - 0.5) / vec2(max(sx, 0.0001), sy) + 0.5;
    vec4 c = sampleIn(t, q);
    c.rgb = mix(c.rgb, vec3(1.0), smoothstep(0.35, 0.65, k));
    return over(c, vec4(0.0, 0.0, 0.0, 1.0));
}
void main() {
    float p = u_progress;
    fragColor = p < 0.5 ? tube(u_fromTexture, ease(p * 2.0)) : tube(u_toTexture, 1.0 - ease(p * 2.0 - 1.0));
}
'''),
]


# ---- NumPy previews ---------------------------------------------------------------------------
def mirror(x):
    return 1 - np.abs(1 - np.mod(x, 2))


def msample(img, u, v):
    return sample(img, mirror(u), mirror(v))


def window(p):
    return smooth(0, 0.5, p) if p < 0.5 else 1 - smooth(0.5, 1, p)


def hash21(x, y):
    return np.mod(np.sin(x * 127.1 + y * 311.7) * 43758.5453123, 1.0)


def vnoise(x, y):
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash21(ix, iy), hash21(ix + 1, iy)
    c, d = hash21(ix, iy + 1), hash21(ix + 1, iy + 1)
    return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy


BLACK = np.zeros((N, N, 4), np.float32)
BLACK[..., 3] = 1


def zoomed(img, z, blur, outward):
    acc = np.zeros((N, N, 4), np.float32)
    for i in range(8):
        k = 1 + blur * i / 7 if outward else 1 - blur * i / 7
        acc += msample(img, (U - .5) / (z * k) + .5, (V - .5) / (z * k) + .5)
    return acc / 8


def turned(img, ang):
    cx, cy = U - .5, V - .5
    den = math.cos(ang) - cx * math.sin(ang) * 0.9
    ok = den > 0.001
    u = np.where(ok, cx / np.where(ok, den, 1), 9)
    v = cy * (1 + u * math.sin(ang) * 0.9)
    return sample(img, u + .5, v + .5)


def swirl(img, amount):
    cx, cy = (U - .5), (V - .5)
    r = np.sqrt(cx * cx + cy * cy)
    ang = amount * np.maximum(0, 1 - r / 0.9) ** 2
    s, c = np.sin(ang), np.cos(ang)
    return msample(img, c * cx - s * cy + .5, s * cx + c * cy + .5)


def frame(tid, p):
    e = ease(p)
    if tid == 'pull_in':
        za = 1 + 1.2 * ease(min(1, p * 2)); zb = 2.2 - 1.2 * ease(max(0, p * 2 - 1))
        s_ = math.sin(p * math.pi)
        blur = 0.38 * s_ * s_
        def z12(img, z):
            acc = np.zeros((N, N, 4), np.float32)
            for i in range(12):
                k = 1 + blur * i / 11
                acc += msample(img, (U - .5) / (z * k) + .5, (V - .5) / (z * k) + .5)
            return acc / 12
        col = mix(z12(A, za), z12(B, zb), float(smooth(0.45, 0.55, p))).copy()
        c = 1 - smooth(0, 0.6, np.sqrt((U - .5) ** 2 + (V - .5) ** 2))
        col[..., :3] = np.minimum(col[..., :3] + 0.35 * s_ * s_ * c[..., None], 1)
        return col
    if tid == 'cube_turn':
        a = 1.0; f = 2.0
        back = 1 + 0.5 * math.sin(math.pi * e)
        Cz = (f + a * .5) * back
        th = e * math.pi / 2
        ct, st = math.cos(th), math.sin(th)
        rot = lambda x, y, z: (x * ct - z * st, y, x * st + z * ct)
        ox, oy, oz = rot(0.0, 0.0, -Cz)
        dx, dy, dz = rot(U - .5, V - .5, np.full_like(U, f))
        dx = dx + 1e-6; dy = dy + 1e-6; dz = dz + 1e-6
        ext = (a * .5, .5, a * .5)
        t1 = [(-ext[0] - ox) / dx, (-ext[1] - oy) / dy, (-ext[2] - oz) / dz]
        t2 = [(ext[0] - ox) / dx, (ext[1] - oy) / dy, (ext[2] - oz) / dz]
        tn = np.maximum.reduce([np.minimum(t1[i], t2[i]) for i in range(3)])
        tf = np.minimum.reduce([np.maximum(t1[i], t2[i]) for i in range(3)])
        hit = (tn < tf) & (tn > 0)
        Lx, Ly, Lz = ox + dx * tn, oy + dy * tn, oz + dz * tn
        front = hit & (np.abs(Lz + ext[2]) < 2e-3)
        side = hit & ~front & (np.abs(Lx - ext[0]) < 2e-3)
        out = BLACK.copy()
        fa = sample(A, Lx / a + .5, Ly + .5); fa[..., :3] *= max(0.25, ct)
        fb = sample(B, Lz / a + .5, Ly + .5); fb[..., :3] *= max(0.25, st)
        out = np.where(front[..., None], over(fa, BLACK), out)
        out = np.where(side[..., None], over(fb, BLACK), out)
        return out
    if tid == 'white_flash':
        col = (A if p < 0.5 else B).copy()
        pulse = lambda at: math.exp(-((p - at) / 0.055) ** 2)
        f1, f2, f3 = pulse(0.3), pulse(0.5), pulse(0.68)
        band = (f1 * (1 - smooth(0, 0.28, np.abs(V - 0.18))) + f2 * (1 - smooth(0.05, 0.45, np.abs(V - 0.6)))
                + f3 * (1 - smooth(0, 0.3, np.abs(V - 0.45))))
        w = np.clip(band + 0.35 * max(f1, f2, f3), 0, 1)
        col[..., :3] = col[..., :3] * (1 - w[..., None]) + w[..., None]
        return col
    if tid == 'light_leak':
        col = mix(A, B, float(smooth(0.45, 0.55, p))).copy()
        k = math.sin(p * math.pi)
        c = (-0.25 + 0.8 * p, 0.3 + 0.45 * p)
        g = np.exp(-((U - c[0]) ** 2 + (V - c[1]) ** 2) / 0.35)
        t = float(smooth(0.45, 0.75, p))
        tint = np.array([1, .82, .4]) * (1 - t) + np.array([1, .45, .8]) * t
        col[..., :3] = np.minimum(col[..., :3] + g[..., None] * tint * 1.8 * k, 1)
        w = 0.8 * k ** 4
        col[..., :3] = col[..., :3] * (1 - w) + np.array([1, .97, .88]) * w
        return col
    if tid == 'paint_swirl':
        qx, qy = U + 0.15, V - 0.5
        r = np.sqrt(qx * qx + qy * qy) / 1.3
        F = r + 0.09 * np.sin(np.arctan2(qy, qx) * 3 + r * 14)
        paint = np.array([1, .78, .85])
        if p < 0.5:
            cover = ease(p * 2) * 1.25
            m = 1 - smooth(cover - 0.02, cover, F)
            base = A
        else:
            op = ease(p * 2 - 1) * 1.25
            m = smooth(op - 0.02, op, F)
            base = B
        col = base.copy()
        col[..., :3] = col[..., :3] * (1 - m[..., None]) + paint * m[..., None]
        return col
    if tid == 'tv_off':
        def tube(img, k):
            sy = 1 - (1 - 0.006) * float(smooth(0, 0.65, k))
            sx = 1 - float(smooth(0.6, 1.0, k))
            c = sample(img, (U - .5) / max(sx, 1e-4) + .5, (V - .5) / sy + .5)
            w = float(smooth(0.35, 0.65, k))
            c[..., :3] = c[..., :3] * (1 - w) + w
            return over(c, BLACK)
        return tube(A, ease(p * 2)) if p < 0.5 else tube(B, 1 - ease(p * 2 - 1))
    return frame_old(tid, p)


def frame_old(tid, p):
    e = ease(p)
    if tid in ('pull_in', 'pull_out'):
        if tid == 'pull_in':
            za = 1 + 2 * ease(min(1, p * 2)); zb = 3 - 2 * ease(max(0, p * 2 - 1))
        else:
            za = 1 - 0.6 * ease(min(1, p * 2)); zb = 0.4 + 0.6 * ease(max(0, p * 2 - 1))
        blur = 0.12 * math.sin(p * math.pi)
        out = tid == 'pull_in'
        return mix(zoomed(A, za, blur, out), zoomed(B, zb, blur, out), float(smooth(0.42, 0.58, p)))
    if tid == 'axis_rotate':
        col = turned(A, e * math.pi) if e < 0.5 else turned(B, (e - 1) * math.pi)
        return over(col, BLACK)
    if tid == 'cube_turn':
        edge = 1 - e
        left = U < edge
        t = np.where(left, U / max(edge, 1e-4), (U - edge) / max(e, 1e-4))
        ha = 1 - 0.18 * e * (1 - t); hb = 1 - 0.18 * (1 - e) * t
        a = sample(A, t, (V - .5) / ha + .5); a[..., :3] *= 1 - 0.55 * e
        b = sample(B, t, (V - .5) / hb + .5); b[..., :3] *= 0.45 + 0.55 * e
        return over(np.where(left[..., None], a, b), BLACK)
    if tid == 'white_flash':
        col = mix(A, B, float(smooth(0.45, 0.55, p)))
        w = window(p) ** 1.5
        col = col.copy(); col[..., :3] = col[..., :3] * (1 - w) + w
        return col
    if tid == 'glare':
        s = -0.25 + 1.5 * e
        coord = (U + V * 0.5) / 1.5
        m = 1 - smooth(s - 0.04, s + 0.04, coord)
        col = mix(A, B, m).copy()
        d = coord - s
        glow = np.exp(-d * d / 0.003) * 0.9 + 0.25 * window(p)
        col[..., :3] = np.minimum(col[..., :3] + glow[..., None] * np.array([1, .97, .9]), 1)
        return col
    if tid == 'light_leak':
        col = mix(A, B, float(smooth(0.3, 0.7, p))).copy()
        c1 = (-0.2 + 1.1 * p, 0.1 + 0.3 * p); c2 = (1.1 - 0.8 * p, 0.9 - 0.3 * p)
        g1 = np.exp(-((U - c1[0]) ** 2 + (V - c1[1]) ** 2) / 0.18)
        g2 = np.exp(-((U - c2[0]) ** 2 + (V - c2[1]) ** 2) / 0.12)
        k = math.sin(p * math.pi) ** 1.3 * 1.3
        leak = g1[..., None] * np.array([1, .55, .18]) + g2[..., None] * np.array([1, .3, .25])
        col[..., :3] = np.minimum(col[..., :3] + leak * k, 1)
        return col
    if tid == 'film_burn':
        n = vnoise(U * 4, V * 4) * 0.7 + vnoise(U * 11, V * 11) * 0.3
        front = p * 1.25 - 0.1
        burnt = 1 - smooth(front - 0.12, front - 0.02, n)
        rim = (1 - smooth(front, front + 0.06, n)) * (1 - burnt)
        col = mix(A, B, burnt).copy()
        fire = np.array([1, .45, .1]) * (1 - rim[..., None]) + np.array([1, .95, .75]) * rim[..., None]
        col[..., :3] = col[..., :3] * (1 - rim[..., None]) + fire * rim[..., None]
        return col
    if tid == 'split_open':
        top = V < 0.5
        a = np.where(top[..., None], sample(A, U, V + e * .5), sample(A, U, V - e * .5))
        gap = (top & (V + e * .5 >= .5)) | (~top & (V - e * .5 < .5))
        a[gap] = 0
        sb = 1.12 - 0.12 * e
        return over(a, sample(B, (U - .5) / sb + .5, (V - .5) / sb + .5))
    if tid == 'heart_open':
        if e >= 0.999:
            return B.copy()
        qx = (U - .5) / max(e * 1.9, 1e-4)
        qy = -(V - .52) / max(e * 1.9, 1e-4) + 0.25
        f = (qx * qx + qy * qy - 1) ** 3 - qx * qx * qy ** 3
        return mix(A, B, 1 - smooth(-0.02, 0.02, f))
    if tid == 'squeeze':
        if p < 0.5:
            w = 1 - ease(p * 2)
            col = sample(A, (U - .5) / max(w, 1e-4) + .5, V)
        else:
            w = ease(p * 2 - 1)
            col = sample(B, (U - .5) / max(w, 1e-4) + .5, V)
        return over(col, BLACK)
    if tid == 'vortex':
        k = 9 * math.sin(p * math.pi)
        return mix(swirl(A, k), swirl(B, -k), float(smooth(0.4, 0.6, p)))
    raise KeyError(tid)


def write(tid, name, category, order, body):
    d = os.path.join(ROOT, tid)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'main.frag'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEADER + EXTRA + body)
    meta = {
        'id': tid, 'displayName': name, 'category': category, 'order': order, 'parameters': [],
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
