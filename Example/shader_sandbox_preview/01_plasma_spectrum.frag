// Simple Psychedelic Plasma Wave Shader
// Compatible with ZDE Shader Sandbox

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float t = iTime * 0.8;
    float d = 0.0;
    vec2 p = uv;
    for (int i = 0; i < 4; ++i) {
        float fi = float(i + 1);
        p += vec2(sin(p.y * fi + t * 0.5) * 0.3, cos(p.x * fi + t * 0.5) * 0.3);
        d += abs(sin(p.x + p.y + t * 0.3)) / fi;
    }
    vec3 col = 0.5 + 0.5 * sin(t + d * 3.0 + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(col, 1.0);
}
