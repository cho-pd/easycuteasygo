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
