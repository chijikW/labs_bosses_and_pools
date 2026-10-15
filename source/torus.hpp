#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace torus {

    struct Vertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    struct Mesh {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };

    // R Ч радиус "большой" окружности (от центра тора до центра трубы)
    // r Ч радиус трубы
    // N Ч количество сегментов вокруг большой окружности
    // M Ч количество сегментов вокруг трубы
    Mesh generate(float R = 1.0f, float r = 0.35f, int N = 64, int M = 32);

} // namespace torus