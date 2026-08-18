// Domain Warping Fractal Brownian Motion (FBM noise)
// Compatible with ZDE Shader Sandbox

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.0 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float t = iTime * 0.4;
    vec2 q = vec2(fbm(uv + vec2(t * 0.2, t * 0.1)), fbm(uv + vec2(1.0, 1.0) - vec2(t * 0.1, t * 0.2)));
    vec2 r = vec2(fbm(uv + q * 2.0 + vec2(1.7, 9.2) + vec2(t * 0.15, 0.0)), fbm(uv + q * 2.0 + vec2(8.3, 2.8) + vec2(0.0, t * 0.15)));
    float f = fbm(uv + r * 2.5);
    vec3 col = mix(vec3(0.1, 0.3, 0.6), vec3(0.6, 0.2, 0.8), clamp(f * f * 4.0, 0.0, 1.0));
    col = mix(col, vec3(0.9, 0.7, 0.3), clamp(length(q), 0.0, 1.0));
    fragColor = vec4(col * (f * 1.4 + 0.2), 1.0);
}
