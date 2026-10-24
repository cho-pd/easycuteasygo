#version 330 core
// VEDIT: CapCut's 조정 > 컬러 휠 — four wheels: 어두운 부분 (lift), 중간 톤 (gamma), 밝은 부분 (gain)
// and 전체 (offset). Each wheel's dot pushes that part of the picture toward a colour (its angle is
// the hue, its distance the amount); its slider makes that part brighter or darker.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float lift_x; uniform float lift_y; uniform float lift_l;
uniform float gamma_x; uniform float gamma_y; uniform float gamma_l;
uniform float gain_x; uniform float gain_y; uniform float gain_l;
uniform float offset_x; uniform float offset_y; uniform float offset_l;

// A colour push for a wheel position: the hue of the angle minus its grey, scaled by the distance.
vec3 push(float x, float y) {
    float amount = min(1.0, length(vec2(x, y)));
    if (amount < 1e-4) return vec3(0.0);
    float h = atan(y, x) / 6.2831853;
    vec3 c = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return (c - vec3(dot(c, vec3(0.3333)))) * amount;
}
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec3 v = clamp(c.rgb, 0.0, 1.0);
    vec3 lift = push(lift_x, lift_y) * 0.25 + vec3(lift_l * 0.2);
    vec3 gain = vec3(1.0) + push(gain_x, gain_y) * 0.35 + vec3(gain_l * 0.35);
    vec3 gam = vec3(1.0) - push(gamma_x, gamma_y) * 0.4 - vec3(gamma_l * 0.4);
    v = v * gain + lift * (1.0 - v);
    v = pow(clamp(v, 0.0, 1.0), max(gam, vec3(0.2)));
    v += push(offset_x, offset_y) * 0.15 + vec3(offset_l * 0.15);
    fragColor = vec4(clamp(v, 0.0, 1.0), c.a);
}
