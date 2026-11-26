#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

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

    vec3 N = normalize(fragNormal);

    vec3 V = normalize(scene.camera_position.xyz - fragPosition);

    vec3 L = normalize(-scene.light_direction.xyz);

    //(Blinn-Phong)
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), 0.0);

    float specular = 0.0;

    if (diffuse > 0.0) {
        specular = pow(
            max(dot(N, H), 0.0),
            obj.specular_color_shininess.w
        );
    }

    vec3 ambient =
        scene.ambient_color_intensity.rgb *
        scene.ambient_color_intensity.w *
        obj.albedo.rgb;

    vec3 lightColor =
        scene.light_color_intensity.rgb *
        scene.light_color_intensity.w;

    vec3 diffuseColor =
        lightColor * obj.albedo.rgb * diffuse;

    vec3 specularColor =
        lightColor * obj.specular_color_shininess.rgb * specular;

    vec3 result = ambient + diffuseColor + specularColor;

    outColor = vec4(result, 1.0);
}