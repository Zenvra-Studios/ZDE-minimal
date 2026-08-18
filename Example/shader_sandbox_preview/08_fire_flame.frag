// Procedural Volumetric Fire & Flame
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
        p = p * 2.02 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    
    // Upward flame convection
    float t = iTime * 2.8;
    vec2 p = uv * vec2(2.5, 3.5);
    
    // Multi-layer turbulent domain warping
    vec2 q = vec2(fbm(p + vec2(0.0, -t)), fbm(p + vec2(5.2, -t * 0.8)));
    vec2 r = vec2(fbm(p + q * 2.5 + vec2(1.7, -t * 1.2)), fbm(p + q * 2.0 + vec2(8.3, -t * 1.5)));
    float f = fbm(p + r * 1.8);
    
    // Tapering flame mask (wide base, rising flame tongues)
    float center_dist = abs(uv.x - 0.5) * 2.2;
    float vertical_fade = pow(clamp(1.0 - uv.y, 0.0, 1.0), 0.8);
    float width_profile = (1.0 - uv.y * 0.75);
    float mask = clamp((width_profile - center_dist * 0.9) * 2.0, 0.0, 1.0);
    
    // Intensity computation
    float intensity = clamp(f * mask * vertical_fade * 2.4, 0.0, 1.0);
    
    // Hot fiery color ramp (black -> dark red -> fiery orange -> bright yellow -> white core)
    vec3 col = vec3(0.0);
    col += vec3(1.0, 0.2, 0.02) * smoothstep(0.05, 0.45, intensity);
    col += vec3(1.0, 0.65, 0.08) * smoothstep(0.35, 0.75, intensity);
    col += vec3(1.0, 0.95, 0.75) * smoothstep(0.70, 0.98, intensity);
    
    // Glowing ambient heat around the base
    float base_glow = (1.0 - length((uv - vec2(0.5, 0.0)) * vec2(1.5, 3.0)));
    col += vec3(0.3, 0.08, 0.01) * clamp(base_glow, 0.0, 1.0) * 0.6;
    
    fragColor = vec4(col, 1.0);
}
