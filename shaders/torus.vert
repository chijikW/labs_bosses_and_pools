#version 450

// Вход: атрибуты вершины (позиция + цвет)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// Выход: цвет передаётся во фрагментный шейдер
layout(location = 0) out vec3 fragColor;

// Uniform buffer с MVP-матрицами
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}