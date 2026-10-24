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
