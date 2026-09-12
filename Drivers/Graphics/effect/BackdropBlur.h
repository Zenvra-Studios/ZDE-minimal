#pragma once

#include <array>

namespace Zenvra::Graphics {

/// Modern card geometry configuration for the backdrop blur shader mask.
/// Excludes solid UI card regions (Explorer, Text Editor, Terminal, Shader Sandbox)
/// matching the Windows 11 / modern acrylic backdrop blur behavior.
struct ModernCardBlurMask {
    std::array<float, 4> explorer_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> editor_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> terminal_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> shader_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> popup_card = {0.0F, 0.0F, 0.0F, 0.0F};
    float card_radius = 8.0F;

    std::array<float, 4> explorer_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> editor_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> terminal_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> shader_color = {0.0F, 0.0F, 0.0F, 0.0F};

    int enable_blur = 1; // 1 = blur triggered, 0 = pass-through solid
};

namespace Shaders {

// GLSL 1.20 is intentional: the Linux presenter may be attached to an older
// compatibility-profile GLX context.
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

    // Modern card geometry (x, y, width, height) in window pixel coordinates
    // Matches Win32 card layout: Explorer, Editor, Terminal, Shader Sandbox
    uniform vec4 uExplorerCard;
    uniform vec4 uEditorCard;
    uniform vec4 uTerminalCard;
    uniform vec4 uShaderCard;
    uniform vec4 uPopupCard;
    uniform float uCardRadius;

    // Optional solid color overrides for cards (RGBA, [0..1])
    uniform vec4 uExplorerColor;
    uniform vec4 uEditorColor;
    uniform vec4 uTerminalColor;
    uniform vec4 uShaderColor;

    // Trigger switch: 1 = blur triggered, 0 = pass-through
    uniform int uEnableBlur;

    varying vec2 vUv;

    // Interleaved gradient noise for dithering & grain
    float interleavedNoise(vec2 p) {
        return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
    }

    // Signed distance function for a 2D rounded rectangle
    float sdRoundedBox(vec2 p, vec2 center, vec2 halfSize, float radius) {
        vec2 d = abs(p - center) - halfSize + vec2(radius);
        return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
    }

    // Calculate normalized coverage [0.0 = outside, 1.0 = solid inside] for a card
    float getCardCoverage(vec2 p, vec4 card, float radius) {
        if (card.z <= 0.0 || card.w <= 0.0) {
            return 0.0;
        }
        float r = min(radius, min(card.z, card.w) * 0.5);
        vec2 center = card.xy + card.zw * 0.5;
        vec2 halfSize = card.zw * 0.5;
        float dist = sdRoundedBox(p, center, halfSize, r);
        // 1-pixel smooth anti-aliased edge
        return clamp(0.5 - dist, 0.0, 1.0);
    }

    void main() {
        // If blur is not triggered, pass-through original window texture
        if (uEnableBlur == 0) {
            gl_FragColor = texture2D(uSource, vUv);
            return;
        }

        // Convert fragment coordinates to window pixel space (top-left origin)
        vec2 res = vec2(1.0 / max(uTexelSize.x, 1e-6), 1.0 / max(uTexelSize.y, 1e-6));
        vec2 pixelPos = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y);

        // Compute coverage for each solid card region
        float explorerCov = getCardCoverage(pixelPos, uExplorerCard, uCardRadius);
        float editorCov = getCardCoverage(pixelPos, uEditorCard, uCardRadius);
        float terminalCov = getCardCoverage(pixelPos, uTerminalCard, uCardRadius);
        float shaderCov = getCardCoverage(pixelPos, uShaderCard, uCardRadius);
        float popupCov = getCardCoverage(pixelPos, uPopupCard, uCardRadius);

        float totalSolidCoverage = clamp(explorerCov + editorCov + terminalCov + shaderCov + popupCov, 0.0, 1.0);

        // Fast-path: fully inside a solid card (explorer, editor, terminal, shader)
        // Skip expensive blur sampling completely for massive performance boost
        if (totalSolidCoverage >= 0.999) {
            vec4 solidCol = texture2D(uSource, vUv);
            if (explorerCov > 0.5 && uExplorerColor.a > 0.0) {
                solidCol = uExplorerColor;
            } else if (editorCov > 0.5 && uEditorColor.a > 0.0) {
                solidCol = uEditorColor;
            } else if (terminalCov > 0.5 && uTerminalColor.a > 0.0) {
                solidCol = uTerminalColor;
            } else if (shaderCov > 0.5 && uShaderColor.a > 0.0) {
                solidCol = uShaderColor;
            }
            gl_FragColor = vec4(solidCol.rgb, 1.0);
            return;
        }

        // Outside solid cards (titlebar, margins, gaps, status bar): compute Vogel spiral backdrop blur
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
        vec4 blurredColor = vec4(clamp(material, 0.0, 1.0), 1.0);

        // If on the anti-aliased edge between blur background and a solid card, smoothly blend
        if (totalSolidCoverage > 0.001) {
            vec4 solidCol = texture2D(uSource, vUv);
            if (explorerCov > 0.001 && uExplorerColor.a > 0.0) {
                solidCol = mix(solidCol, uExplorerColor, explorerCov);
            } else if (editorCov > 0.001 && uEditorColor.a > 0.0) {
                solidCol = mix(solidCol, uEditorColor, editorCov);
            } else if (terminalCov > 0.001 && uTerminalColor.a > 0.0) {
                solidCol = mix(solidCol, uTerminalColor, terminalCov);
            } else if (shaderCov > 0.001 && uShaderColor.a > 0.0) {
                solidCol = mix(solidCol, uShaderColor, shaderCov);
            }
            gl_FragColor = mix(blurredColor, vec4(solidCol.rgb, 1.0), totalSolidCoverage);
        } else {
            gl_FragColor = blurredColor;
        }
    }
)glsl";

} // namespace Shaders
} // namespace Zenvra::Graphics