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
