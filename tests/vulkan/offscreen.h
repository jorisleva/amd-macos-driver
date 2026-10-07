// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Included after Harness/readShader, inside the probe's anonymous namespace.
constexpr VkFormat kImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};
struct ImageResources {
    Harness& h;
    Image source, target;
    Buffer upload, readback;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    explicit ImageResources(Harness& harness) : h(harness) {}
    ~ImageResources() {
        if (h.pending) return;
        if (framebuffer) vkDestroyFramebuffer(h.device, framebuffer, nullptr);
        for (auto* image : {&source, &target}) {
            if (image->view) vkDestroyImageView(h.device, image->view, nullptr);
            if (image->image) vkDestroyImage(h.device, image->image, nullptr);
            if (image->memory) vkFreeMemory(h.device, image->memory, nullptr);
        }
        for (auto* b : {&upload, &readback}) {
            if (b->mapped) vkUnmapMemory(h.device, b->memory);
            if (b->buffer) vkDestroyBuffer(h.device, b->buffer, nullptr);
            if (b->memory) vkFreeMemory(h.device, b->memory, nullptr);
        }
    }
};
std::string byteChecksum(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (auto b : bytes) { hash ^= b; hash *= 1099511628211ull; }
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
void writePixels(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) throw std::runtime_error("Cannot save readback/reference image");
}
struct OffscreenHarness {
    Harness& h;
    Report& report;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::array<VkShaderModule, 4> modules{};
    std::array<VkPipeline, 3> pipelines{};
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    struct Draw { offscreen::Color color; uint32_t shape; std::array<uint32_t, 3> padding{}; };
    static_assert(sizeof(Draw) == 32, "Push constant ABI");
    OffscreenHarness(Harness& harness, Report& r) : h(harness), report(r) {}
    ~OffscreenHarness() {
        if (h.pending || !h.device) return;
        for (auto pipeline : pipelines) if (pipeline) vkDestroyPipeline(h.device, pipeline, nullptr);
        for (auto module : modules) if (module) vkDestroyShaderModule(h.device, module, nullptr);
        if (sampler) vkDestroySampler(h.device, sampler, nullptr);
        if (renderPass) vkDestroyRenderPass(h.device, renderPass, nullptr);
    }
    void image(Image& out, offscreen::Size size, VkImageUsageFlags usage, const std::string& name, const char* role) {
        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        create.imageType = VK_IMAGE_TYPE_2D; create.format = kImageFormat;
        create.extent = {size.width, size.height, 1}; create.mipLevels = 1; create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT; create.tiling = VK_IMAGE_TILING_OPTIMAL; create.usage = usage;
        check(vkCreateImage(h.device, &create, nullptr, &out.image), "vkCreateImage");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(h.device, out.image, &requirements);
        VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(h.physical, &memory);
        uint32_t type = UINT32_MAX;
        for (uint32_t pass = 0; pass < 2 && type == UINT32_MAX; ++pass) for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            const auto flags = memory.memoryTypes[i].propertyFlags;
            if (!(requirements.memoryTypeBits & (1u << i)) || !(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) continue;
            if (!pass && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
            type = i; break;
        }
        if (type == UINT32_MAX) throw std::runtime_error("No device-local image memory");
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
        check(vkAllocateMemory(h.device, &alloc, nullptr, &out.memory), "vkAllocateMemory(image)");
        check(vkBindImageMemory(h.device, out.image, out.memory, 0), "vkBindImageMemory");
        const auto& chosen = memory.memoryTypes[type];
        report.allocations.push_back({name, role, 0, type, chosen.heapIndex, chosen.propertyFlags, memory.memoryHeaps[chosen.heapIndex].size});
        if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = out.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = kImageFormat;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(h.device, &view, nullptr, &out.view), "vkCreateImageView");
        }
    }
    void transition(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                    VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = srcAccess; barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout; barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, src, dst, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    void memoryBarrier(VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask = srcAccess; barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(command, src, dst, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    void initialize(const std::filesystem::path& shaderDirectory) {
        const auto& limits = report.devices[size_t(report.selected)].properties.limits;
        report.subPixelPrecisionBits = limits.subPixelPrecisionBits;
        if (limits.subPixelPrecisionBits < 8 || limits.maxFramebufferWidth < 257 || limits.maxFramebufferHeight < 129 ||
            limits.maxImageDimension2D < 257 || limits.maxPushConstantsSize < sizeof(Draw))
            throw std::runtime_error("GPU limits outside the offscreen reference contract");
        VkFormatProperties format{}; vkGetPhysicalDeviceFormatProperties(h.physical, kImageFormat, &format);
        report.formatFeatures = format.optimalTilingFeatures;
        constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((format.optimalTilingFeatures & required) != required) throw std::runtime_error("Required RGBA8 image features unavailable");
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = report.queueFamily; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; create.queueCreateInfoCount = 1; create.pQueueCreateInfos = &q;
        check(vkCreateDevice(h.physical, &create, nullptr, &h.device), "vkCreateDevice(graphics)");
        vkGetDeviceQueue(h.device, report.queueFamily, 0, &h.queue);
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; set.bindingCount = 1; set.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(h.device, &set, nullptr, &h.setLayout), "vkCreateDescriptorSetLayout(graphics)");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Draw)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1; layout.pSetLayouts = &h.setLayout; layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(h.device, &layout, nullptr, &h.pipelineLayout), "vkCreatePipelineLayout(graphics)");
        VkAttachmentDescription attachment{}; attachment.format = kImageFormat; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render.attachmentCount = 1; render.pAttachments = &attachment; render.subpassCount = 1; render.pSubpasses = &subpass;
        check(vkCreateRenderPass(h.device, &render, nullptr, &renderPass), "vkCreateRenderPass");
        const std::array<const char*, 4> names{"fullscreen.vert.spv", "texture.frag.spv", "triangle.vert.spv", "solid.frag.spv"};
        for (size_t i = 0; i < names.size(); ++i) {
            auto words = readShader((shaderDirectory / names[i]).string());
            VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; module.codeSize = words.size() * 4; module.pCode = words.data();
            check(vkCreateShaderModule(h.device, &module, nullptr, &modules[i]), "vkCreateShaderModule(graphics)");
        }
        for (size_t i = 0; i < pipelines.size(); ++i) {
            const bool sampled = i == 0, blend = i == 2;
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = modules[sampled ? 0 : 2];
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = modules[sampled ? 1 : 3];
            VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = 1; viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState colorBlend{}; colorBlend.blendEnable = blend ? VK_TRUE : VK_FALSE;
            colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlend.colorBlendOp = VK_BLEND_OP_ADD; colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
            colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blending.attachmentCount = 1; blending.pAttachments = &colorBlend;
            std::array<VkDynamicState, 2> states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = uint32_t(states.size()); dynamic.pDynamicStates = states.data();
            VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipeline.stageCount = uint32_t(stages.size()); pipeline.pStages = stages.data(); pipeline.pVertexInputState = &vertices; pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster; pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blending;
            pipeline.pDynamicState = &dynamic; pipeline.layout = h.pipelineLayout; pipeline.renderPass = renderPass;
            check(vkCreateGraphicsPipelines(h.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipelines[i]), "vkCreateGraphicsPipelines");
        }
        VkSamplerCreateInfo sample{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sample.magFilter = VK_FILTER_NEAREST; sample.minFilter = VK_FILTER_NEAREST; sample.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sample.addressModeU = sample.addressModeV = sample.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(h.device, &sample, nullptr, &sampler), "vkCreateSampler");
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &poolSize;
        check(vkCreateDescriptorPool(h.device, &pool, nullptr, &h.descriptorPool), "vkCreateDescriptorPool(graphics)");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = h.descriptorPool; da.descriptorSetCount = 1; da.pSetLayouts = &h.setLayout;
        check(vkAllocateDescriptorSets(h.device, &da, &descriptor), "vkAllocateDescriptorSets(graphics)");
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = report.queueFamily;
        check(vkCreateCommandPool(h.device, &cp, nullptr, &h.commandPool), "vkCreateCommandPool(graphics)");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = h.commandPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(h.device, &ca, &command), "vkAllocateCommandBuffers(graphics)");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; check(vkCreateFence(h.device, &fc, nullptr, &h.fence), "vkCreateFence(graphics)");
    }
    void execute(const std::string& scene, offscreen::Size size, uint32_t iteration, const std::filesystem::path& imageDirectory) {
        const bool copied = scene == "texture-copy", sampled = scene == "texture-sample", textured = copied || sampled, blend = scene == "alpha-blend";
        offscreen::Case result{}; result.scene = scene; result.width = size.width; result.height = size.height; result.iteration = iteration;
        result.name = scene + "-" + std::to_string(size.width) + "x" + std::to_string(size.height) + "-" + std::to_string(iteration);
        result.tolerance = textured ? 0u : 1u;
        auto expected = offscreen::reference(scene, size, iteration); result.covered = expected.covered; result.overlap = expected.overlap;
        const VkDeviceSize activeBytes = VkDeviceSize(size.width) * size.height * 4;
        const VkDeviceSize totalBytes = activeBytes + 2 * offscreen::kGuardBytes;
        ImageResources resources(h);
        h.allocate(resources.readback, totalBytes, false, report, result.name, "image-readback", 0);
        auto* readback = static_cast<uint8_t*>(resources.readback.mapped);
        std::fill_n(readback, size_t(totalBytes), offscreen::kGuardValue);
        std::vector<uint8_t> originalUpload;
        if (textured) {
            h.allocate(resources.upload, totalBytes, false, report, result.name, "image-upload", 0);
            originalUpload.assign(size_t(totalBytes), offscreen::kGuardValue);
            auto input = offscreen::texture(size, iteration);
            std::copy(input.begin(), input.end(), originalUpload.begin() + offscreen::kGuardBytes);
            std::memcpy(resources.upload.mapped, originalUpload.data(), originalUpload.size());
            image(resources.source, size, VK_IMAGE_USAGE_TRANSFER_DST_BIT | (copied ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : VK_IMAGE_USAGE_SAMPLED_BIT), result.name, "source-image");
        }
        if (!copied) {
            image(resources.target, size, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, result.name, "render-image");
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass = renderPass;
            fb.attachmentCount = 1; fb.pAttachments = &resources.target.view; fb.width = size.width; fb.height = size.height; fb.layers = 1;
            check(vkCreateFramebuffer(h.device, &fb, nullptr, &resources.framebuffer), "vkCreateFramebuffer");
        }
        if (sampled) {
            VkDescriptorImageInfo info{sampler, resources.source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = descriptor; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &info;
            vkUpdateDescriptorSets(h.device, 1, &write, 0, nullptr);
        }
        check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer(graphics)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(graphics)");
        // Include both upload visibility and the host-written readback sentinels before transfer writes.
        memoryBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{}; copy.bufferOffset = offscreen::kGuardBytes;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {size.width, size.height, 1};
        if (textured) {
            transition(resources.source.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(command, resources.upload.buffer, resources.source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            transition(resources.source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                copied ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, copied ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, copied ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT);
        }
        if (!copied) {
            transition(resources.target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            VkClearValue clear{}; const auto bg = offscreen::background(iteration);
            for (size_t c = 0; c < 4; ++c) clear.color.float32[c] = float(bg[c]) / 255.0f;
            VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; render.renderPass = renderPass;
            render.framebuffer = resources.framebuffer; render.renderArea.extent = {size.width, size.height}; render.clearValueCount = 1; render.pClearValues = &clear;
            vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport viewport{0, 0, float(size.width), float(size.height), 0, 1}; VkRect2D scissor{{0, 0}, {size.width, size.height}};
            vkCmdSetViewport(command, 0, 1, &viewport); vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[sampled ? 0 : (blend ? 2 : 1)]);
            if (sampled) {
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, h.pipelineLayout, 0, 1, &descriptor, 0, nullptr);
                vkCmdDraw(command, 3, 1, 0, 0);
            } else for (uint32_t draw = 0; draw < (blend ? 2u : 1u); ++draw) {
                const Draw push{offscreen::color(iteration, draw, blend), draw, {}};
                vkCmdPushConstants(command, h.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                vkCmdDraw(command, 3, 1, 0, 0);
            }
            vkCmdEndRenderPass(command);
            transition(resources.target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        }
        vkCmdCopyImageToBuffer(command, copied ? resources.source.image : resources.target.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resources.readback.buffer, 1, &copy);
        memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_HOST_READ_BIT);
        check(vkEndCommandBuffer(command), "vkEndCommandBuffer(graphics)");
        check(vkResetFences(h.device, 1, &h.fence), "vkResetFences(graphics)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        check(vkQueueSubmit(h.queue, 1, &submit, h.fence), "vkQueueSubmit(graphics)"); h.pending = true;
        check(vkWaitForFences(h.device, 1, &h.fence, VK_TRUE, kTimeoutNs), "vkWaitForFences(graphics)"); h.pending = false;
        std::vector<uint8_t> actual(readback + offscreen::kGuardBytes, readback + offscreen::kGuardBytes + size_t(activeBytes));
        result.differences = offscreen::compare(actual, expected.pixels, result.tolerance);
        result.differences.guards = offscreen::guards(readback, size_t(activeBytes));
        if (textured) {
            auto* upload = static_cast<const uint8_t*>(resources.upload.mapped);
            result.differences.guards += offscreen::guards(upload, size_t(activeBytes));
            for (size_t i = 0; i < originalUpload.size(); ++i) result.differences.upload += upload[i] != originalUpload[i];
        }
        result.outputChecksum = byteChecksum(actual); result.referenceChecksum = byteChecksum(expected.pixels);
        writePixels(imageDirectory / (result.name + ".rgba"), actual);
        writePixels(imageDirectory / (result.name + ".reference.rgba"), expected.pixels);
        report.graphicsCases.push_back(result);
        if (result.differences.pixels || result.differences.guards || result.differences.upload)
            throw std::runtime_error("Offscreen image verification failed: " + result.name);
        if (report.validationErrors.load()) throw std::runtime_error("Vulkan graphics validation error");
    }
    void run(const std::filesystem::path& shaders, const std::filesystem::path& images) {
        if (shaders.empty() || images.empty()) throw std::runtime_error("Graphics requires --shader-directory and --image-directory");
        std::filesystem::create_directories(images);
        initialize(shaders);
        const std::array<offscreen::Size, 5> textures{{{1, 1}, {3, 5}, {64, 64}, {65, 37}, {257, 129}}};
        const std::array<offscreen::Size, 3> triangles{{{32, 32}, {65, 37}, {127, 95}}};
        for (const std::string scene : {"texture-copy", "texture-sample", "triangle", "alpha-blend"}) {
            if (scene == "texture-copy" || scene == "texture-sample") {
                for (auto size : textures) for (uint32_t i = 0; i < 3; ++i) execute(scene, size, i, images);
            } else for (auto size : triangles) for (uint32_t i = 0; i < 3; ++i) execute(scene, size, i, images);
        }
        if (report.graphicsCases.size() != 48) throw std::runtime_error("Incomplete offscreen corpus");
    }
};
