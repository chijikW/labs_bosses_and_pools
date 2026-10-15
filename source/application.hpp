#pragma once

#include "graphics_internal.hpp"
#include "torus_renderer.hpp"

#include <glm/glm.hpp>

namespace application {

    // Глоб. сост. приложения.
    struct State {
        torus::Renderer renderer;

        // Трансформация тора
        glm::vec3 position{ 0.0f, 0.0f, 0.0f };
        glm::vec3 rotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

        // Цвет
        glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Анимация
        bool  playing = true;
        float speed = 1.0f;
        float radius = 1.5f;
        float elapsed = 0.0f;

        // Проекция
        bool  usePerspective = true;
    };

    extern State state;

    bool initialize();
    void shutdown();

    void update(double time);
    void render(const graphics::internal::FrameData& fd);

} // namespace application