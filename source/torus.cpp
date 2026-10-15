#include "torus.hpp"

#include <cmath>

namespace torus {

    Mesh generate(float R, float r, int N, int M) {
        Mesh mesh;

        constexpr float twoPi = 6.28318530718f;

        mesh.vertices.reserve((N + 1) * (M + 1));
        mesh.indices.reserve(N * M * 6);

        for (int i = 0; i <= N; ++i) {
            const float u = twoPi * static_cast<float>(i) / static_cast<float>(N);
            const float cu = std::cos(u);
            const float su = std::sin(u);

            for (int j = 0; j <= M; ++j) {
                const float v = twoPi * static_cast<float>(j) / static_cast<float>(M);
                const float cv = std::cos(v);
                const float sv = std::sin(v);

                const glm::vec3 position{
                    (R + r * cv) * cu,
                    r * sv,
                    (R + r * cv) * su,
                };

                // Процедурный цвет по локальной позиции — пригодится
                // для дополнительного задания №5.
                const glm::vec3 color{
                    0.5f + 0.5f * std::sin(position.x * 2.0f),
                    0.5f + 0.5f * std::sin(position.y * 2.0f),
                    0.5f + 0.5f * std::sin(position.z * 2.0f),
                };

                mesh.vertices.push_back({ position, color });
            }
        }

        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < M; ++j) {
                const uint32_t a = static_cast<uint32_t>(i * (M + 1) + j);
                const uint32_t b = a + 1;
                const uint32_t c = a + static_cast<uint32_t>(M + 1);
                const uint32_t d = c + 1;

                mesh.indices.push_back(a);
                mesh.indices.push_back(c);
                mesh.indices.push_back(b);

                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
                mesh.indices.push_back(d);
            }
        }

        return mesh;
    }

} // namespace torus