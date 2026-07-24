#include "app/ClockScene.hpp"
#include "app/TimeSync.hpp"
#include "app/CrystalMath.hpp"
#include "gs/GsConstants.hpp"
#include "renderer/RenderTargets.hpp"
#include <imgui.h>

ClockScene::ClockScene(RenderOrchestrator& orchestrator)
    : m_orchestrator(orchestrator)
{
}

void ClockScene::update(const FrameParams& params, float dt) {
}

void ClockScene::record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkExtent2D ext = params.extent;

    recorder.runPass({"Tunnel Background", {0.2f, 0.2f, 0.6f},
                      &targets.tunnel, &targets.depth, {}, &clear, ext}, [&] {
        m_orchestrator.recordTunnelPass(recorder, params);
    });

    recorder.copyImage(targets.tunnel, targets.mainColor, ext);

    recorder.runPass({"Crystal Clock (Pass 1)", {0.2f, 0.4f, 1.0f},
                      &targets.mainColor, &targets.depth,
                      {&targets.tunnel}, nullptr, ext}, [&] {
        m_orchestrator.recordCrystalPasses(recorder, params);
    });

    recorder.copyImage(targets.mainColor, targets.tunnel, ext);
    recorder.runPass({"Crystal Clock (Pass 2: Inter-Rod)", {0.4f, 0.6f, 1.0f},
                      &targets.mainColor, &targets.depth, {&targets.tunnel}, nullptr, ext}, [&] {
        m_orchestrator.recordCrystalPasses(recorder, params);
    });
}

void ClockScene::drawUI() {
    TimeInfo timeInfo = TimeSync::getCurrentTime();
    int hlRod = CrystalMath::getHighlightedRod(timeInfo.hour);
    int hourCounter = static_cast<int>(timeInfo.minute * 60 + timeInfo.secondsInMinute);
    ImGuiIO& io = ImGui::GetIO();
    float aspect = io.DisplaySize.y > 0.0f ? io.DisplaySize.x / io.DisplaySize.y : 1.0f;
    bool isWide = aspect > 1.5f;
    int screenRatio = isWide ? GsConstants::SCREEN_RATIO_16_9 : GsConstants::SCREEN_RATIO_4_3;
    float fillAmt = CrystalMath::computeRodScale(hlRod, 1.0f, isWide, screenRatio, true, hourCounter);

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("CrystalClock Debug");

    ImGui::Text("Time: %02d:%02d:%02d.%03d", timeInfo.hour, timeInfo.minute, timeInfo.second, timeInfo.millisecond);
    ImGui::Text("Highlighted Rod: %d", hlRod);
    ImGui::Text("Hour Scale Slide: %.3f", fillAmt);
    ImGui::Text("Sec in Min: %.2f", timeInfo.secondsInMinute);
    ImGui::Separator();
    ImGui::Text("Tunnel(1) + Glass(12) + Spec(12) + Fill(1)");
    ImGui::Text("Draw Calls: %d", 1 + 12 + 12 + 1);

    ImGui::End();
}
