#version 330 core
// VEDIT: CapCut-style 조정 — 노출 (stops), 밝은 부분 / 어두운 부분, 화이트 / 블랙 (the very top and
// bottom of the range), 휘도 (midtones up or down, the ends held), 색조 (green ↔ magenta tint),
// 선명도 (local contrast), 입자 (film grain that moves), 페이드 (washed-out blacks) and 가장자리
// 어둡게 (vignette) — all neutral at 0.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform vec2 u_resolution; uniform float u_timeUs;
uniform float exposure; uniform float highlights; uniform float shadows; uniform float vignette;
uniform float whites; uniform float blacks; uniform float brilliance; uniform float tint;
uniform float clarity; uniform float grain; uniform float fade;
uniform float balance_r; uniform float balance_g; uniform float balance_b; // VEDIT: 색 맞추기

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec3 src = c.rgb;
    // 선명도: what a soft blur takes away, added back weighted to the midtones
    vec3 detail = vec3(0.0);
    if (abs(clarity) > 0.001) {
        vec2 px = 6.0 / max(u_resolution, vec2(1.0));
        vec3 blur = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            float a = float(i) * 0.785398;
            blur += texture(u_currentTexture, v_texCoord + vec2(cos(a), sin(a)) * px).rgb;
        }
        blur /= 8.0;
        detail = src - blur;
    }
    vec3 rgb = src * exp2(exposure);
    float luma = dot(clamp(rgb, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722));
    float hi = smoothstep(0.45, 1.0, luma);
    float lo = 1.0 - smoothstep(0.0, 0.55, luma);
    rgb += highlights * 0.35 * hi;
    rgb += shadows * 0.35 * lo;
    rgb += whites * 0.3 * smoothstep(0.55, 1.0, luma);
    rgb += blacks * 0.3 * (1.0 - smoothstep(0.0, 0.45, luma));
    rgb = pow(clamp(rgb, 0.0, 1.0), vec3(1.0 - brilliance * 0.35));
    rgb += vec3(tint * 0.07, -tint * 0.07, tint * 0.07);
    rgb += vec3(balance_r, balance_g, balance_b);
    float mid = 1.0 - abs(luma * 2.0 - 1.0);
    rgb += detail * clarity * 1.6 * (0.35 + 0.65 * mid);
    rgb = rgb * (1.0 - fade * 0.32) + fade * 0.16;
    if (grain > 0.001) {
        float frame = floor(u_timeUs / 41666.0);
        float n = hash(floor(v_texCoord * u_resolution) + vec2(frame * 17.0, frame * 31.0)) - 0.5;
        rgb += grain * 0.22 * n;
    }
    vec2 p = v_texCoord * 2.0 - 1.0;
    rgb *= 1.0 - vignette * 0.85 * smoothstep(0.25, 1.6, dot(p, p));
    fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);
}
