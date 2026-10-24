#version 330 core
// VEDIT: CapCut's 조정 > HSL — for each of eight colours (red, orange, yellow, green, cyan, blue,
// purple, magenta) its hue nudged toward a neighbour, its saturation and its lightness, blended
// smoothly between neighbouring colours so no edge appears. Greys are left alone.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float h_red; uniform float s_red; uniform float l_red;
uniform float h_orange; uniform float s_orange; uniform float l_orange;
uniform float h_yellow; uniform float s_yellow; uniform float l_yellow;
uniform float h_green; uniform float s_green; uniform float l_green;
uniform float h_cyan; uniform float s_cyan; uniform float l_cyan;
uniform float h_blue; uniform float s_blue; uniform float l_blue;
uniform float h_purple; uniform float s_purple; uniform float l_purple;
uniform float h_magenta; uniform float s_magenta; uniform float l_magenta;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
float hueDist(float a, float b) { float d = abs(a - b); return min(d, 1.0 - d); }

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec3 hsv = rgb2hsv(clamp(c.rgb, 0.0, 1.0));
    // centres in turns: red 0, orange 30, yellow 60, green 120, cyan 180, blue 225, purple 270, magenta 315
    float centres[8] = float[8](0.0, 0.0833, 0.1667, 0.3333, 0.5, 0.625, 0.75, 0.875);
    float hs[8] = float[8](h_red, h_orange, h_yellow, h_green, h_cyan, h_blue, h_purple, h_magenta);
    float ss[8] = float[8](s_red, s_orange, s_yellow, s_green, s_cyan, s_blue, s_purple, s_magenta);
    float ls[8] = float[8](l_red, l_orange, l_yellow, l_green, l_cyan, l_blue, l_purple, l_magenta);
    float dh = 0.0, ds = 0.0, dl = 0.0, wsum = 0.0;
    for (int i = 0; i < 8; ++i) {
        float w = max(0.0, 1.0 - hueDist(hsv.x, centres[i]) / 0.09);
        w = w * w * (3.0 - 2.0 * w);
        dh += hs[i] * w; ds += ss[i] * w; dl += ls[i] * w; wsum += w;
    }
    if (wsum > 1e-4) { dh /= max(wsum, 1.0); ds /= max(wsum, 1.0); dl /= max(wsum, 1.0); }
    float colourful = smoothstep(0.04, 0.25, hsv.y); // greys stay grey
    hsv.x = fract(hsv.x + dh * 0.0833 * colourful);
    hsv.y = clamp(hsv.y * (1.0 + ds * colourful), 0.0, 1.0);
    vec3 rgb = hsv2rgb(hsv);
    rgb = clamp(rgb + dl * 0.35 * colourful * hsv.y, 0.0, 1.0);
    fragColor = vec4(rgb, c.a);
}
