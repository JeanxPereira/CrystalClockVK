#include "UIRenderer.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

UIRenderer::UIRenderer(const VulkanContext& ctx, SDL_Window* window, VkFormat swapchainFormat)
    : m_ctx(ctx), m_format(swapchainFormat) {

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 100;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.Alpha = 0.92f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.15f, 0.88f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.15f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.22f, 0.80f);

    ImGui_ImplSDL3_InitForVulkan(window);

    VkPipelineRenderingCreateInfoKHR pipelineRendering{};
    pipelineRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineRendering.colorAttachmentCount = 1;
    pipelineRendering.pColorAttachmentFormats = &m_format;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = m_ctx.instance();
    initInfo.PhysicalDevice = m_ctx.physicalDevice();
    initInfo.Device = m_ctx.device();
    initInfo.QueueFamily = m_ctx.graphicsQueueFamily();
    initInfo.Queue = m_ctx.graphicsQueue();
    initInfo.DescriptorPool = m_imguiPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;

    ImGui_ImplVulkan_Init(&initInfo);
}

UIRenderer::~UIRenderer() {
    vkDeviceWaitIdle(m_ctx.device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(m_ctx.device(), m_imguiPool, nullptr);
}

void UIRenderer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void UIRenderer::render(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent) {
    ImGui::Render();

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = targetView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = {{0, 0}, extent};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttach;

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}
