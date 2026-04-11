#pragma once

#include <SDL3/SDL.h>

class WindowContext {
public:
    WindowContext();
    ~WindowContext();

    WindowContext(const WindowContext&) = delete;
    WindowContext& operator=(const WindowContext&) = delete;

    void pollEvents();
    bool shouldClose() const;
    SDL_Window* getHandle() const;

private:
    SDL_Window* m_window{nullptr};
    bool m_shouldClose{false};
};
