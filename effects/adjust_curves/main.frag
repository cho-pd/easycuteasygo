#version 330 core
// VEDIT: CapCut's 조정 > 곡선 — an RGB curve over all three channels, then one each for red,
// green and blue. Five points per curve at 0, 25, 50, 75 and 100 % input (the line by default),
// joined smoothly (Catmull-Rom, the same maths the editor draws).
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float m0, m1, m2, m3, m4;
uniform float r0, r1, r2, r3, r4;
uniform float g0, g1, g2, g3, g4;
uniform float b0, b1, b2, b3, b4;

float seg(float p0, float p1, float p2, float p3, float t) {
    return 0.5 * (2.0 * p1 + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
                  + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);
}
float curve(float x, float y0, float y1, float y2, float y3, float y4) {
    x = clamp(x, 0.0, 1.0);
    float ys[5] = float[5](y0, y1, y2, y3, y4);
    float f = x * 4.0;
    int i = int(min(floor(f), 3.0));
    float t = f - float(i);
    float p0 = ys[max(i - 1, 0)], p1 = ys[i], p2 = ys[i + 1], p3 = ys[min(i + 2, 4)];
    return clamp(seg(p0, p1, p2, p3, t), 0.0, 1.0);
}
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec3 v = clamp(c.rgb, 0.0, 1.0);
    v = vec3(curve(v.r, m0, m1, m2, m3, m4), curve(v.g, m0, m1, m2, m3, m4), curve(v.b, m0, m1, m2, m3, m4));
    v = vec3(curve(v.r, r0, r1, r2, r3, r4), curve(v.g, g0, g1, g2, g3, g4), curve(v.b, b0, b1, b2, b3, b4));
    fragColor = vec4(v, c.a);
}
