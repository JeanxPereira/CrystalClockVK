#include "RenderOrchestrator.hpp"
#include "renderer/ShaderLoader.hpp"
#include <filesystem>
#include <iostream>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "../../3rdparty/stb_image.h"

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
    m_ctx = &ctx;

    m_passAlpha[0] = GsAlpha::alphaBlend();
    m_passAlpha[1] = GsAlpha::additive();
    m_passAlpha[2] = GsAlpha::additive();
    m_passAlpha[3] = GsAlpha::alphaBlend();
    m_passAlpha[4] = GsAlpha::reverseAlpha();

    createDescriptorResources(ctx);
    createPipelines(ctx.device(), swapchain.imageFormat());

    for (int i = 0; i < 2; i++) {
        m_uboBuffer[i] = resources.createBuffer(
            sizeof(FrameUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    uploadMeshes(resources);
    loadTextures(resources);

    // Initialize rod states
    for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
        m_rodState[i].selected = false;
        m_rodState[i].screenRatio = GsConstants::SCREEN_RATIO_16_9;
        m_rodState[i].yScale = 1.0f;
    }

    std::cout << "[OK] RenderOrchestrator initialized ("
              << m_rodVertexCount << " rod vertices, "
              << m_tunnelVertexCount << " tunnel vertices, "
              << PASS_COUNT << " GS passes)\n";
}

void RenderOrchestrator::createDescriptorResources(const VulkanContext& ctx) {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { uboBinding, samplerBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &m_descriptorLayout);

    std::vector<DescriptorAllocator::PoolSizeRatio> ratios = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f}
    };
    m_descriptorAllocator[0].init(ctx.device(), 10, ratios);
    m_descriptorAllocator[1].init(ctx.device(), 10, ratios);

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(ctx.device(), &sampInfo, nullptr, &m_sampler);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(CrystalPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &plInfo, nullptr, &m_pipelineLayout);
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

    m_tunnelPipeline = PipelineBuilder()
        .setShaders(tunnelVert, tunnelFrag)
        .setVertexInput(bindings, attributes)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_BACK_BIT)
        .setBlendMode(BlendMode::AlphaBlend)
        .setDepthTest(false, false)
        .setColorFormat(colorFormat)
        .setDepthFormat(VK_FORMAT_D32_SFLOAT)
        .setPipelineLayout(m_pipelineLayout)
        .build(device);

    // Pass 1/4: Glass refraction (alpha blend) — SRC_ALPHA / ONE_MINUS_SRC_ALPHA
    m_glassPipeline = PipelineBuilder()
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

    // Pass 2/3: Specular highlights (additive) — ONE / ONE
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

    // Pass 5: Reverse-alpha fill — ONE_MINUS_SRC_ALPHA / SRC_ALPHA
    m_reversePipeline = PipelineBuilder()
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

    std::cout << "[OK] Pipelines created (tunnel + glass + specular + reverse)\n";
}

