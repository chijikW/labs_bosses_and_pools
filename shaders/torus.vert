#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;
} scene;

layout(set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec4 tint;
} obj;

void main() {
    gl_Position = scene.view_projection * obj.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}