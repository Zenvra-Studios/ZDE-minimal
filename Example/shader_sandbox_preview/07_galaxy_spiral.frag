// Cosmic Spiral Galaxy & Nebula
// Compatible with ZDE Shader Sandbox

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float t = iTime * 0.5;
    float r = length(p);
    float a = atan(p.y, p.x);
    float arms = sin(a * 2.0 + 4.0 * log(r + 0.01) - t * 1.5);
    float density = exp(-r * 2.0) * (0.6 + 0.4 * arms);
    float core = 0.08 / (r + 0.05);
    vec3 arm_col = vec3(0.3 + 0.3 * sin(t + r * 5.0), 0.5 + 0.3 * cos(t + r * 3.0), 0.9);
    vec3 col = arm_col * density + vec3(1.0, 0.85, 0.5) * core;
    fragColor = vec4(col, 1.0);
}
