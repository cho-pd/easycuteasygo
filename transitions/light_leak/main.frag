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
