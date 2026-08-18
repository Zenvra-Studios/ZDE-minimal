// Infinite Cyber Tunnel Vortex
// Compatible with ZDE Shader Sandbox

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float t = iTime * 1.2;
    float r = length(p);
    float a = atan(p.y, p.x);
    vec2 uv = vec2(1.0 / r + t, a * 3.0 / 3.14159265);
    float grid = max(abs(fract(uv.x) - 0.5), abs(fract(uv.y) - 0.5));
    float wire = smoothstep(0.40, 0.48, grid);
    vec3 col = mix(vec3(0.05, 0.2, 0.5), vec3(0.9, 0.1, 0.6), 0.5 + 0.5 * sin(uv.x * 2.0 + t));
    fragColor = vec4((col * 0.4 + vec3(0.2, 0.9, 1.0) * wire) * min(r * 2.2, 1.0), 1.0);
}
