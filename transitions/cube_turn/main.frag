#version 330 core
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

vec2 mirrorUV(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}
float window(float p) { return p < 0.5 ? smoothstep(0.0, 0.5, p) : 1.0 - smoothstep(0.5, 1.0, p); }

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
