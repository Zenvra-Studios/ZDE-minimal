// 3D Raymarching SDF Torus & Sphere with Phong Lighting
// Compatible with ZDE Shader Sandbox

float map(vec3 p) {
    vec3 q = p;
    float cy = cos(iTime * 0.7), sy = sin(iTime * 0.7);
    q.xz = mat2(cy, -sy, sy, cy) * q.xz;
    vec2 t = vec2(length(q.xz) - 1.0, q.y);
    float torus = length(t) - 0.35;
    float sphere = length(p) - (0.45 + 0.1 * sin(iTime * 2.0));
    return min(torus, sphere);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    vec3 ro = vec3(0.0, 0.0, -3.2);
    vec3 rd = normalize(vec3(uv, 1.3));
    float d = 0.0;
    for (int i = 0; i < 64; ++i) {
        vec3 p = ro + rd * d;
        float dist = map(p);
        if (dist < 0.001) break;
        d += dist;
        if (d > 20.0) break;
    }
    vec3 col = vec3(0.05, 0.08, 0.15);
    if (d < 20.0) {
        vec3 p = ro + rd * d;
        vec2 eps = vec2(0.002, 0.0);
        vec3 n = normalize(vec3(
            map(p + eps.xyy) - map(p - eps.xyy),
            map(p + eps.yxy) - map(p - eps.yxy),
            map(p + eps.yyx) - map(p - eps.yyx)
        ));
        vec3 light = normalize(vec3(1.0, 2.0, -1.5));
        float diff = max(dot(n, light), 0.0);
        float spec = pow(max(dot(reflect(-light, n), -rd), 0.0), 16.0);
        vec3 mat = mix(vec3(0.1, 0.6, 0.9), vec3(0.9, 0.3, 0.6), 0.5 + 0.5 * sin(p.y * 3.0 + iTime));
        col = mat * (diff + 0.15) + vec3(1.0) * spec * 0.5;
    }
    fragColor = vec4(col, 1.0);
}
