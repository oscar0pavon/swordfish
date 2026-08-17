#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 light_position;
} ubo;

//binding 0, stepped per vertex: the unit box
layout(location = 0) in vec3 in_position;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

//binding 1, stepped per instance: one building
layout(location = 6) in vec3 instance_position;
layout(location = 7) in vec3 instance_scale;
layout(location = 8) in vec3 instance_color;

//the entry name, 4 characters packed per uint
layout(location = 9) in uvec4 instance_name0;
layout(location = 10) in uvec4 instance_name1;
layout(location = 11) in float instance_name_length;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_color;
layout(location = 2) out float out_view_depth;
layout(location = 3) out float out_is_roof;
layout(location = 4) out float out_time;

layout(location = 5) flat out uvec4 out_name0;
layout(location = 6) flat out uvec4 out_name1;
layout(location = 7) flat out float out_name_length;
layout(location = 8) out vec3 out_scale;

void main() {
    //the box sits on the ground and spans 0..1 upward, so scaling z
    //grows the building without pushing it through the floor
    vec3 world = in_position * instance_scale + instance_position;

    vec4 view_position = ubo.view * ubo.model * vec4(world, 1.0);
    gl_Position = ubo.proj * view_position;

    //rescale uv into world units so window rows stay the same size
    //whether the building is two storeys or fifty
    out_uv = vec2(in_uv.x * instance_scale.x, in_uv.y * instance_scale.z);

    out_color = instance_color;
    out_view_depth = length(view_position.xyz);

    //z facing quads are roofs, they get a flat glow instead of windows
    out_is_roof = abs(in_normal.z);

    //the fragment stage cannot read the ubo, so hand the clock over here
    out_time = ubo.light_position.w;

    out_name0 = instance_name0;
    out_name1 = instance_name1;
    out_name_length = instance_name_length;
    out_scale = instance_scale;
}
