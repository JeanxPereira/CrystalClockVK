#pragma once

struct FrameParams;
class PassRecorder;
struct RenderTargets;

class IScene {
public:
    virtual ~IScene() = default;
    virtual void update(const FrameParams& params, float dt) = 0;
    virtual void record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) = 0;
    virtual void drawUI() = 0;
};
