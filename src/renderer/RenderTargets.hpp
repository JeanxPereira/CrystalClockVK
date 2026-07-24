#pragma once

#include "renderer/ResourceManager.hpp"

struct RenderTargets {
    AllocatedImage depth;
    AllocatedImage tunnel;
    AllocatedImage mainColor;

    void create(ResourceManager& res, VkExtent2D extent, VkFormat colorFormat) {
        depth = res.createImage(extent, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        tunnel = res.createImage(extent, colorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        mainColor = res.createImage(extent, colorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    }

    void destroy(ResourceManager& res) {
        res.destroyImage(depth);
        res.destroyImage(tunnel);
        res.destroyImage(mainColor);
    }
};
