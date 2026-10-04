#include "app/DebugPanel.hpp"

#include <imgui.h>

namespace app {

void drawPanel(PanelState& state, const PanelInfo& info) {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Crystal Clock", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s", info.clock.c_str());
    ImGui::Text("output %ux%u, %.1f fps, logic frame %llu", info.outputWidth, info.outputHeight, info.framesPerSecond, static_cast<unsigned long long>(info.logicFrames));
    ImGui::Text("validation errors: %u", info.validationErrors);
    ImGui::Separator();

    static const char* resolutions[] = {"640x448 (native)", "window", "x2", "x4"};
    int resolution = static_cast<int>(state.resolution);
    if (ImGui::Combo("resolution", &resolution, resolutions, 4)) state.resolution = static_cast<Resolution>(resolution);
    static const uint32_t counts[] = {1, 2, 4, 8};
    static const char* msaa[] = {"off", "2x", "4x", "8x"};
    int chosen = 0;
    for (int i = 0; i < 4; ++i)
        if (counts[i] == state.samples) chosen = i;
    if (ImGui::BeginCombo("MSAA", msaa[chosen])) {
        for (int i = 0; i < 4; ++i) {
            if (!(info.sampleCounts & counts[i])) continue;
            if (ImGui::Selectable(msaa[i], i == chosen)) state.samples = counts[i];
        }
        ImGui::EndCombo();
    }
    static const char* targets[] = {"display", "refraction source", "work"};
    int shown = static_cast<int>(state.shown);
    if (ImGui::Combo("show", &shown, targets, 3)) state.shown = static_cast<scene::TargetName>(shown);
    ImGui::Checkbox("4:3 picture", &state.tvAspect);
    ImGui::Separator();

    ImGui::Checkbox("pause", &state.paused);
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.paused);
    if (ImGui::Button("step")) state.step = true;
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt3("h m s", state.time);
    if (ImGui::Button("set time")) state.setTime = true;
    ImGui::SameLine();
    if (ImGui::Button("local time")) state.localTime = true;
    ImGui::Separator();

    if (ImGui::Button("screenshot")) state.screenshot = true;
    if (!info.lastScreenshot.empty()) ImGui::TextWrapped("%s", info.lastScreenshot.c_str());
    ImGui::End();
}

}  // namespace app
