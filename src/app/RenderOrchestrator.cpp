#include "RenderOrchestrator.hpp"

void RenderOrchestrator::init(const VulkanContext&, const SwapchainManager&, ResourceManager&) {}

void RenderOrchestrator::recordFrame(PassRecorder&, const FrameParams&) {}

void RenderOrchestrator::updateUBO(const FrameParams&) {}

void RenderOrchestrator::destroy(VkDevice, ResourceManager&) {}
