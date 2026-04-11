#include "RenderOrchestrator.hpp"
#include "renderer/ShaderLoader.hpp"
#include <filesystem>
#include <iostream>

static std::filesystem::path findShaderDir() {
    std::vector<std::filesystem::path> candidates = {
        "bin/shaders", "shaders", "../bin/shaders",
    };
    for (auto& c : candidates) {
        if (std::filesystem::exists(c / "Crystal.vert.spv")) return c;
    }
    throw std::runtime_error("Cannot find compiled shaders (bin/shaders/*.spv)");
}

void RenderOrchestrator::init(const VulkanContext& ctx, const SwapchainManager& swapchain, ResourceManager& resources) {
    // GS ALPHA register configs per pass (from GsRegisterState.hpp)
    // Pass 1: (1,0,1) Glass FB refraction  → AlphaBlend
    // Pass 2: (2,1,2) Additive highlights   → Additive
    // Pass 3: (2,1,2) Additive offset       → Additive
    // Pass 4: (1,0,1) Active rod refract    → AlphaBlend
    // Pass 5: (0,1,1) Active rod fill       → ReverseAlpha
    m_passAlpha[0] = GsAlpha::alphaBlend();
    m_passAlpha[1] = GsAlpha::additive();
    m_passAlpha[2] = GsAlpha::additive();
    m_passAlpha[3] = GsAlpha::alphaBlend();
    m_passAlpha[4] = GsAlpha::reverseAlpha();

    // Set up Input Attachment descriptor layout
    VkDescriptorSetLayoutBinding inputAttachBinding{};
    inputAttachBinding.binding = 0;
    inputAttachBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    inputAttachBinding.descriptorCount = 1;
    inputAttachBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfoBinding{};
    layoutInfoBinding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfoBinding.bindingCount = 1;
    layoutInfoBinding.pBindings = &inputAttachBinding;
    if (vkCreateDescriptorSetLayout(ctx.device(), &layoutInfoBinding, nullptr, &m_inputAttachmentLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create input attachment layout");
    }

    // Initialize descriptor allocator for input attachments (we need 1 per frame, max inflight Frames is usually 2, allocate 10 safe mode)
    std::vector<DescriptorAllocator::PoolSizeRatio> ratios = { {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1.0f} };
    m_descriptorAllocator[0].init(ctx.device(), 10, ratios);
    m_descriptorAllocator[1].init(ctx.device(), 10, ratios);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(CrystalPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_inputAttachmentLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &layoutInfo, nullptr, &m_pipelineLayout);

    createPipelines(ctx.device(), swapchain.imageFormat());
    uploadMesh(resources);

    std::cout << "[OK] RenderOrchestrator initialized ("
              << m_vertexCount << " vertices/rod, "
              << PASS_COUNT << " GS passes configured)\n";
}

void RenderOrchestrator::createPipelines(VkDevice device, VkFormat colorFormat) {
    auto shaderDir = findShaderDir();

    VkShaderModule tunnelVert = ShaderLoader::loadModule(device, shaderDir / "Tunnel.vert.spv");
    VkShaderModule tunnelFrag = ShaderLoader::loadModule(device, shaderDir / "Tunnel.frag.spv");
    VkShaderModule crystalVert = ShaderLoader::loadModule(device, shaderDir / "Crystal.vert.spv");
    VkShaderModule crystalFrag = ShaderLoader::loadModule(device, shaderDir / "Crystal.frag.spv");
    VkShaderModule specularFrag = ShaderLoader::loadModule(device, shaderDir / "CrystalSpecular.frag.spv");

    auto binding = CrystalGeometry::getBindingDescription();
    auto attrs = CrystalGeometry::getAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> bindings = {binding};
    std::vector<VkVertexInputAttributeDescription> attributes(attrs.begin(), attrs.end());

    // Tunnel background — fullscreen, opaque, no depth, no vertex input
    m_tunnelPipeline = PipelineBuilder()
        .setShaders(tunnelVert, tunnelFrag)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setBlendMode(BlendMode::Opaque)
        .setDepthTest(false, false)
        .setColorFormat(colorFormat)
        .setDepthFormat(VK_FORMAT_D32_SFLOAT)
        .setPipelineLayout(m_pipelineLayout)
        .build(device);

    // Pass 1/4: Glass refraction (alpha blend) — GS ALPHA(1,0,1)
    m_glassPipeline = PipelineBuilder()
        .setFlags(VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)
        .setShaders(crystalVert, crystalFrag)
        .setVertexInput(bindings, attributes)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setBlendMode(BlendMode::AlphaBlend)
        .setDepthTest(true, true)
        .setColorFormat(colorFormat)
        .setDepthFormat(VK_FORMAT_D32_SFLOAT)
        .setPipelineLayout(m_pipelineLayout)
        .build(device);

    // Pass 2/3: Specular highlights (additive) — GS ALPHA(2,1,2)
    m_specularPipeline = PipelineBuilder()
        .setShaders(crystalVert, specularFrag)
        .setVertexInput(bindings, attributes)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setBlendMode(BlendMode::Additive)
        .setDepthTest(true, false)
        .setColorFormat(colorFormat)
        .setDepthFormat(VK_FORMAT_D32_SFLOAT)
        .setPipelineLayout(m_pipelineLayout)
        .build(device);

    // Pass 5: Reverse-alpha fill — GS ALPHA(0,1,1)
    m_reversePipeline = PipelineBuilder()
        .setFlags(VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)
        .setShaders(crystalVert, crystalFrag)
        .setVertexInput(bindings, attributes)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setBlendMode(BlendMode::ReverseAlpha)
        .setDepthTest(true, false)
        .setColorFormat(colorFormat)
        .setDepthFormat(VK_FORMAT_D32_SFLOAT)
        .setPipelineLayout(m_pipelineLayout)
        .build(device);

    vkDestroyShaderModule(device, tunnelVert, nullptr);
    vkDestroyShaderModule(device, tunnelFrag, nullptr);
    vkDestroyShaderModule(device, crystalVert, nullptr);
    vkDestroyShaderModule(device, crystalFrag, nullptr);
    vkDestroyShaderModule(device, specularFrag, nullptr);

#ifndef NDEBUG
    // Apply RenderDoc/Vulkan debugging names
    auto setDebugName = [&](VkPipeline pipe, const char* name) {
        auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
        if (func) {
            VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
            info.objectType = VK_OBJECT_TYPE_PIPELINE;
            info.objectHandle = (uint64_t)pipe;
            info.pObjectName = name;
            func(device, &info);
        }
    };
    
    setDebugName(m_tunnelPipeline, "Tunnel Background Pipeline");
    setDebugName(m_glassPipeline, "Glass Refraction Pipeline");
    setDebugName(m_specularPipeline, "Specular Highlight Pipeline");
    setDebugName(m_reversePipeline, "Reverse Alpha Active Pipeline");
#endif

    std::cout << "[OK] Pipelines created (tunnel + glass + specular + reverse)\n";
}

void RenderOrchestrator::uploadMesh(ResourceManager& resources) {
    auto meshVertices = CrystalGeometry::generateRodMesh();
    m_vertexCount = static_cast<uint32_t>(meshVertices.size());
    VkDeviceSize bufferSize = sizeof(CrystalVertex) * m_vertexCount;

    m_vertexBuffer = resources.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    resources.uploadToBuffer(m_vertexBuffer, meshVertices.data(), bufferSize);

    std::cout << "[OK] Crystal mesh uploaded: " << m_vertexCount << " vertices ("
              << bufferSize << " bytes)\n";
}

void RenderOrchestrator::recordFrame(PassRecorder& recorder, const FrameParams& params) {
    int highlightedRod = CrystalMath::getHighlightedRod(params.time.hour);
    
    // Continuous rod self-rotation
    float selfRotation = std::fmod(params.totalTime * (CrystalMath::TAU / 15.0f), CrystalMath::TAU);

    // Get the global cycling color (deep blue -> violet -> teal)
    glm::vec3 globalPrismColor = CrystalMath::lerpPrismColor(params.time.secondsInMinute);

    // Standard glass base color for transparent passes
    glm::vec4 glassColor = glm::vec4(0.15f, 0.25f, 0.45f, 1.0f);

    float fov = glm::radians(42.0f); 
    float halfWidth = static_cast<float>(params.extent.width) / 2.0f;
    float nearPlane = 0.1f;
    
    glm::mat4 proj = CrystalMath::buildProjectionMatrix(fov, halfWidth, nearPlane, params.aspect);
    proj[1][1] *= -1.0f; // Vulkan Y-flip

    float w = static_cast<float>(params.extent.width);
    float h = static_cast<float>(params.extent.height);
    float baseAngle = params.totalTime;

    // ═══════════════════════════════════════════════════════════════════════
    // TUNNEL BACKGROUND — fullscreen opaque draw
    // ═══════════════════════════════════════════════════════════════════════
    recorder.bindPipeline(m_tunnelPipeline);
    {
        CrystalPushConstants pc{};
        pc.mvp = glm::mat4(1.0f);
        pc.rodColor = glm::vec4(0.0f);
        pc.screenParams = glm::vec4(w, h, params.totalTime, 0.0f);

        recorder.pushConstants(m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            &pc, sizeof(pc));
        recorder.draw(3);
    }

    // Move camera backwards (from Z=6.5f to Z=11.5f) to see the entire ring
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 11.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Common lambda to draw unselected rods (Passes 1, 2, 3)
    auto drawUnselectedRods = [&](VkPipeline pipeline, glm::vec4 color, float alpha, float angleOffsetA) {
        recorder.bindPipeline(pipeline);
        for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
            if (i == highlightedRod) continue;

            float rodRingAngle = (i * CrystalMath::ANGLE_STEP_P2);
            float yScale = CrystalMath::computeRodScale(i, 1.0f, params.aspect > 1.5f, false, 0);
            
            // clockGroupPitch, clockGroupYaw dynamically turning the clock structure based on time
            float groupPitch = std::sin(params.totalTime * 0.2f) * 0.3f;
            float groupYaw = baseAngle * 0.1f;
            
            glm::mat4 model = CrystalMath::buildRodMatrix(rodRingAngle, angleOffsetA, yScale, selfRotation, groupPitch, groupYaw);
            glm::mat4 mvp = proj * view * model;

            CrystalPushConstants pc{};
            pc.mvp = mvp;
            pc.rodColor = color;
            pc.screenParams = glm::vec4(w, h, params.totalTime, alpha); 

            recorder.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
            recorder.draw(m_vertexCount);
        }
    };
    
    // Common lambda to draw selected rods (Passes 4, 5)
    auto drawSelectedRods = [&](VkPipeline pipeline, glm::vec4 color, float alpha) {
        recorder.bindPipeline(pipeline);
        
        float rodRingAngle = (highlightedRod * CrystalMath::ANGLE_STEP_P2);
        
        int hourCounter = static_cast<int>(params.time.minute * 60 + params.time.secondsInMinute);
        float yScale = CrystalMath::computeRodScale(highlightedRod, 1.0f, params.aspect > 1.5f, true, hourCounter);
        
        float groupPitch = std::sin(params.totalTime * 0.2f) * 0.3f;
        float groupYaw = baseAngle * 0.1f;
        
        glm::mat4 model = CrystalMath::buildRodMatrix(rodRingAngle, 0.0f, yScale, selfRotation, groupPitch, groupYaw);
        glm::mat4 mvp = proj * view * model;

        CrystalPushConstants pc{};
        pc.mvp = mvp;
        pc.rodColor = color;
        pc.screenParams = glm::vec4(w, h, params.totalTime, alpha); 

        recorder.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
        recorder.draw(m_vertexCount);
    };

    recorder.bindVertexBuffer(m_vertexBuffer.buffer);

    // Pass 1: Base transparent glass (Requires Local Read Framebuffer Refraction)
    {
        m_descriptorAllocator[params.frameIndex].resetPools();
        VkDescriptorSet inputAttachSet = m_descriptorAllocator[params.frameIndex].allocate(m_inputAttachmentLayout);
        
        DescriptorWriter writer;
        writer.writeImage(0, params.currentImageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
        writer.updateSet(params.device, inputAttachSet);

        // Transition from rendering output to input read by injecting a region dependency
        recorder.insertLocalReadBarrier();

        // 0 = index of the color attachment we are reading from
        recorder.setLocalReadInputIndices(0);
        recorder.bindDescriptorSet(m_pipelineLayout, 0, inputAttachSet);
        drawUnselectedRods(m_glassPipeline, glassColor, 0.48f, 0.0f);
    }
    
    // Pass 2: Specular highlights
    drawUnselectedRods(m_specularPipeline, glm::vec4(globalPrismColor * 1.5f, 1.0f), 0.7f, 0.0f);
    
    // Pass 3: Offset rotation shimmer (THE OSDSYS GHOSTING!)
    // Using a single offset for tilt
    drawUnselectedRods(m_specularPipeline, glm::vec4(globalPrismColor * 1.0f, 1.0f), 0.5f, CrystalMath::SHIMMER_OFFSET_X);
                       
    // Barrier to make writes from unselected rods visible to subpassLoad in selected rods
    recorder.insertLocalReadBarrier();

    // Pass 4: Highlighted rod glass
    drawSelectedRods(m_glassPipeline, glm::vec4(0.2f, 0.4f, 0.7f, 1.0f), 0.72f);
    
    // Pass 5: Highlighted rod fill (Reverse alpha)
    drawSelectedRods(m_reversePipeline, glm::vec4(globalPrismColor * 2.0f, 1.0f), 0.8f);
}

void RenderOrchestrator::destroy(VkDevice device, ResourceManager& resources) {
    resources.destroyBuffer(m_vertexBuffer);
    vkDestroyPipeline(device, m_tunnelPipeline, nullptr);
    vkDestroyPipeline(device, m_glassPipeline, nullptr);
    vkDestroyPipeline(device, m_specularPipeline, nullptr);
    vkDestroyPipeline(device, m_reversePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_inputAttachmentLayout, nullptr);
    m_descriptorAllocator[0].destroy();
    m_descriptorAllocator[1].destroy();
}
