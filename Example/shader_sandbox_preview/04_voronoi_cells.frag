// Dynamic Animated Voronoi Cell Diagram
// Compatible with ZDE Shader Sandbox

vec2 hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy * 6.0;
    float t = iTime * 0.5;
    vec2 i_st = floor(uv), f_st = fract(uv);
    float m_dist = 1.0;
    vec2 m_point = vec2(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = hash2(i_st + neighbor);
            vec2 anim = 0.5 + 0.5 * vec2(sin(t + 6.2831 * point.x), cos(t + 6.2831 * point.y));
            vec2 diff = neighbor + anim - f_st;
            float dist = length(diff);
            if (dist < m_dist) {
                m_dist = dist;
                m_point = anim;
            }
        }
    }
    vec3 col = vec3(0.2 + 0.8 * m_point.x, 0.4 + 0.6 * m_point.y, 0.6 + 0.4 * (1.0 - m_dist));
    fragColor = vec4(col * smoothstep(0.02, 0.06, m_dist), 1.0);
}
