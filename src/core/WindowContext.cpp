#include "WindowContext.hpp"
#include <stdexcept>

WindowContext::WindowContext() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }

    m_window = SDL_CreateWindow("CrystalClockVK", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
}

WindowContext::~WindowContext() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

#include <imgui_impl_sdl3.h>

void WindowContext::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            m_shouldClose = true;
        }
    }
}

bool WindowContext::shouldClose() const {
    return m_shouldClose;
}

SDL_Window* WindowContext::getHandle() const {
    return m_window;
}
