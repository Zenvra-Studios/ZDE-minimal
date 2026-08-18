// =========================================================================================================
//  REAL-TIME PHYSICAL VOLUMETRIC CLOUDS & ATMOSPHERIC SCATTERING (HIGH-PERFORMANCE 60 FPS OPTIMIZED)
//  Repository Reference: https://github.com/kotivas/skygl
//
//  COMPREHENSIVE OPTIMIZATION BREAKDOWN:
//  ---------------------------------------------------------------------------------------------------------
//  1. Fast 8-point Lattice Cellular Noise (Menggantikan 27-loop nested Worley yang lambat).
//  2. Early Exit & Dynamic Step Distance (Empty Space Skipping).
//  3. Lightweight Secondary Shadow Marching (Hanya 3 step cepat untuk transmisi cahaya matahari).
//  4. Dual-lobe Henyey-Greenstein Silver Lining + Powder Sugar Effect tetap aktif dengan visual 3D maksimal.
// =========================================================================================================

#define PI               3.14159265358979323846
#define TWO_PI           6.28318530717958647692
#define INV_4_PI         0.07957747154594766788

// Planetary Geometry Constants (Metric units)
const float EARTH_BOTTOM_RADIUS    = 6360000.0; // Ground surface radius: 6,360 km
const float CLOUD_LAYER_BOTTOM     = 1500.0;    // Cloud layer bottom: 1,500 m
const float CLOUD_LAYER_TOP        = 4800.0;    // Cloud layer top: 4,800 m
const float CLOUD_LAYER_THICKNESS  = 3300.0;    // Total vertical thickness: 3,300 m
const float HIGH_CLOUDS_ALTITUDE   = 9500.0;    // Cirrus altitude: 9,500 m

// Raymarching Steps (Optimized for rock-solid 60 FPS)
#define PRIMARY_RAY_STEPS          36
#define LIGHT_RAY_STEPS            3
#define MAX_MARCH_DISTANCE         36000.0

// Optical Coefficients
const float CLOUD_SIGMA_EXTINCTION = 0.0034;
const float CLOUD_SIGMA_SCATTERING = 0.0032;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }

float remapClamped(float value, float min1, float max1, float min2, float max2) {
    return clamp(min2 + (value - min1) * (max2 - min2) / (max1 - min1), min2, max2);
}

