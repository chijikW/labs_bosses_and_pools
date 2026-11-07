#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace application {

    namespace {     // вершина и процедурная генерация тора
        struct Vertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        struct Mesh {
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;
        };

        Mesh generateTorus(float R, float r, int N, int M) {
            Mesh mesh;
            constexpr float twoPi = 6.28318530718f;

            mesh.vertices.reserve((N + 1) * (M + 1));
            mesh.indices.reserve(N * M * 6);

            for (int i = 0; i <= N; ++i) {
                const float u = twoPi * float(i) / float(N);
                const float cu = std::cos(u);
                const float su = std::sin(u);

                for (int j = 0; j <= M; ++j) {
                    const float v = twoPi * float(j) / float(M);
                    const float cv = std::cos(v);
                    const float sv = std::sin(v);

                    const glm::vec3 position{
                        (R + r * cv) * cu,
                        r * sv,
                        (R + r * cv) * su,
                    };

                    // процедурные цвета по локальной позиции
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
                    const uint32_t a = uint32_t(i * (M + 1) + j);
                    const uint32_t b = a + 1;
                    const uint32_t c = a + uint32_t(M + 1);
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

        // ================================================================
        // Uniform структуры
        // ================================================================

        struct SceneUniforms {
            glm::mat4 view_projection;
        };

        struct ModelUniforms {
            glm::mat4 model;
            glm::vec4 tint;
        };

        struct Object {
            VkBuffer        uniform_buffer = VK_NULL_HANDLE;
            VmaAllocation   uniform_allocation = VK_NULL_HANDLE;
            void* uniform_mapped = nullptr;
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        };


        struct SceneState {
            int   projection = 0;          // 0 = perspective | 1 = ortho
            float fov_deg = 60.0f;
            float ortho_half_height = 1.5f;

            // легкая трансформация
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 rotation_deg = glm::vec3(0.0f);
            glm::vec3 scale = glm::vec3(1.0f);

            bool  animate = true;
            bool  playing = true;
            float speed = 1.0f;
            float radius = 1.5f;
            float vertical_amplitude = 0.5f;
            int   vertical_frequency = 2;
            float spin_speed = 1.0f;

            glm::vec4 tint = glm::vec4(1.0f);
        };

        SceneState scene;
        double animation_time = 0.0;
        bool   show_demo = false;

        VkBuffer       vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation  vertex_allocation = VK_NULL_HANDLE;

        VkBuffer       index_buffer = VK_NULL_HANDLE;
        VmaAllocation  index_allocation = VK_NULL_HANDLE;
        uint32_t       index_count = 0;

        VkBuffer              scene_buffer = VK_NULL_HANDLE;
        VmaAllocation         scene_allocation = VK_NULL_HANDLE;
        void* scene_mapped = nullptr;
        VkDescriptorSet       scene_set = VK_NULL_HANDLE;
        VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;

        VkDescriptorSetLayout model_set_layout = VK_NULL_HANDLE;
        std::vector<Object>   objects;
        uint32_t              object_count = 0;

        VkDescriptorPool      descriptor_pool = VK_NULL_HANDLE;
        VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
        VkPipeline            pipeline = VK_NULL_HANDLE;

        std::vector<char> readFile(const char* path) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file) {
                std::cerr << "Failed to open: " << path << '\n';
                return {};
            }
            const size_t size = size_t(file.tellg());
            std::vector<char> buf(size);
            file.seekg(0);
            file.read(buf.data(), std::streamsize(size));
            return buf;
        }

        VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
            const VkShaderModuleCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data()),
            };
            VkShaderModule m = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &info, nullptr, &m);
            return m;
        }

        // ================================================================
        // создание буфера через Vma
        // ================================================================

        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& out_buffer, VmaAllocation& out_allocation,
            void** out_mapped = nullptr) {
            auto& ctx = graphics::internal::context;

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

            VmaAllocationInfo actual{};
            if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                &out_buffer, &out_allocation, &actual) != VK_SUCCESS) {
                std::cerr << "Failed to create buffer\n";
                return false;
            }
            if (out_mapped) *out_mapped = actual.pMappedData;
            return true;
        }

        // ================================================================
        // геометрия
        // ================================================================

        bool createGeometry() {
            auto& ctx = graphics::internal::context;

            const Mesh mesh = generateTorus(1.0f, 0.35f, 64, 32);
            index_count = uint32_t(mesh.indices.size());
            if (mesh.vertices.empty() || mesh.indices.empty()) {
                std::cerr << "Torus mesh empty\n";
                return false;
            }

            // Vertex buffer
            const VkDeviceSize vb_size = mesh.vertices.size() * sizeof(Vertex);
            if (!createBuffer(vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                vertex_buffer, vertex_allocation)) return false;
            {
                void* mapped = nullptr;
                vmaMapMemory(ctx.allocator, vertex_allocation, &mapped);
                std::memcpy(mapped, mesh.vertices.data(), size_t(vb_size));
                vmaUnmapMemory(ctx.allocator, vertex_allocation);
            }

            // Index buffer
            const VkDeviceSize ib_size = mesh.indices.size() * sizeof(uint32_t);
            if (!createBuffer(ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                index_buffer, index_allocation)) return false;
            {
                void* mapped = nullptr;
                vmaMapMemory(ctx.allocator, index_allocation, &mapped);
                std::memcpy(mapped, mesh.indices.data(), size_t(ib_size));
                vmaUnmapMemory(ctx.allocator, index_allocation);
            }

            std::cout << "Torus mesh: " << mesh.vertices.size() << " vertices, "
                << mesh.indices.size() << " indices\n";
            return true;
        }

        // ================================================================
        // Descriptor set layouts
        // ================================================================

        bool createDescriptorSetLayouts() {
            auto& ctx = graphics::internal::context;

            // set 0 —- scene
            const VkDescriptorSetLayoutBinding scene_binding = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            };
            const VkDescriptorSetLayoutCreateInfo scene_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &scene_binding,
            };
            if (vkCreateDescriptorSetLayout(ctx.device, &scene_info, nullptr,
                &scene_set_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create scene set layout\n";
                return false;
            }

            // set 1 —- model
            const VkDescriptorSetLayoutBinding model_binding = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            };
            const VkDescriptorSetLayoutCreateInfo model_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &model_binding,
            };
            if (vkCreateDescriptorSetLayout(ctx.device, &model_info, nullptr,
                &model_set_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create model set layout\n";
                return false;
            }
            return true;
        }

        // ================================================================
        // Uniform buffers
        // ================================================================

        bool createUniformBuffers(uint32_t max_objects) {
            auto& ctx = graphics::internal::context;

            // scene
            if (!createBuffer(sizeof(SceneUniforms),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                scene_buffer, scene_allocation, &scene_mapped)) {
                return false;
            }

            // objects
            object_count = std::max(1u, max_objects);
            objects.resize(object_count);
            for (uint32_t i = 0; i < object_count; ++i) {
                if (!createBuffer(sizeof(ModelUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    objects[i].uniform_buffer,
                    objects[i].uniform_allocation,
                    &objects[i].uniform_mapped)) {
                    return false;
                }
            }
            return true;
        }

        // ================================================================
        // Descriptor pool + sets
        // ================================================================

        bool createDescriptorPoolAndSets() {
            auto& ctx = graphics::internal::context;

            const VkDescriptorPoolSize pool_size = {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1 + object_count,
            };
            const VkDescriptorPoolCreateInfo pool_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1 + object_count,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size,
            };
            if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr,
                &descriptor_pool) != VK_SUCCESS) {
                std::cerr << "Failed to create descriptor pool\n";
                return false;
            }

            const VkDescriptorSetAllocateInfo scene_alloc = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &scene_set_layout,
            };
            if (vkAllocateDescriptorSets(ctx.device, &scene_alloc, &scene_set) != VK_SUCCESS) {
                return false;
            }
            {
                const VkDescriptorBufferInfo b = {
                    .buffer = scene_buffer, .offset = 0, .range = sizeof(SceneUniforms),
                };
                const VkWriteDescriptorSet w = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = scene_set,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &b,
                };
                vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            }

            for (uint32_t i = 0; i < object_count; ++i) {
                const VkDescriptorSetAllocateInfo alloc = {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptor_pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &model_set_layout,
                };
                if (vkAllocateDescriptorSets(ctx.device, &alloc,
                    &objects[i].descriptor_set) != VK_SUCCESS) {
                    return false;
                }
                const VkDescriptorBufferInfo b = {
                    .buffer = objects[i].uniform_buffer,
                    .offset = 0,
                    .range = sizeof(ModelUniforms),
                };
                const VkWriteDescriptorSet w = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = objects[i].descriptor_set,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &b,
                };
                vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            }
            return true;
        }

        // ================================================================
        // Pipeline
        // ================================================================

        bool createPipeline() {
            auto& ctx = graphics::internal::context;

            // шейдеры
            const auto vert_code = readFile("shaders/torus.vert.spv");
            const auto frag_code = readFile("shaders/torus.frag.spv");
            if (vert_code.empty() || frag_code.empty()) {
                std::cerr << "Failed to read shaders\n";
                return false;
            }
            VkShaderModule vert = createShaderModule(ctx.device, vert_code);
            VkShaderModule frag = createShaderModule(ctx.device, frag_code);
            if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) return false;

            // pipeline layout
            const VkDescriptorSetLayout layouts[] = {
                scene_set_layout,
                model_set_layout,
            };
            const VkPipelineLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 2,
                .pSetLayouts = layouts,
            };
            if (vkCreatePipelineLayout(ctx.device, &layout_info, nullptr,
                &pipeline_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline layout\n";
                return false;
            }

            // pipeline stages
            const VkPipelineShaderStageCreateInfo stages[] = {
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert, .pName = "main" },
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag, .pName = "main" },
            };

            // vertex input 
            const VkVertexInputBindingDescription binding = {
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
            const VkVertexInputAttributeDescription attrs[] = {
                {.location = 0, .binding = 0,
                  .format = VK_FORMAT_R32G32B32_SFLOAT,
                  .offset = offsetof(Vertex, position) },
                {.location = 1, .binding = 0,
                  .format = VK_FORMAT_R32G32B32_SFLOAT,
                  .offset = offsetof(Vertex, color) },
            };
            const VkPipelineVertexInputStateCreateInfo vi = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding,
                .vertexAttributeDescriptionCount = 2,
                .pVertexAttributeDescriptions = attrs,
            };

            const VkPipelineInputAssemblyStateCreateInfo ia = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };
            const VkPipelineViewportStateCreateInfo vp = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1, .scissorCount = 1,
            };
            const VkPipelineRasterizationStateCreateInfo rs = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f,
            };
            const VkPipelineMultisampleStateCreateInfo ms = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            };
            const VkPipelineDepthStencilStateCreateInfo ds = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
            };
            const VkPipelineColorBlendAttachmentState cba = {
                .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };
            const VkPipelineColorBlendStateCreateInfo cb = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1, .pAttachments = &cba,
            };
            const VkDynamicState dyn_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
            };
            const VkPipelineDynamicStateCreateInfo dyn = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2, .pDynamicStates = dyn_states,
            };

            const VkGraphicsPipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2, .pStages = stages,
                .pVertexInputState = &vi,
                .pInputAssemblyState = &ia,
                .pViewportState = &vp,
                .pRasterizationState = &rs,
                .pMultisampleState = &ms,
                .pDepthStencilState = &ds,
                .pColorBlendState = &cb,
                .pDynamicState = &dyn,
                .layout = pipeline_layout,
                .renderPass = ctx.render_pass,
                .subpass = 0,
            };

            const VkResult result = vkCreateGraphicsPipelines(
                ctx.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);

            vkDestroyShaderModule(ctx.device, vert, nullptr);
            vkDestroyShaderModule(ctx.device, frag, nullptr);

            if (result != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline\n";
                return false;
            }
            return true;
        }

    } // namespace



    bool initialize() {
        if (!createGeometry())                       return false;
        if (!createDescriptorSetLayouts())           return false;
        if (!createUniformBuffers(1))                return false;
        if (!createDescriptorPoolAndSets())          return false;
        if (!createPipeline())                       return false;
              
        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = 1.5f; 

        std::cout << "Application initialized\n";
        return true;
    }

    void shutdown() {
        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);

        if (pipeline)          vkDestroyPipeline(ctx.device, pipeline, nullptr);
        if (pipeline_layout)   vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
        if (descriptor_pool)   vkDestroyDescriptorPool(ctx.device, descriptor_pool, nullptr);
        if (model_set_layout)  vkDestroyDescriptorSetLayout(ctx.device, model_set_layout, nullptr);
        if (scene_set_layout)  vkDestroyDescriptorSetLayout(ctx.device, scene_set_layout, nullptr);

        for (auto& obj : objects) {
            if (obj.uniform_buffer != VK_NULL_HANDLE)
                vmaDestroyBuffer(ctx.allocator, obj.uniform_buffer, obj.uniform_allocation);
        }
        objects.clear();

        if (scene_buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx.allocator, scene_buffer, scene_allocation);
        if (index_buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx.allocator, index_buffer, index_allocation);
        if (vertex_buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx.allocator, vertex_buffer, vertex_allocation);
    }

    // ================================================================
    // ImGui + матрицы + запись в uniform buffers
    // ================================================================

    void update(double time) {
        const float dt = float(time);
        ImGui::SetNextWindowSize(ImVec2(450, 700), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::Begin("Torus controls");

        if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::RadioButton("Perspective", &scene.projection, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Orthographic", &scene.projection, 1);
            if (scene.projection == 0) {
                ImGui::SliderFloat("FOV (deg)", &scene.fov_deg, 20.0f, 120.0f);
            }
            else {
                ImGui::SliderFloat("Ortho half-height",
                    &scene.ortho_half_height, 0.5f, 5.0f);
            }
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &scene.position.x, 0.01f);
            ImGui::SliderFloat3("Rotation (deg)", &scene.rotation_deg.x, -180.0f, 180.0f);
            ImGui::DragFloat3("Scale", &scene.scale.x, 0.01f, 0.05f, 5.0f);
        }

        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable trajectory", &scene.animate);
            if (ImGui::Button(scene.playing ? "Pause" : "Play")) {
                scene.playing = !scene.playing;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset time")) animation_time = 0.0;
            ImGui::SliderFloat("Speed", &scene.speed, 0.0f, 5.0f);
            ImGui::SliderFloat("Radius", &scene.radius, 0.0f, 3.0f);
            ImGui::SliderFloat("Vertical amplitude", &scene.vertical_amplitude, 0.0f, 2.0f);
            ImGui::SliderInt("Vertical frequency", &scene.vertical_frequency, 1, 6);
            ImGui::SliderFloat("Spin speed", &scene.spin_speed, 0.0f, 5.0f);
        }

        if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Tint", &scene.tint.x);
        }

        if (ImGui::Button("Reset all")) {
            scene = SceneState();
            animation_time = 0.0;
        }
        ImGui::Checkbox("Show ImGui demo", &show_demo);
        ImGui::End();

        if (show_demo) ImGui::ShowDemoWindow(&show_demo);

        if (scene.animate && scene.playing) {
            animation_time += double(dt) * double(scene.speed);
        }
        const float t = float(animation_time);

        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 1.0f, 5.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));

        const auto extent = graphics::internal::context.swapchain_extent;
        const float aspect = float(extent.width) /
            float(std::max(extent.height, 1u));

        glm::mat4 proj;
        if (scene.projection == 0) {
            proj = glm::perspective(glm::radians(scene.fov_deg),
                aspect, 0.1f, 100.0f);
        }
        else {
            const float h = scene.ortho_half_height;
            proj = glm::ortho(-h * aspect, h * aspect, -h, h, 0.1f, 100.0f);
        }
        proj[1][1] *= -1.0f;
        proj[2][2] = proj[2][2] * 0.5f;
        proj[3][2] = proj[3][2] * 0.5f + 0.5f;

        SceneUniforms scene_ubo{};
        scene_ubo.view_projection = proj * view;
        std::memcpy(scene_mapped, &scene_ubo, sizeof(scene_ubo));

        glm::vec3 offset(0.0f);
        if (scene.animate) {
            offset = glm::vec3(
                scene.radius * std::cos(t),
                scene.vertical_amplitude * std::sin(float(scene.vertical_frequency) * t),
                scene.radius * std::sin(t));
        }
        glm::mat4 model = glm::translate(glm::mat4(1.0f),
            scene.position + offset);

        model = glm::rotate(model, glm::radians(scene.rotation_deg.z),
            glm::vec3(0, 0, 1));
        model = glm::rotate(model, glm::radians(scene.rotation_deg.y),
            glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(scene.rotation_deg.x),
            glm::vec3(1, 0, 0));

        if (scene.animate) {
            model = glm::rotate(model, scene.spin_speed * t,
                glm::vec3(0.5f, 1, 0));
        }

        model = glm::scale(model, scene.scale);

        // запись в uniform buffer
        ModelUniforms obj{};
        obj.model = model;
        obj.tint = scene.tint;
        std::memcpy(objects[0].uniform_mapped, &obj, sizeof(obj));
    }

    // ================================================================
    // render
    // ================================================================

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;

        const VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(fd.command_buffer, &begin);

        VkClearValue clear_values[2] = {};
        clear_values[0].color = { { 0.05f, 0.05f, 0.08f, 1.0f } };
        clear_values[1].depthStencil = { 1.0f, 0 };

        const VkRenderPassBeginInfo rp_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.render_pass,
            .framebuffer = fd.framebuffer,
            .renderArea = {.extent = ctx.swapchain_extent },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };
        vkCmdBeginRenderPass(fd.command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VkViewport viewport = {
            0.0f, 0.0f,
            float(ctx.swapchain_extent.width),
            float(ctx.swapchain_extent.height),
            0.0f, 1.0f,
        };
        const VkRect2D scissor = { {0, 0}, ctx.swapchain_extent };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

        const VkBuffer     vbs[] = { vertex_buffer };
        const VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vbs, offsets);
        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(fd.command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
            0, 1, &scene_set, 0, nullptr);

        vkCmdBindDescriptorSets(fd.command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
            1, 1, &objects[0].descriptor_set, 0, nullptr);

        vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application