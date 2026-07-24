#pragma once

#include "app/IScene.hpp"
#include "app/RenderOrchestrator.hpp"

class ClockScene : public IScene {
public:
    explicit ClockScene(RenderOrchestrator& orchestrator);

    void update(const FrameParams& params, float dt) override;
    void record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) override;
    void drawUI() override;

private:
    RenderOrchestrator& m_orchestrator;
};
