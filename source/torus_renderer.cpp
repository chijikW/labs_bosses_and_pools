#include "torus_renderer.hpp"
#include "graphics_internal.hpp"
#include <cstring>
#include <iostream>
#include <fstream>

namespace {

    std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << '\n';
            return {};
        }

        const size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);

        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(file_size));
        file.close();

        return buffer;
    }

    VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
        const VkShaderModuleCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        };

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            std::cerr << "Failed to create shader module\n";
            return VK_NULL_HANDLE;
        }
        return module;
    }

} // namespace

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

    bool Renderer::createPipelineLayout() {
        // Push constants для tint из ImGui
        const VkPushConstantRange push_range = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(float) * 4, // vec4
        };

        const VkPipelineLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptor_set_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_range,
        };

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr,
            &pipeline_layout_) != VK_SUCCESS) {
            std::cerr << "Failed to create pipeline layout\n";
            return false;
        }
        return true;
    }

    bool Renderer::createPipeline() {
        const auto vert_code = readFile("shaders/torus.vert.spv");
        const auto frag_code = readFile("shaders/torus.frag.spv");

        if (vert_code.empty() || frag_code.empty()) {
            std::cerr << "Failed to read shader SPIR-V files. "
                "Did you compile shaders? (check shaders/ folder)\n";
            return false;
        }

        VkShaderModule vert_module = createShaderModule(device_, vert_code);
        VkShaderModule frag_module = createShaderModule(device_, frag_code);
        if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
            return false;
        }

        const VkPipelineShaderStageCreateInfo stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vert_module,
                .pName = "main",
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = frag_module,
                .pName = "main",
            },
        };

        // Описание формата вершин: position (vec3) + color (vec3)
        const VkVertexInputBindingDescription binding = {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        const VkVertexInputAttributeDescription attributes[] = {
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
              .offset = offsetof(Vertex, position) },
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
              .offset = offsetof(Vertex, color) },
        };

        const VkPipelineVertexInputStateCreateInfo vertex_input = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = attributes,
        };

        const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewport_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        const VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo color_blending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment,
        };

        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamic_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamic_states,
        };

        const VkGraphicsPipelineCreateInfo pipeline_info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depth_stencil,
            .pColorBlendState = &color_blending,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_,
            .renderPass = graphics::internal::context.render_pass,
            .subpass = 0,
        };

        // context из graphics::internal
        const VkResult result = vkCreateGraphicsPipelines(
            device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);

        vkDestroyShaderModule(device_, frag_module, nullptr);
        vkDestroyShaderModule(device_, vert_module, nullptr);

        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create graphics pipeline\n";
            return false;
        }
        return true;
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
        if (!createPipelineLayout()) {
            shutdown();
            return false;
        }
        if (!createPipeline()) {
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
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
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