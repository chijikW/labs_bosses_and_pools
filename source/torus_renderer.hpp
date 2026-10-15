#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "torus.hpp"

namespace torus {

    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    class Renderer {
    public:
        Renderer() = default;
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        bool initialize(VmaAllocator allocator, VkDevice device);
        void shutdown();

        // Обновление матриц в uniform buffer.
        // Вызывается каждый кадр из application::update().
        void updateUniforms(const UniformBufferObject& ubo);

        // Геттеры для отрисовки
        VkBuffer vertexBuffer() const { return vertex_buffer_; }
        VkBuffer indexBuffer() const { return index_buffer_; }
        uint32_t indexCount() const { return index_count_; }

        VkPipeline pipeline() const { return pipeline_; }
        VkPipelineLayout pipelineLayout() const { return pipeline_layout_; }
        VkDescriptorSetLayout descriptorSetLayout() const { return descriptor_set_layout_; }
        VkDescriptorSet descriptorSet() const { return descriptor_set_; }

    private:
        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& out_buffer, VmaAllocation& out_allocation);

        bool createUniformBuffer();
        bool createDescriptorSetLayout();
        bool createDescriptorPoolAndSet();
        bool createPipelineLayout();
        bool createPipeline();

        VmaAllocator allocator_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;

        // Геометрия
        VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
        VmaAllocation vertex_allocation_ = VK_NULL_HANDLE;

        VkBuffer index_buffer_ = VK_NULL_HANDLE;
        VmaAllocation index_allocation_ = VK_NULL_HANDLE;

        uint32_t index_count_ = 0;

        // Uniform buffer
        VkBuffer uniform_buffer_ = VK_NULL_HANDLE;
        VmaAllocation uniform_allocation_ = VK_NULL_HANDLE;
        void* uniform_mapped_ = nullptr;

        // Descriptor set
        VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

        // Pipeline
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    };

} // namespace torus