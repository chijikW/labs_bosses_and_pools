#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;

    vec4 camera_position;

    vec4 light_direction;
    vec4 light_color_intensity;

    vec4 ambient_color_intensity;
} scene;

layout(set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec4 albedo;
    vec4 specular_color_shininess;
} obj;

void main() {

    vec4 worldPosition = obj.model * vec4(inPosition, 1.0);

    gl_Position = scene.view_projection * worldPosition;

    fragPosition = worldPosition.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(obj.model)));

    fragNormal = normalize(normalMatrix * inNormal);
}