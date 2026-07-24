#pragma once

#include "app/IScene.hpp"
#include "app/RenderOrchestrator.hpp"
#include <glm/gtc/quaternion.hpp>

class TestScene : public IScene {
public:
    explicit TestScene(RenderOrchestrator& orchestrator);

    void update(const FrameParams& params, float dt) override;
    void record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) override;
    void drawUI() override;

    TestSceneParams& params() { return m_params; }

private:
    RenderOrchestrator& m_orchestrator;
    TestSceneParams m_params;
    glm::quat m_cubeRot{1.0f, 0.0f, 0.0f, 0.0f};
    float m_cubeScale{1.0f};
    bool m_autoRotate{false};
};
