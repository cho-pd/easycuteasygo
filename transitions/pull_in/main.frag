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
