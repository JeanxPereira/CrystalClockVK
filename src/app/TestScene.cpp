#include "app/TestScene.hpp"
#include "renderer/RenderTargets.hpp"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

TestScene::TestScene(RenderOrchestrator& orchestrator)
    : m_orchestrator(orchestrator)
{
}

void TestScene::update(const FrameParams& params, float dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            float sens = 0.008f;
            glm::quat yaw = glm::angleAxis(io.MouseDelta.x * sens, glm::vec3(0, 1, 0));
            glm::quat pitch = glm::angleAxis(io.MouseDelta.y * sens, glm::vec3(1, 0, 0));
            m_cubeRot = glm::normalize(pitch * yaw * m_cubeRot);
        }
        if (io.MouseWheel != 0.0f) {
            m_cubeScale = std::clamp(m_cubeScale * std::exp(io.MouseWheel * 0.1f), 0.1f, 5.0f);
        }
    }
    if (m_autoRotate) {
        m_cubeRot = glm::normalize(glm::angleAxis(dt * 0.5f, glm::vec3(0, 1, 0)) * m_cubeRot);
    }
    m_params.cubeModel = glm::mat4_cast(m_cubeRot) *
        glm::scale(glm::mat4(1.0f), glm::vec3(m_cubeScale));
}

void TestScene::record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkExtent2D ext = params.extent;

    recorder.runPass({"Test Background", {0.2f, 0.2f, 0.6f}, &targets.tunnel, &targets.depth, {}, &clear, ext}, [&] {
        m_orchestrator.recordTestBackgroundPass(recorder, params, m_params);
    });

    recorder.copyImage(targets.tunnel, targets.mainColor, ext);

    recorder.runPass({"Test Cube", {0.2f, 0.4f, 1.0f}, &targets.mainColor, &targets.depth,
                      {&targets.tunnel}, nullptr, ext}, [&] {
        m_orchestrator.recordTestCubePass(recorder, params, m_params);
    });
}

void TestScene::drawUI() {
    ImGui::SetNextWindowPos(ImVec2(10, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Refraction Test Scene");
    ImGui::TextDisabled("LMB drag: rotate cube | Wheel: scale");

    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* bgModes[] = {"Stripes (Vertical)", "Stripes (Horizontal)", "Checkerboard", "Grid"};
        ImGui::Combo("Pattern", &m_params.bgMode, bgModes, 4);
        ImGui::SliderFloat("Scale", &m_params.bgScale, 1.0f, 64.0f);
        ImGui::SliderFloat("Scroll Speed", &m_params.bgScrollSpeed, 0.0f, 2.0f);
        ImGui::ColorEdit3("Color 1", &m_params.bgColor1.x);
        ImGui::ColorEdit3("Color 2", &m_params.bgColor2.x);
    }

    if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Eta (IOR ratio)", &m_params.eta, 0.0f, 1.5f);
        ImGui::SliderFloat("Offset Scale", &m_params.refractScale, 0.0f, 10.0f);
        ImGui::SliderFloat("Emissive Boost", &m_params.refractBoost, 0.0f, 8.0f);
        ImGui::SliderFloat("Rim Strength", &m_params.rimStrength, 0.0f, 4.0f);
        ImGui::SliderFloat("Emissive Base", &m_params.emissiveBase, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Composition", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* compModes[] = {"Front (opaque)", "Back (scene mix)"};
        ImGui::Combo("Mode", &m_params.composition, compModes, 2);
        ImGui::SliderFloat("Diffuse Mix", &m_params.diffuseMix, 0.0f, 1.0f);
        ImGui::SliderFloat("Reflect Strength", &m_params.reflectStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("Fade Alpha", &m_params.fadeAlpha, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Tint", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Animate", &m_params.animateTint);
        if (m_params.animateTint)
            ImGui::SliderFloat("Period (s)", &m_params.colorPeriod, 1.0f, 60.0f);
        else
            ImGui::SliderFloat("Lerp", &m_params.tintLerp, 0.0f, 1.0f);
        ImGui::ColorEdit3("Tint 1", &m_params.tint1.x);
        ImGui::ColorEdit3("Tint 2", &m_params.tint2.x);
    }

    if (ImGui::CollapsingHeader("Cube", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Auto Rotate", &m_autoRotate);
        ImGui::SliderFloat("Size", &m_cubeScale, 0.1f, 5.0f);
        if (ImGui::Button("Reset Rotation")) {
            m_cubeRot = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            m_cubeScale = 1.0f;
        }
    }
    ImGui::End();
}
