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
            VMA_MEMORY_USAGE_AUTO);
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

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.binding = 2;
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.descriptorCount = 1;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { uboBinding, samplerBinding, normalBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &m_descriptorLayout);

    std::vector<DescriptorAllocator::PoolSizeRatio> ratios = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2.0f}
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
        .setBlendMode(BlendMode::Opaque)
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
        .setDepthTest(false, false)
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
    auto loadOne = [&](const std::vector<std::string>& paths, const char* label,
                       AllocatedImage& dst, glm::u8vec4 fallback) {
        int w = 0, h = 0, channels = 0;
        unsigned char* data = nullptr;
        for (auto& path : paths) {
            data = stbi_load(path.c_str(), &w, &h, &channels, 4);
            if (data) {
                std::cout << "[OK] Loaded " << label << ": " << path << " (" << w << "x" << h << ")\n";
                break;
            }
        }
        if (!data) {
            std::cerr << "[WARN] Could not load " << label << ", creating fallback\n";
            w = 64; h = 64;
            data = new unsigned char[w * h * 4];
            for (int i = 0; i < w * h * 4; i += 4) {
                data[i+0] = fallback.r; data[i+1] = fallback.g;
                data[i+2] = fallback.b; data[i+3] = fallback.a;
            }
        }
        VkExtent2D texExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
        dst = resources.createImage(texExtent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        resources.uploadToImage(dst, data, texExtent, VK_FORMAT_R8G8B8A8_UNORM);
        stbi_image_free(data);
    };

    loadOne({
        "resources/textures/noiseTexture.png",
        "../resources/textures/noiseTexture.png",
        "bin/resources/textures/noiseTexture.png",
    }, "noise texture", m_noiseTexture, {128, 128, 128, 255});

    loadOne({
        "resources/textures/normal.jpg",
        "../resources/textures/normal.jpg",
        "bin/resources/textures/normal.jpg",
    }, "normal map", m_normalTexture, {128, 128, 255, 255});
}

void RenderOrchestrator::updateRodStates(const FrameParams& params) {
    int highlightedRod = CrystalMath::getHighlightedRod(params.time.hour);
    bool isWidescreen = params.aspect > 1.5f;
    float minutesInHour = static_cast<float>(params.time.minute) + params.time.secondsInMinute / 60.0f;
    float selectedFill = 1.0f - (minutesInHour / 60.0f);

    for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
        m_rodState[i].selected = (i == highlightedRod);
        m_rodState[i].screenRatio = isWidescreen
            ? GsConstants::SCREEN_RATIO_16_9
            : GsConstants::SCREEN_RATIO_4_3;
        m_rodState[i].yScale = m_rodState[i].selected ? selectedFill : 1.0f;
    }
}

void RenderOrchestrator::updateUBO(const FrameParams& params) {
    float fov = glm::radians(CrystalMath::CAMERA_FOV);
    glm::mat4 proj = GsCrystalMath::buildGsProjection(
        fov, params.aspect, CrystalMath::CAMERA_NEAR, CrystalMath::CAMERA_FAR);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, CrystalMath::CAMERA_Z),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    float smoothSeconds = params.time.minute * 60.0f + params.time.secondsInMinute;

    FrameUBO ubo{};
    ubo.viewProj = proj * view;
    ubo.view = view;
    ubo.viewPos = glm::vec4(0.0f, 0.0f, CrystalMath::CAMERA_Z, 1.0f);
    ubo.prismColor = glm::vec4(CrystalMath::lerpPrismColor(smoothSeconds), 1.0f);

    std::memcpy(m_uboBuffer[params.frameIndex].allocationInfo.pMappedData, &ubo, sizeof(FrameUBO));
    vmaFlushAllocation(m_ctx->allocator(), m_uboBuffer[params.frameIndex].allocation, 0, sizeof(FrameUBO));

    updateRodStates(params);
}

void RenderOrchestrator::recordTunnelPass(PassRecorder& recorder, const FrameParams& params) {
    m_descriptorAllocator[params.frameIndex].resetPools();

    m_tunnelDescSet[params.frameIndex] = m_descriptorAllocator[params.frameIndex].allocate(m_descriptorLayout);

    DescriptorWriter writer;
    writer.writeBuffer(0, m_uboBuffer[params.frameIndex].buffer, sizeof(FrameUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.writeImage(1, m_noiseTexture.imageView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.writeImage(2, m_normalTexture.imageView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
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
    writer.writeImage(2, m_normalTexture.imageView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.updateSet(params.device, m_crystalDescSet[params.frameIndex]);

    float smoothSeconds = params.time.minute * 60.0f + params.time.secondsInMinute;
    glm::vec3 globalPrismColor = CrystalMath::lerpPrismColor(smoothSeconds);

    float groupRot = (params.time.secondsInMinute / 60.0f) * CrystalMath::TAU;
    int highlightIndex = CrystalMath::getHighlightedRod(params.time.hour);

    float w = static_cast<float>(params.extent.width);
    float h = static_cast<float>(params.extent.height);

    enum class PassMode { All, SelectedOnly };

    auto drawRods = [&](VkPipeline pipeline, glm::vec4 color, float alpha,
                        PassMode mode, bool applyYScale) {
        recorder.bindPipeline(pipeline);
        recorder.bindDescriptorSet(m_pipelineLayout, 0, m_crystalDescSet[params.frameIndex]);
        recorder.bindVertexBuffer(m_rodVertexBuffer.buffer);

        for (int i = 0; i < CrystalMath::ROD_COUNT; i++) {
            bool isSelected = m_rodState[i].selected;
            if (mode == PassMode::SelectedOnly && !isSelected) continue;

            float yScale = applyYScale ? m_rodState[i].yScale : 1.0f;

            glm::mat4 rodMatrix = CrystalMath::buildRodMatrix(i, groupRot, highlightIndex);
            glm::mat4 axialSpin = CrystalMath::buildAxialSpin(i, params.totalTime);
            glm::mat4 model = CrystalMath::buildFullRodModel(rodMatrix, axialSpin, yScale);

            CrystalPushConstants pc{};
            pc.model = model;
            pc.rodColor = color;
            pc.screenParams = glm::vec4(w, h, params.totalTime, alpha);

            recorder.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
            recorder.draw(m_rodVertexCount);
        }
    };

    glm::vec4 glassColor(globalPrismColor * 0.6f, 1.0f);
    glm::vec4 specColor(globalPrismColor * 1.0f, 1.0f);
    glm::vec4 fillColor(globalPrismColor * 2.5f + glm::vec3(0.45f), 1.0f);

    drawRods(m_glassPipeline,    glassColor, 0.65f, PassMode::All,          false);
    drawRods(m_specularPipeline, specColor,  0.30f, PassMode::All,          false);
    drawRods(m_specularPipeline, fillColor,  0.85f, PassMode::SelectedOnly, true);
}

void RenderOrchestrator::destroy(VkDevice device, ResourceManager& resources) {
    resources.destroyBuffer(m_rodVertexBuffer);
    resources.destroyBuffer(m_tunnelVertexBuffer);
    resources.destroyImage(m_noiseTexture);
    resources.destroyImage(m_normalTexture);
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
