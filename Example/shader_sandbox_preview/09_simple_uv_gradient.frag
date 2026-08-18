#version 450 core

// Standard GLSL Fragment Shader UV Gradient
// Compatible with ZDE Shader Sandbox

in vec2 v_uv;
out vec4 frag_color;

void main()
{
    frag_color = vec4(v_uv, 0.5, 1.0);
}
