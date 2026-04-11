#include "RenderDocWrapper.hpp"
#include "../../3rdparty/renderdoc_app.h"
#include <iostream>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

RenderDocWrapper::RenderDocWrapper() = default;

RenderDocWrapper::~RenderDocWrapper() {
    if (m_module) {
#if defined(_WIN32)
        // FreeLibrary((HMODULE)m_module);
#else
        // Injected library stays in memory usually. Space is negligible. 
        // dlclose(m_module);
#endif
    }
}

bool RenderDocWrapper::init() {
    pRENDERDOC_GetAPI RENDERDOC_GetAPI = nullptr;

#if defined(_WIN32)
    m_module = GetModuleHandleA("renderdoc.dll");
    if (!m_module) return false;
    RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)m_module, "RENDERDOC_GetAPI");
#else
    // On macOS / Linux
    const char* paths[] = {
        "librenderdoc.dylib",
        "librenderdoc.so",
        "/Applications/RenderDoc.app/Contents/MacOS/librenderdoc.dylib",
        "/usr/local/lib/librenderdoc.dylib",
        "/opt/homebrew/lib/librenderdoc.dylib"
    };
    
    for (const char* path : paths) {
        m_module = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
        if (!m_module) {
            m_module = dlopen(path, RTLD_NOW); // try to explicitly load
        }
        if (m_module) break;
    }
    
    if (!m_module) return false;

    RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(m_module, "RENDERDOC_GetAPI");
#endif

    if (!RENDERDOC_GetAPI) {
        std::cerr << "[RenderDoc] Module found, but GetAPI address failed." << std::endl;
        return false;
    }

    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&m_api);
    if (ret != 1) {
        std::cerr << "[RenderDoc] API Version initialization failed." << std::endl;
        return false;
    }

    std::cout << "[RenderDoc] Wrapper initialized successfully for runtime captures." << std::endl;
    return true;
}

void RenderDocWrapper::triggerCapture() {
    if (m_api) {
        auto* api = static_cast<RENDERDOC_API_1_1_2*>(m_api);
        api->TriggerCapture();
    }
}
