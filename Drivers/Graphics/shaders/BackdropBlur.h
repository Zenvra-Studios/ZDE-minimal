#pragma once

namespace Zenvra::Graphics::Shaders {

// GLSL 1.20 is intentional: the Linux presenter may be attached to an older
// compatibility-profile GLX context. The 5x5 kernel is only used while a
// native popup backdrop is prepared, not on every UI frame.
inline constexpr const char* BlurVertexShader = R"glsl(
    #version 120
    varying vec2 vUv;

    void main() {
        gl_Position = ftransform();
        vUv = gl_MultiTexCoord0.xy;
    }
)glsl";

inline constexpr const char* BlurFragmentShader = R"glsl(
    #version 120
    uniform sampler2D uSource;
    uniform vec2 uTexelSize;
    uniform float uRadius;
    uniform float uSaturation;
    uniform vec4 uTint;
    uniform float uNoiseOpacity;
    varying vec2 vUv;

    // Uses Vogel spiral sampling for artifact-free large blur

    float interleavedNoise(vec2 p) {
        return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
    }

    void main() {
        vec4 blurred = vec4(0.0);
        float totalWeight = 0.0;
        
        const float GOLDEN_ANGLE = 2.39996323;
        const int SAMPLES = 64;
        
        // Per-pixel random rotation to turn banding into grain (dithering)
        float rot = interleavedNoise(gl_FragCoord.xy) * 6.2831853;
        float s = sin(rot);
        float c = cos(rot);
        mat2 rotMat = mat2(c, -s, s, c);
        
        for (int i = 0; i < SAMPLES; ++i) {
            // normalized radius [0, 1] mapped for uniform density
            float r = sqrt(float(i) + 0.5) / sqrt(float(SAMPLES));
            float theta = float(i) * GOLDEN_ANGLE;
            
            vec2 offset = vec2(cos(theta), sin(theta)) * r;
            offset = rotMat * offset; // apply dither rotation
            
            // Gaussian drop-off weight
            float weight = exp(-(r * r) * 4.0);
            blurred += texture2D(uSource, vUv + offset * max(uRadius, 1.0) * uTexelSize) * weight;
            totalWeight += weight;
        }
        blurred /= totalWeight;

        float luminance = dot(blurred.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec3 saturated = mix(vec3(luminance), blurred.rgb, uSaturation);
        vec3 material = mix(saturated, uTint.rgb, uTint.a);
        float grain = interleavedNoise(gl_FragCoord.xy) - 0.5;
        material += grain * uNoiseOpacity;

        gl_FragColor = vec4(clamp(material, 0.0, 1.0), 1.0);
}
)glsl";

} // namespace spectrax::ui::shaders