void RenderOrchestrator::uploadMeshes(ResourceManager& resources) {
    auto rodVertices = CrystalGeometry::generateRodMesh();
    m_rodVertexCount = static_cast<uint32_t>(rodVertices.size());
    VkDeviceSize rodSize = sizeof(CrystalVertex) * m_rodVertexCount;
    m_rodVertexBuffer = resources.createBuffer(rodSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    resources.uploadToBuffer(m_rodVertexBuffer, rodVertices.data(), rodSize);

    auto cylVertices = CrystalGeometry::generateCylinderMesh();
    m_tunnelVertexCount = static_cast<uint32_t>(cylVertices.size());
    VkDeviceSize cylSize = sizeof(CrystalVertex) * m_tunnelVertexCount;
    m_tunnelVertexBuffer = resources.createBuffer(cylSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    resources.uploadToBuffer(m_tunnelVertexBuffer, cylVertices.data(), cylSize);

    std::cout << "[OK] Meshes uploaded: rod=" << m_rodVertexCount
              << " tunnel=" << m_tunnelVertexCount << " vertices\n";
}

void RenderOrchestrator::loadTextures(ResourceManager& resources) {
    std::vector<std::string> texPaths = {
        "resources/textures/noiseTexture.png",
        "../resources/textures/noiseTexture.png",
        "bin/resources/textures/noiseTexture.png",
    };

    int w, h, channels;
    unsigned char* data = nullptr;
    for (auto& path : texPaths) {
        data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (data) {
            std::cout << "[OK] Loaded noise texture: " << path << " (" << w << "x" << h << ")\n";
            break;
        }
    }

    if (!data) {
        std::cerr << "[WARN] Could not load noiseTexture.png, creating fallback\n";
        w = 64; h = 64;
        data = new unsigned char[w * h * 4];
        for (int i = 0; i < w * h * 4; i += 4) {
            unsigned char v = static_cast<unsigned char>(rand() % 128 + 64);
            data[i] = v; data[i+1] = v; data[i+2] = v; data[i+3] = 255;
        }
    }

    VkExtent2D texExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    m_noiseTexture = resources.createImage(texExtent, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    resources.uploadToImage(m_noiseTexture, data, texExtent, VK_FORMAT_R8G8B8A8_UNORM);
    stbi_image_free(data);
}

void RenderOrchestrator::updateRodStates(const FrameParams& params) {
    int highlightedRod = CrystalMath::getHighlightedRod(params.time.hour);
    bool isWidescreen = params.aspect > 1.5f;
    int hourCounter = static_cast<int>(params.time.secondsInHour);

    for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
        m_rodState[i].selected = (i == highlightedRod);
        m_rodState[i].screenRatio = isWidescreen
            ? GsConstants::SCREEN_RATIO_16_9
            : GsConstants::SCREEN_RATIO_4_3;
        m_rodState[i].yScale = GsCrystalMath::computeRodScale(
            i, GsConstants::INITIAL_SCALE_FACTOR, isWidescreen,
            m_rodState[i].screenRatio, m_rodState[i].selected, hourCounter);
    }
}

void RenderOrchestrator::updateUBO(const FrameParams& params) {
    float fov = glm::radians(CrystalMath::CAMERA_FOV);
    glm::mat4 proj = glm::perspective(fov, params.aspect, CrystalMath::CAMERA_NEAR, CrystalMath::CAMERA_FAR);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, CrystalMath::CAMERA_Z),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    float smoothSeconds = params.time.minute * 60.0f + params.time.secondsInMinute;

    FrameUBO ubo{};
    ubo.viewProj = proj * view;
    ubo.viewPos = glm::vec4(0.0f, 0.0f, CrystalMath::CAMERA_Z, 0.0f);
    ubo.prismColor = glm::vec4(CrystalMath::lerpPrismColor(smoothSeconds), params.totalTime);

    void* mapped = nullptr;
    vmaMapMemory(m_ctx->allocator(), m_uboBuffer[params.frameIndex].allocation, &mapped);
    std::memcpy(mapped, &ubo, sizeof(FrameUBO));
    vmaUnmapMemory(m_ctx->allocator(), m_uboBuffer[params.frameIndex].allocation);

    updateRodStates(params);
}

void RenderOrchestrator::recordTunnelPass(PassRecorder& recorder, const FrameParams& params) {
    m_descriptorAllocator[params.frameIndex].resetPools();

    m_tunnelDescSet[params.frameIndex] = m_descriptorAllocator[params.frameIndex].allocate(m_descriptorLayout);

    DescriptorWriter writer;
    writer.writeBuffer(0, m_uboBuffer[params.frameIndex].buffer, sizeof(FrameUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.writeImage(1, m_noiseTexture.imageView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.updateSet(params.device, m_tunnelDescSet[params.frameIndex]);

    float w = static_cast<float>(params.extent.width);
    float h = static_cast<float>(params.extent.height);

    recorder.bindPipeline(m_tunnelPipeline);
    recorder.bindDescriptorSet(m_pipelineLayout, 0, m_tunnelDescSet[params.frameIndex]);
    recorder.bindVertexBuffer(m_tunnelVertexBuffer.buffer);

    CrystalPushConstants pc{};
    pc.model = CrystalMath::buildTunnelMatrix();
    pc.rodColor = glm::vec4(0.0f);
    pc.screenParams = glm::vec4(w, h, params.totalTime * 0.004f, 0.0f);

    recorder.pushConstants(m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        &pc, sizeof(pc));
    recorder.draw(m_tunnelVertexCount);
}

void RenderOrchestrator::recordCrystalPasses(PassRecorder& recorder, const FrameParams& params) {
    m_crystalDescSet[params.frameIndex] = m_descriptorAllocator[params.frameIndex].allocate(m_descriptorLayout);

    DescriptorWriter writer;
    writer.writeBuffer(0, m_uboBuffer[params.frameIndex].buffer, sizeof(FrameUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.writeImage(1, params.tunnelImageView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.updateSet(params.device, m_crystalDescSet[params.frameIndex]);

    float smoothSeconds = params.time.minute * 60.0f + params.time.secondsInMinute;
    glm::vec3 globalPrismColor = CrystalMath::lerpPrismColor(smoothSeconds);

    // OSDSYS base angle: (float)*param_3 * in_f1
    // We derive from clock time — the base rotation from elapsed seconds
    float baseAngle = CrystalMath::lerpClockRotation(smoothSeconds / 60.0f);
    float hourAngle = CrystalMath::getClockRotationAngle(params.time.hour);

    // Pass 3 shimmer offsets — from param_3[0x2c] and param_3[0x2d]
    // These are clock-state-driven. We approximate from time progression.
    float shimmerOffsetX = std::sin(smoothSeconds * 0.1f) * 0.15f;
    float shimmerOffsetY = std::cos(smoothSeconds * 0.07f) * 0.12f;

    float w = static_cast<float>(params.extent.width);
    float h = static_cast<float>(params.extent.height);

    // ═══════════════════════════════════════════════════════════════════════
    // Draw helper — OSDSYS-accurate rod filtering
    // Pass 1-3: only rods with flag==0 (unselected)
    // Pass 4-5: only rods with flag!=0 (selected)
    // ═══════════════════════════════════════════════════════════════════════
    enum class PassMode { Unselected, SelectedOnly };

    auto drawRods = [&](VkPipeline pipeline, glm::vec4 color, float alpha,
                        PassMode mode, int passIndex) {
        recorder.bindPipeline(pipeline);
        recorder.bindDescriptorSet(m_pipelineLayout, 0, m_crystalDescSet[params.frameIndex]);
        recorder.bindVertexBuffer(m_rodVertexBuffer.buffer);

        float angleStep = GsConstants::ANGLE_STEP_P2_STATIC;
        if (passIndex == 2) angleStep = GsConstants::ANGLE_STEP_P3_STATIC;

        for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
            bool isSelected = m_rodState[i].selected;

            // OSDSYS rod filtering: flag==0 for P1-3, flag!=0 for P4-5
            if (mode == PassMode::Unselected && isSelected) continue;
            if (mode == PassMode::SelectedOnly && !isSelected) continue;

            float yScale = m_rodState[i].yScale;

            // Compute per-rod angle for this pass
            float rodAngle = CrystalMath::computeRodAngle(baseAngle, i, angleStep);

            // Build rotation matrix based on pass
            glm::mat4 orbitMatrix;
            if (passIndex == 0 || passIndex == 3 || passIndex == 4) {
                // Pass 1/4/5: glass — use hour angle + rod placement
                float rodPlacement = hourAngle + static_cast<float>(i) * (-CrystalMath::TAU / 12.0f);
                orbitMatrix = CrystalMath::buildRodMatrix(rodPlacement, rodPlacement);
            } else if (passIndex == 1) {
                // Pass 2: specular — SAME angle for both params
                // OSDSYS: FUN_00232e38(angle, angle, ...)
                orbitMatrix = CrystalMath::buildRodMatrix(rodAngle, rodAngle);
            } else {
                // Pass 3: shimmer — DIFFERENT angles
                // OSDSYS: FUN_00232e38(angle + param_3[0x2c], angle + param_3[0x2d])
                float angleA = rodAngle + shimmerOffsetX;
                float angleB = rodAngle + shimmerOffsetY;
                orbitMatrix = CrystalMath::buildRodMatrix(angleA, angleB);
            }

            glm::mat4 model = CrystalMath::buildFullRodModel(orbitMatrix, yScale);

            // Pass 5: alpha override 0xFF
            float passAlpha = alpha;
            if (passIndex == 4) passAlpha = static_cast<float>(GsConstants::PASS5_ALPHA_OVERRIDE) / 255.0f;

            CrystalPushConstants pc{};
            pc.model = model;
            pc.rodColor = color;
            pc.screenParams = glm::vec4(w, h, params.totalTime, passAlpha);

            recorder.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
            recorder.draw(m_rodVertexCount);
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // Pass 1: Base transparent glass (FB refraction)
    // OSDSYS: FUN_002324e8(1,0,1) — Cs*As + Cd*(1-As) — unselected rods
    // ═══════════════════════════════════════════════════════════════════════
    drawRods(m_glassPipeline,
             glm::vec4(0.15f, 0.25f, 0.45f, 1.0f), 0.4f,
             PassMode::Unselected, 0);

    // ═══════════════════════════════════════════════════════════════════════
    // Pass 2: Additive specular highlights — SAME angles
    // OSDSYS: FUN_00230fe8(2,1,2) — Cs + Cd — unselected rods
    // FUN_00232e38(angle, angle, ...) ← both params identical
    // ═══════════════════════════════════════════════════════════════════════
    drawRods(m_specularPipeline,
             glm::vec4(globalPrismColor * 0.8f, 1.0f), 0.7f,
             PassMode::Unselected, 1);

    // ═══════════════════════════════════════════════════════════════════════
    // Pass 3: Offset rotation shimmer — DIFFERENT angles (THE GHOSTING)
    // OSDSYS: FUN_00232e38(angle+param_3[0x2c], angle+param_3[0x2d])
    // ═══════════════════════════════════════════════════════════════════════
    drawRods(m_specularPipeline,
             glm::vec4(globalPrismColor * 0.8f, 1.0f), 0.7f,
             PassMode::Unselected, 2);

    // ═══════════════════════════════════════════════════════════════════════
    // Pass 4: Selected rod glass (alpha blend) — ONLY active rod
    // OSDSYS: FUN_00232538(1,0,1) — rods with flag!=0
    // ═══════════════════════════════════════════════════════════════════════
    drawRods(m_glassPipeline,
             glm::vec4(0.15f, 0.25f, 0.45f, 1.0f), 0.5f,
             PassMode::SelectedOnly, 3);

    // ═══════════════════════════════════════════════════════════════════════
    // Pass 5: Selected rod fill (reverse alpha) — ONLY active rod
    // OSDSYS: FUN_002324e8(0,1,1) + param=0xFF — rods with flag!=0
    // ═══════════════════════════════════════════════════════════════════════
    drawRods(m_reversePipeline,
             glm::vec4(globalPrismColor * 1.5f, 1.0f), 0.8f,
             PassMode::SelectedOnly, 4);
}

void RenderOrchestrator::destroy(VkDevice device, ResourceManager& resources) {
    resources.destroyBuffer(m_rodVertexBuffer);
    resources.destroyBuffer(m_tunnelVertexBuffer);
    resources.destroyImage(m_noiseTexture);
    for (int i = 0; i < 2; i++) {
        vmaDestroyBuffer(m_ctx->allocator(), m_uboBuffer[i].buffer, m_uboBuffer[i].allocation);
    }
    vkDestroySampler(device, m_sampler, nullptr);
    vkDestroyPipeline(device, m_tunnelPipeline, nullptr);
    vkDestroyPipeline(device, m_glassPipeline, nullptr);
    vkDestroyPipeline(device, m_specularPipeline, nullptr);
    vkDestroyPipeline(device, m_reversePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorLayout, nullptr);
    m_descriptorAllocator[0].destroy();
    m_descriptorAllocator[1].destroy();
}
