#version 330 core
// VEDIT (조박사님: special entrances): driven per frame by a clip movement, not by hand.
//   reveal — wiped on from the left: 1 shows all, 0 nothing, with a soft 4 % edge
//   flash  — a burst of light added on top (glow entrance)
//   split  — red and blue pulled apart sideways (glitch), in texture units
//   slice  — horizontal bands shoved sideways (glitch), 0 = none; seed picks the bands
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float reveal; uniform float flash; uniform float split; uniform float slice; uniform float seed;

float hash(float n) { return fract(sin(n * 12.9898 + seed * 78.233) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord;
    if (slice > 0.0) {
        float band = floor(uv.y * 14.0);
        float h = hash(band);
        if (h > 0.55)
            uv.x += (hash(band + 7.0) - 0.5) * 0.25 * slice;
    }
    vec4 c = texture(u_currentTexture, uv);
    if (abs(split) > 1e-5) {
        vec4 r = texture(u_currentTexture, uv + vec2(split, 0.0));
        vec4 b = texture(u_currentTexture, uv - vec2(split, 0.0));
        c = vec4(r.r, c.g, b.b, max(c.a, max(r.a, b.a)));
    }
    if (flash > 0.0)
        c.rgb = min(c.rgb + c.a * flash * vec3(1.0, 0.97, 0.9), vec3(c.a));
    if (reveal < 1.0) {
        float edge = 0.04;
        float m = 1.0 - smoothstep(reveal * (1.0 + edge) - edge, reveal * (1.0 + edge), uv.x);
        c *= m;
    }
    fragColor = c;
}
