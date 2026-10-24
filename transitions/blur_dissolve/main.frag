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

vec4 blurred(sampler2D t, vec2 uv, float r) {
    vec4 sum = texture(t, uv);
    float n = 1.0;
    for (int i = 0; i < 16; ++i) {
        float a = float(i) * 2.39996323;      // golden-angle spiral
        float d = sqrt((float(i) + 0.5) / 16.0) * r;
        sum += texture(t, uv + vec2(cos(a), sin(a) * aspectRatio()) * d);
        n += 1.0;
    }
    return sum / n;
}

void main() {
    vec2 uv = v_texCoord;
    float p = u_progress;
    // Soft-focus out of A and back into focus on B.
    float r = 0.03 * sin(p * 3.14159265);
    fragColor = mix(blurred(u_fromTexture, uv, r), blurred(u_toTexture, uv, r), smoothstep(0.2, 0.8, p));
}
