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
