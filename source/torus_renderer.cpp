#include "torus_renderer.hpp"

#include <cstring>
#include <iostream>

namespace torus {

    Renderer::~Renderer() {
        if (vertex_buffer_ != VK_NULL_HANDLE || index_buffer_ != VK_NULL_HANDLE) {
            std::cerr << "TorusRenderer destroyed without calling shutdown()!\n";
        }
    }

    bool Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkBuffer& out_buffer, VmaAllocation& out_allocation) {
        const VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        const VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo actual_alloc_info{};

        if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info,
            &out_buffer, &out_allocation,
            &actual_alloc_info) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan buffer via VMA\n";
            return false;
        }

        // Сохраняю указатель на отображённую память для быстрой записи.
        return true;
    }
    bool Renderer::createUniformBuffer() {
        const VkDeviceSize ubo_size = sizeof(UniformBufferObject);

        const VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = ubo_size,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        const VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo actual_alloc_info{};

        if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info,
            &uniform_buffer_, &uniform_allocation_,
            &actual_alloc_info) != VK_SUCCESS) {
            std::cerr << "Failed to create uniform buffer via VMA\n";
            return false;
        }

        uniform_mapped_ = actual_alloc_info.pMappedData;
        if (uniform_mapped_ == nullptr) {
            std::cerr << "Uniform buffer is not mapped (host access not available)\n";
            return false;
        }

        return true;
    }

    bool Renderer::createDescriptorSetLayout() {
        const VkDescriptorSetLayoutBinding ubo_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr,
        };

        const VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &ubo_binding,
        };

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr,
            &descriptor_set_layout_) != VK_SUCCESS) {
            std::cerr << "Failed to create descriptor set layout\n";
            return false;
        }
        return true;
    }

    bool Renderer::createDescriptorPoolAndSet() {
        const VkDescriptorPoolSize pool_size = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
        };

        const VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        };

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr,
            &descriptor_pool_) != VK_SUCCESS) {
            std::cerr << "Failed to create descriptor pool\n";
            return false;
        }

        const VkDescriptorSetAllocateInfo set_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptor_set_layout_,
        };

        if (vkAllocateDescriptorSets(device_, &set_info,
            &descriptor_set_) != VK_SUCCESS) {
            std::cerr << "Failed to allocate descriptor set\n";
            return false;
        }

        const VkDescriptorBufferInfo buffer_info = {
            .buffer = uniform_buffer_,
            .offset = 0,
            .range = sizeof(UniformBufferObject),
        };

        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set_,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buffer_info,
        };

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        return true;
    }

    void Renderer::updateUniforms(const UniformBufferObject& ubo) {
        if (uniform_mapped_ == nullptr) {
            return;
        }
        std::memcpy(uniform_mapped_, &ubo, sizeof(ubo));
    }
    bool Renderer::initialize(VmaAllocator allocator, VkDevice device) {
        allocator_ = allocator;
        device_ = device;

        // 1. Сгенерировать меш тора.
        const Mesh mesh = generate(1.0f, 0.35f, 64, 32);
        index_count_ = static_cast<uint32_t>(mesh.indices.size());

        if (mesh.vertices.empty() || mesh.indices.empty()) {
            std::cerr << "Torus mesh is empty!\n";
            return false;
        }

        // 2. Создать vertex buffer.
        const VkDeviceSize vb_size =
            static_cast<VkDeviceSize>(mesh.vertices.size() * sizeof(Vertex));

        if (!createBuffer(vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertex_buffer_, vertex_allocation_)) {
            return false;
        }

        // 3. Создать index buffer.
        const VkDeviceSize ib_size =
            static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));

        if (!createBuffer(ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            index_buffer_, index_allocation_)) {
            shutdown();
            return false;
        }

        // 4. Записать данные в буферы.
        {
            void* mapped = nullptr;
            if (vmaMapMemory(allocator_, vertex_allocation_, &mapped) != VK_SUCCESS) {
                std::cerr << "Failed to map vertex buffer\n";
                shutdown();
                return false;
            }
            std::memcpy(mapped, mesh.vertices.data(), static_cast<size_t>(vb_size));
            vmaUnmapMemory(allocator_, vertex_allocation_);
        }
        {
            void* mapped = nullptr;
            if (vmaMapMemory(allocator_, index_allocation_, &mapped) != VK_SUCCESS) {
                std::cerr << "Failed to map index buffer\n";
                shutdown();
                return false;
            }
            std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(ib_size));
            vmaUnmapMemory(allocator_, index_allocation_);
        }
        // 5. Uniform buffer + descriptor set.
        if (!createUniformBuffer()) {
            shutdown();
            return false;
        }
        if (!createDescriptorSetLayout()) {
            shutdown();
            return false;
        }
        if (!createDescriptorPoolAndSet()) {
            shutdown();
            return false;
        }
        std::cout << "Torus renderer initialized: "
            << mesh.vertices.size() << " vertices, "
            << mesh.indices.size() << " indices\n";

        return true;
    }

    void Renderer::shutdown() {
        if (allocator_ == VK_NULL_HANDLE) {
            return;
        }

        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
            descriptor_set_ = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
        if (uniform_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, uniform_buffer_, uniform_allocation_);
            uniform_buffer_ = VK_NULL_HANDLE;
            uniform_allocation_ = VK_NULL_HANDLE;
            uniform_mapped_ = nullptr;
        }

        if (index_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, index_buffer_, index_allocation_);
            index_buffer_ = VK_NULL_HANDLE;
            index_allocation_ = VK_NULL_HANDLE;
        }

        if (vertex_buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, vertex_buffer_, vertex_allocation_);
            vertex_buffer_ = VK_NULL_HANDLE;
            vertex_allocation_ = VK_NULL_HANDLE;
        }

        allocator_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
    }

} // namespace torus