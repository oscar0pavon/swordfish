#version 450
#extension GL_ARB_separate_shader_objects : enable


layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec2 out_uv;

void main() {
    gl_Position = ubo.proj * ubo.model * vec4(inPosition, 1.0);
    out_uv = uv;
}
