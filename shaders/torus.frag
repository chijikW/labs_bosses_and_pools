#version 450

// Вход: цвет от вершинного шейдера
layout(location = 0) in vec3 fragColor;

// Выход: цвет пикселя
layout(location = 0) out vec4 outColor;

// Push constant: множитель цвета из ImGui
layout(push_constant) uniform PushConstants {
    vec4 tint;
} pc;

void main() {
    outColor = vec4(fragColor * pc.tint.rgb, 1.0);
}