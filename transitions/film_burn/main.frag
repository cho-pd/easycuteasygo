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
