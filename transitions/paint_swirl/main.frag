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
