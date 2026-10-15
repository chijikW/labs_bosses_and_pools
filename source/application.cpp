#include "application.hpp"

#include <imgui.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>

namespace application {

    State state;

    bool initialize() {
        auto& ctx = graphics::internal::context;

        if (!state.renderer.initialize(ctx.allocator, ctx.device)) {
            std::cerr << "Failed to initialize torus renderer\n";
            return false;
        }

        return true;
    }

    void shutdown() {
        state.renderer.shutdown();

        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);
    }

    void update(double time) {
        // time дельта между кадрами (сек)
        const float delta = static_cast<float>(time);

        if (state.playing) {
            state.elapsed += delta * state.speed;

            const float angle = state.elapsed;
            state.position.x = state.radius * std::cos(angle);
            state.position.z = state.radius * std::sin(angle);
            state.rotation.x = angle * 0.7f;
            state.rotation.y = angle;
        }

        // расчёт MVP
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, state.position);
        model = glm::rotate(model, state.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, state.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, state.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, state.scale);

        const glm::vec3 eye{ 0.0f, 1.0f, 4.0f };
        const glm::vec3 center{ 0.0f, 0.0f, 0.0f };
        const glm::vec3 up{ 0.0f, 1.0f, 0.0f };
        glm::mat4 view = glm::lookAt(eye, center, up);

        const auto& extent = graphics::internal::context.swapchain_extent;
        const float aspect = static_cast<float>(extent.width) /
            static_cast<float>(extent.height);

        glm::mat4 proj;
        if (state.usePerspective) {
            proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        }
        else {
            const float h = 3.0f;
            const float w = h * aspect;
            proj = glm::ortho(-w, w, -h, h, 0.1f, 100.0f);
        }

        // Vulkan: ось Y смотрит вниз — инвертируем Y
        proj[1][1] *= -1.0f;

        // Vulkan: z в NDC ∈ [0, 1], а glm (OpenGL) даёт z ∈ [-1, 1]
        // преобразование: z_vk = 0.5 * z_gl + 0.5
        proj[2][2] *= 0.5f;
        proj[3][2] = proj[3][2] * 0.5f + 0.5f;

        state.renderer.updateUniforms({ model, view, proj });

        // ImGui окно управления
        ImGui::Begin("Torus controls");

        ImGui::Checkbox("Perspective", &state.usePerspective);
        ImGui::SameLine();
        ImGui::TextDisabled("(uncheck = orthographic)");

        ImGui::Separator();
        ImGui::Text("Transform");
        ImGui::DragFloat3("Position", &state.position.x, 0.01f);
        ImGui::DragFloat3("Rotation", &state.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale", &state.scale.x, 0.01f);

        ImGui::Separator();
        ImGui::Text("Color");
        ImGui::ColorEdit4("Tint", &state.tint.x);

        ImGui::Separator();
        ImGui::Text("Animation");
        ImGui::Checkbox("Playing", &state.playing);
        ImGui::SliderFloat("Speed", &state.speed, 0.0f, 5.0f);
        ImGui::SliderFloat("Radius", &state.radius, 0.0f, 5.0f);

        ImGui::End();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;

        const VkCommandBuffer cmd = fd.command_buffer;

        //начало render pass
        VkClearValue color_clear{};
        color_clear.color = { { 0.05f, 0.05f, 0.08f, 1.0f } };
        VkClearValue depth_clear{};
        depth_clear.depthStencil = { 1.0f, 0 };

        const VkClearValue clear_values[] = { color_clear, depth_clear };

        const VkRenderPassBeginInfo render_pass_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.render_pass,
            .framebuffer = fd.framebuffer,
            .renderArea = {.extent = ctx.swapchain_extent },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

        // динамический viewport/scissor
        const VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(ctx.swapchain_extent.width),
            .height = static_cast<float>(ctx.swapchain_extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        const VkRect2D scissor = {
            .offset = { 0, 0 },
            .extent = ctx.swapchain_extent,
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // привязка pipeline и ресурсов
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            state.renderer.pipeline());

        const VkDescriptorSet descriptor_sets[] = { state.renderer.descriptorSet() };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            state.renderer.pipelineLayout(),
            0, 1, descriptor_sets,
            0, nullptr);

        vkCmdPushConstants(cmd, state.renderer.pipelineLayout(),
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4,
            &state.tint);

        const VkBuffer vertex_buffers[] = { state.renderer.vertexBuffer() };
        const VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(cmd, state.renderer.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // отрисовка
        vkCmdDrawIndexed(cmd, state.renderer.indexCount(), 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd); //конец render pass
    }

} // namespace application