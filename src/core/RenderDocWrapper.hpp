#pragma once

#include <string>

class RenderDocWrapper {
public:
    RenderDocWrapper();
    ~RenderDocWrapper();

    bool init();
    void triggerCapture();
    bool isLoaded() const { return m_api != nullptr; }

private:
    void* m_api = nullptr;
    void* m_module = nullptr;
};
