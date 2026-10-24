#version 330 core
// VEDIT (조박사님: clip movements too plain): the smear a camera shutter leaves on something
// moving — averaged over where the picture was across the shutter: slid by (vx, vy) (texture
// units), grown by exp(zoomX / zoomY) on each axis, turned by spin (radians). Outside the picture is transparent, so its
// edges soften the way a fast-moving frame's do. Set per frame by the clip animation, not by hand.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float vx; uniform float vy; uniform float zoomX; uniform float zoomY; uniform float spin;

void main() {
    float amount = abs(vx) + abs(vy) + (abs(zoomX) + abs(zoomY)) * 0.5 + abs(spin) * 0.5;
    if (amount < 1e-4) {
        fragColor = texture(u_currentTexture, v_texCoord);
        return;
    }
    const int N = 40; // enough taps that a long streak has no steps
    vec2 c = vec2(0.5);
    vec4 acc = vec4(0.0);
    for (int i = 0; i < N; ++i) {
        float s = float(i) / float(N - 1) - 0.5; // across the shutter
        vec2 sc = vec2(exp(-zoomX * s), exp(-zoomY * s)); // each axis grows on its own (flips, bands)
        float a = -spin * s;
        vec2 d = (v_texCoord - c) * sc;
        vec2 q = vec2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a)) + c
                 + vec2(vx, vy) * s;
        if (q.x >= 0.0 && q.x <= 1.0 && q.y >= 0.0 && q.y <= 1.0)
            acc += texture(u_currentTexture, q);
    }
    fragColor = acc / float(N);
}