// ---------------------------------------------------------------------------------------------------------
// Fast Analytical 3D Noise Functions (High-Framerate GPU Optimized)
// ---------------------------------------------------------------------------------------------------------
float hash31(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise3D(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    
    return mix(
        mix(mix(hash31(p + vec3(0,0,0)), hash31(p + vec3(1,0,0)), f.x),
            mix(hash31(p + vec3(0,1,0)), hash31(p + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash31(p + vec3(0,0,1)), hash31(p + vec3(1,0,1)), f.x),
            mix(hash31(p + vec3(0,1,1)), hash31(p + vec3(1,1,1)), f.x), f.y), f.z);
}

// Fast 8-cell Cellular Billow Noise for Cauliflower Cloud Tops
float fastWorley3D(vec3 p) {
    vec3 id = floor(p);
    vec3 fd = fract(p);
    float d = 1.0;
    for (int z = 0; z <= 1; ++z) {
        for (int y = 0; y <= 1; ++y) {
            for (int x = 0; x <= 1; ++x) {
                vec3 cell = vec3(float(x), float(y), float(z));
                float h = hash31(id + cell);
                vec3 pt = cell + vec3(h, fract(h * 13.3), fract(h * 27.7)) * 0.7;
                d = min(d, length(pt - fd));
            }
        }
    }
    return d;
}

float fbmNoise3D(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * noise3D(p);
        p = p * 2.08 + vec3(1.7, 9.2, 3.4);
        a *= 0.5;
    }
    return v;
}

// ---------------------------------------------------------------------------------------------------------
// Weather System & 3D Cloud Morphology
// ---------------------------------------------------------------------------------------------------------
float HeightGradient(float h) {
    float h_c = saturate(h);
    // Flat sharp condensation base + cauliflower top dome
    float base_shape = smoothstep(0.0, 0.09, h_c);
    float top_shape  = smoothstep(0.90, 0.28, h_c);
    return base_shape * top_shape;
}

float SampleCloudDensity(vec3 pos, vec3 earth_center, float time, bool is_shadow) {
    float altitude = length(pos - earth_center) - EARTH_BOTTOM_RADIUS;
    float h = (altitude - CLOUD_LAYER_BOTTOM) / CLOUD_LAYER_THICKNESS;
    if (h < 0.0 || h > 1.0) return 0.0;

    float height_grad = HeightGradient(h);
    if (height_grad < 0.001) return 0.0;

    // Wind advection
    vec3 wind = vec3(time * 28.0, 0.0, time * 9.0);
    vec3 sp = (pos + wind) * 0.00030;

    // Base 3D Perlin-Worley noise
    float base_noise = fbmNoise3D(sp);
    float worley_f1 = 1.0 - fastWorley3D(sp * 2.0);
    float perlin_worley = remapClamped(base_noise, 0.0, 1.0, worley_f1, 1.0);

    float coverage = 0.52;
    float base_cloud = remapClamped(perlin_worley * height_grad, 1.0 - coverage, 1.0, 0.0, 1.0);
    if (base_cloud <= 0.0) return 0.0;

    if (!is_shadow) {
        float detail_worley = 1.0 - fastWorley3D(sp * 5.5);
        float detail_erosion = detail_worley * (1.0 - h * 0.4) * 0.28;
        base_cloud = remapClamped(base_cloud, detail_erosion, 1.0, 0.0, 1.0);
    }

    return base_cloud * CLOUD_SIGMA_EXTINCTION;
}

// ---------------------------------------------------------------------------------------------------------
// Lighting & Optical Phase Functions
// ---------------------------------------------------------------------------------------------------------
float HenyeyGreenstein(float g, float cos_theta) {
    float g2 = g * g;
    return INV_4_PI * ((1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cos_theta, 1.5));
}

float CloudPhaseFunction(float cos_theta) {
    float forward  = HenyeyGreenstein(0.82, cos_theta);  // Forward silver-lining halo
    float backward = HenyeyGreenstein(-0.25, cos_theta); // Soft backward scattering
    return mix(forward, backward, 0.18);
}

float PowderSugarEffect(float optical_depth, float cos_theta) {
    float powder = 1.0 - exp(-optical_depth * 2.0);
    return mix(1.0, powder, saturate(-cos_theta * 0.5 + 0.5));
}

// ---------------------------------------------------------------------------------------------------------
// Planetary Atmosphere & Sky Radiance
// ---------------------------------------------------------------------------------------------------------
bool RaySphereIntersection(vec3 ro, vec3 rd, vec3 sc, float sr, out float t_near, out float t_far) {
    vec3 oc = ro - sc;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sr * sr;
    float d = b * b - c;
    if (d < 0.0) { t_near = -1.0; t_far = -1.0; return false; }
    float sq = sqrt(d);
    t_near = -b - sq;
    t_far  = -b + sq;
    return true;
}

vec3 ComputeAtmosphereSkyRadiance(vec3 ray_dir, vec3 sun_dir) {
    float sun_zenith = saturate(dot(sun_dir, vec3(0.0, 1.0, 0.0)));
    float view_zenith = saturate(dot(ray_dir, vec3(0.0, 1.0, 0.0)));
    float cos_theta = dot(ray_dir, sun_dir);

    // Rayleigh scattering gradient
    vec3 zenith_day  = vec3(0.16, 0.42, 0.88);
    vec3 horizon_day = vec3(0.68, 0.82, 0.98);
    vec3 zenith_sunset  = vec3(0.09, 0.14, 0.38);
    vec3 horizon_sunset = vec3(0.98, 0.40, 0.10);

    float sunset_factor = 1.0 - smoothstep(0.02, 0.38, sun_zenith);
    vec3 zenith_col = mix(zenith_day, zenith_sunset, sunset_factor);
    vec3 horizon_col = mix(horizon_day, horizon_sunset, sunset_factor);

    float horizon_grad = pow(1.0 - view_zenith, 3.8);
    vec3 sky_col = mix(zenith_col, horizon_col, horizon_grad);

    // Sun disk & Mie corona
    float sun_angular_size = cos(radians(0.533));
    float sun_disk = smoothstep(sun_angular_size - 0.0005, sun_angular_size + 0.0002, cos_theta);
    vec3 sun_illuminant = mix(vec3(1.0, 0.96, 0.88), vec3(1.0, 0.32, 0.06), sunset_factor);

    float mie = pow(saturate(cos_theta), 72.0) * 1.6 + pow(saturate(cos_theta), 12.0) * 0.32;
    sky_col += sun_illuminant * mie * (0.3 + 0.7 * sun_zenith);
    sky_col += sun_illuminant * sun_disk * 8.5;

    return sky_col;
}

// ---------------------------------------------------------------------------------------------------------
// Ultra-Fast Light Raymarch (3 Steps with Multi-Scattering Diffusion)
// ---------------------------------------------------------------------------------------------------------
float SampleLightRayTransmittance(vec3 pos, vec3 sun_dir, vec3 ec, float time) {
    float light_step = CLOUD_LAYER_THICKNESS / float(LIGHT_RAY_STEPS);
    float od = 0.0;
    for (int i = 0; i < LIGHT_RAY_STEPS; ++i) {
        float step_dist = (float(i) + 0.5) * light_step;
        vec3 sp = pos + sun_dir * step_dist;
        od += SampleCloudDensity(sp, ec, time, true) * light_step;
    }
    float direct = exp(-od * 1.15);
    float multi_scatter = exp(-od * 0.24) * 0.38;
    return max(direct, multi_scatter);
}

// ---------------------------------------------------------------------------------------------------------
// Primary 3D Cloud Raymarching Integrator (Smooth 60 FPS)
// ---------------------------------------------------------------------------------------------------------
vec3 RaymarchVolumetricClouds(vec3 ro, vec3 rd, vec3 sun_dir, vec3 sun_color, vec3 sky_ambient, float time, vec3 bg_color) {
    vec3 ec = vec3(0.0, -EARTH_BOTTOM_RADIUS, 0.0);
    float t_bn, t_bf, t_tn, t_tf;
    bool hit_b = RaySphereIntersection(ro, rd, ec, EARTH_BOTTOM_RADIUS + CLOUD_LAYER_BOTTOM, t_bn, t_bf);
    bool hit_t = RaySphereIntersection(ro, rd, ec, EARTH_BOTTOM_RADIUS + CLOUD_LAYER_TOP, t_tn, t_tf);

    if (!hit_b && !hit_t) return bg_color;

    float t_start = max(0.0, t_bf);
    float t_end = t_tf;
    t_end = min(t_end, t_start + MAX_MARCH_DISTANCE);
    if (t_end <= t_start) return bg_color;

    float total_len = t_end - t_start;
    float step_size = total_len / float(PRIMARY_RAY_STEPS);
    float cos_theta = dot(rd, sun_dir);
    float phase = CloudPhaseFunction(cos_theta);

    vec3 cloud_radiance = vec3(0.0);
    float transmittance = 1.0;
    float t = t_start + step_size * 0.5;

    for (int i = 0; i < PRIMARY_RAY_STEPS; ++i) {
        if (t >= t_end || transmittance < 0.02) break;

        vec3 cp = ro + rd * t;
        float density = SampleCloudDensity(cp, ec, time, false);

        if (density > 0.0001) {
            float altitude = length(cp - ec) - EARTH_BOTTOM_RADIUS;
            float heightFraction = saturate((altitude - CLOUD_LAYER_BOTTOM) / CLOUD_LAYER_THICKNESS);

            float light_trans = SampleLightRayTransmittance(cp, sun_dir, ec, time);
            float powder = PowderSugarEffect(density * step_size * 32.0, cos_theta);

            // Shading: bright sun highlight on top, silver lining rim, cool sky irradiance below
            vec3 ambient_sky = sky_ambient * (0.35 + 0.65 * heightFraction);
            vec3 ground_bounce = vec3(0.12, 0.14, 0.10) * (1.0 - heightFraction) * 0.45;

            vec3 direct_light = sun_color * light_trans * phase * powder * 4.6;
            vec3 total_light = direct_light + ambient_sky + ground_bounce;

            vec3 step_rad = total_light * density * CLOUD_SIGMA_SCATTERING * 240.0;
            float step_ext = exp(-density * step_size * 28.0);

            cloud_radiance += transmittance * (step_rad - step_rad * step_ext);
            transmittance *= step_ext;
        }
        t += step_size;
    }

    float fog_factor = 1.0 - exp(-t_start / (MAX_MARCH_DISTANCE * 0.88));
    vec3 final_clouds = mix(cloud_radiance, bg_color * (1.0 - transmittance), fog_factor);
    return mix(final_clouds, bg_color, transmittance);
}

vec3 ACESFilmicToneMapping(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ---------------------------------------------------------------------------------------------------------
// Main Pipeline Entry Point
// ---------------------------------------------------------------------------------------------------------
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord.xy - 0.5 * iResolution.xy) / iResolution.y;
    float time = iTime * 0.75;

    float yaw   = (iMouse.z > 0.0) ? (iMouse.x / iResolution.x - 0.5) * TWO_PI : time * 0.035;
    float pitch = (iMouse.z > 0.0) ? (iMouse.y / iResolution.y - 0.5) * PI * 0.45 : 0.14;

    vec3 cam_pos = vec3(0.0, 100.0, 0.0);
    vec3 forward = normalize(vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)));
    vec3 right   = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up      = cross(right, forward);
    vec3 ray_dir = normalize(forward + right * uv.x + up * uv.y);

    float sun_angle = time * 0.045 + 0.42;
    vec3 sun_dir = normalize(vec3(cos(sun_angle), sin(sun_angle) * 0.88 + 0.14, sin(sun_angle * 0.5)));
    float sun_zenith = saturate(dot(sun_dir, vec3(0.0, 1.0, 0.0)));

    vec3 sun_color = mix(vec3(1.0, 0.38, 0.08) * 4.0, vec3(1.0, 0.96, 0.90) * 5.6, smoothstep(0.04, 0.36, sun_zenith));
    vec3 sky_ambient = mix(vec3(0.14, 0.12, 0.26), vec3(0.36, 0.56, 0.88), sun_zenith);

    vec3 scene_color = ComputeAtmosphereSkyRadiance(ray_dir, sun_dir);

    if (ray_dir.y > -0.06) {
        scene_color = RaymarchVolumetricClouds(cam_pos, ray_dir, sun_dir, sun_color, sky_ambient, time, scene_color);
    } else {
        vec3 ground_color = vec3(0.07, 0.09, 0.05);
        scene_color = mix(scene_color, ground_color, saturate(-ray_dir.y * 12.0));
    }

    vec3 ldr = ACESFilmicToneMapping(scene_color);
    fragColor = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);
}
