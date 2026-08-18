// Audio Reactive Neon Spectrum Waveform
// Compatible with ZDE Shader Sandbox

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float t = iTime;
    float wave1 = sin(uv.x * 12.0 + t * 3.0) * 0.15 + 0.5;
    float wave2 = sin(uv.x * 24.0 - t * 2.0) * 0.08 + 0.5;
    float wave3 = cos(uv.x * 6.0 + t * 4.0) * 0.2 + 0.5;
    float d1 = 0.015 / max(abs(uv.y - wave1), 0.005);
    float d2 = 0.012 / max(abs(uv.y - wave2), 0.005);
    float d3 = 0.018 / max(abs(uv.y - wave3), 0.005);
    vec3 col = vec3(0.1, 0.8, 1.0) * d1 + vec3(1.0, 0.2, 0.7) * d2 + vec3(0.3, 1.0, 0.4) * d3;
    fragColor = vec4(col, 1.0);
}
