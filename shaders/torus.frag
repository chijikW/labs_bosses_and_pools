#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec4 tint;
} obj;

void main() {
    outColor = vec4(fragColor * obj.tint.rgb, 1.0);
}