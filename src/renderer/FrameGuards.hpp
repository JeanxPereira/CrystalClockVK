#pragma once

#include "renderer/PassRecorder.hpp"
#include "renderer/UIRenderer.hpp"
#include <imgui.h>

class UIFrameGuard {
public:
    explicit UIFrameGuard(UIRenderer& ui) : m_ui(ui) { m_ui.beginFrame(); }
    void render(VkCommandBuffer cmd, VkImageView view, VkExtent2D extent) {
        m_ui.render(cmd, view, extent);
        m_rendered = true;
    }
    ~UIFrameGuard() { if (!m_rendered) ImGui::EndFrame(); }
private:
    UIRenderer& m_ui;
    bool m_rendered{false};
};

class DebugLabelGuard {
public:
    DebugLabelGuard(PassRecorder& r, const char* name, float cr, float cg, float cb)
        : m_r(r) { m_r.beginDebugLabel(name, cr, cg, cb); }
    ~DebugLabelGuard() { m_r.endDebugLabel(); }
private:
    PassRecorder& m_r;
};
