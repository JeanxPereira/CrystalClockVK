cmake_minimum_required(VERSION 3.30)
project(CrystalClockVK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(vma)

FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG v1.4.341
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(vk-bootstrap)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(glm)

# ──────────────────────────────────────────────────────────────────────────────
# IMGUI (SUBMODULE: 3rdparty/imgui)
# ──────────────────────────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui)
add_library(ImGui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(ImGui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ──────────────────────────────────────────────────────────────────────────────
# SHADER COMPILATION (glslc → SPIR-V)
# ──────────────────────────────────────────────────────────────────────────────
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Ensure Vulkan SDK is in PATH or VULKAN_SDK is set.")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_SOURCE_DIR}/bin/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

# Glob all shader sources
file(GLOB SHADER_SOURCES
    ${SHADER_SRC_DIR}/*.vert
    ${SHADER_SRC_DIR}/*.frag
    ${SHADER_SRC_DIR}/*.comp
)

set(SHADER_SPIRV_OUTPUTS "")
foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SHADER_SPV ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} --target-env=vulkan1.3 -o ${SHADER_SPV} ${SHADER_SRC}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader: ${SHADER_NAME} → ${SHADER_NAME}.spv"
        VERBATIM
    )
    list(APPEND SHADER_SPIRV_OUTPUTS ${SHADER_SPV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

# ──────────────────────────────────────────────────────────────────────────────
# APPLICATION
# ──────────────────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/WindowContext.cpp
    src/core/VulkanContext.cpp
    src/core/RenderDocWrapper.cpp
    src/renderer/SwapchainManager.cpp
    src/renderer/ShaderLoader.cpp
    src/renderer/PipelineBuilder.cpp
    src/renderer/ResourceManager.cpp
    src/renderer/PassRecorder.cpp
    src/renderer/DescriptorAllocator.cpp
    src/renderer/UIRenderer.cpp
    src/app/RenderOrchestrator.cpp
    src/app/TimeSync.cpp
    src/app/GsScene.cpp
    src/app/GsRenderer.cpp
    src/clock/ClockRenderer.cpp
)

# Output binary to bin/ at project root (not inside build/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

add_executable(${PROJECT_NAME} ${SOURCES})
add_dependencies(${PROJECT_NAME} Shaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    vk-bootstrap
    VulkanMemoryAllocator
    glm::glm
    ImGui
    gsdump_lib
    gsvk
    gs
    clock
)

find_package(Vulkan REQUIRED)
if (Vulkan_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
    target_link_libraries(ImGui PRIVATE Vulkan::Vulkan SDL3::SDL3)
endif()

# Copy SDL3.dll next to the output binaries so the executables run from bin/.
if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            ${CMAKE_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:SDL3::SDL3>
        COMMENT "Copying SDL3.dll to bin/"
    )
endif()

# ──────────────────────────────────────────────────────────────────────────────
# gsdump — GS-dump parser (Phase 1 W1). Pure: no Vulkan, no SDL. Built as a lib
# (shared by the standalone tool and the app) plus the CLI front-end.
# ──────────────────────────────────────────────────────────────────────────────
add_library(gsdump_lib STATIC tools/gsdump/GsDumpParser.cpp)
target_include_directories(gsdump_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/gsdump
)

add_executable(gsdump tools/gsdump/main.cpp)
target_link_libraries(gsdump PRIVATE gsdump_lib)
set_target_properties(gsdump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# gs — pure PS2 decode lib (swizzle, texture, VRAM). No Vulkan. Used by app + tools.
add_library(gs STATIC
    src/gs/SwizzleEngine.cpp
    src/gs/TextureDecoder.cpp
    src/gs/VramBuffer.cpp
)
target_include_directories(gs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ee — mini PS2 EE interpreter (memory, translation, MMIO, store log). No Vulkan.
add_library(ee STATIC
    src/ee/EeMemory.cpp
    src/ee/EeInterpreter.cpp
)
target_include_directories(ee PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ee PUBLIC cxx_std_23)

# vramdump — deswizzle a region of the .gs VRAM freeze to raw RGBA (W4 texture bring-up).
add_executable(vramdump tools/vramdump/main.cpp)
target_link_libraries(vramdump PRIVATE gsdump_lib gs)
set_target_properties(vramdump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# eerun — discovery CLI: interpret a real EE function from a captured RAM image
# (mini interpreter in src/ee/), reporting MMIO accesses + store extents + call trace.
add_executable(eerun tools/eerun/main.cpp)
target_link_libraries(eerun PRIVATE ee gsdump_lib gs)
set_target_properties(eerun PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

if (APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_METAL_EXT)
elseif (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif (UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_XCB_KHR)
endif()

# ──────────────────────────────────────────────────────────────────────────────
# gsvk — GS->VK translator library (Phase 1 W3). Depends on gs/ structs +
# Vulkan headers (enums/structs only; no device). The bridge layer.
# ──────────────────────────────────────────────────────────────────────────────
add_library(gsvk STATIC
    src/gsvk/GsBlendTranslator.cpp
    src/gsvk/GsDrawRecipe.cpp
)
target_include_directories(gsvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gsvk PUBLIC Vulkan::Vulkan)

# Tests (CTest).
enable_testing()
foreach(t gsvk_blend:GsBlendTest gsvk_recipe:GsRecipeTest)
    string(REPLACE ":" ";" parts ${t})
    list(GET parts 0 tname)
    list(GET parts 1 tsrc)
    add_executable(${tname}_tests tests/${tsrc}.cpp)
    target_link_libraries(${tname}_tests PRIVATE gsvk)
    set_target_properties(${tname}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
    add_test(NAME ${tname} COMMAND ${tname}_tests)
endforeach()

# gs swizzle round-trip (VRAM texture-cache foundation).
add_executable(gs_swizzle_tests tests/SwizzleRoundTripTest.cpp)
target_link_libraries(gs_swizzle_tests PRIVATE gs)
set_target_properties(gs_swizzle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gs_swizzle COMMAND gs_swizzle_tests)

# gs PSMCT32 address code-parity vs PCSX2 GSLocalMemory.
add_executable(gs_swizzle_addr_tests tests/SwizzleAddress32Test.cpp)
target_link_libraries(gs_swizzle_addr_tests PRIVATE gs)
set_target_properties(gs_swizzle_addr_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gs_swizzle_addr COMMAND gs_swizzle_addr_tests)

# gs TEXA expansion code-parity vs PCSX2 GSLocalMemory.
add_executable(gs_texa_tests tests/TexaExpandTest.cpp)
target_link_libraries(gs_texa_tests PRIVATE gs)
set_target_properties(gs_texa_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gs_texa COMMAND gs_texa_tests)

# gs PSMCT24 texture-read applies TEXA (ReadTexel24), not a constant 0x80.
add_executable(gs_texa_deswizzle_tests tests/DeswizzleTexa24Test.cpp)
target_link_libraries(gs_texa_deswizzle_tests PRIVATE gs)
set_target_properties(gs_texa_deswizzle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gs_texa_deswizzle COMMAND gs_texa_deswizzle_tests)

# gs PSMT4 (4-bit indexed) swizzle: address invariants + round-trip.
add_executable(gs_swizzle_addr4_tests tests/SwizzleAddress4Test.cpp)
target_link_libraries(gs_swizzle_addr4_tests PRIVATE gs)
set_target_properties(gs_swizzle_addr4_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gs_swizzle_addr4 COMMAND gs_swizzle_addr4_tests)

# gsdump parser: RGBAQ.Q capture (STQ perspective divisor).
add_executable(gsdump_stq_tests tests/ParserStqQTest.cpp)
target_link_libraries(gsdump_stq_tests PRIVATE gsdump_lib)
set_target_properties(gsdump_stq_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME gsdump_stq COMMAND gsdump_stq_tests)

# ee_memory: EE address translation, RAM image load, MMIO dispatch, store log.
add_executable(ee_memory_tests tests/EeMemoryTest.cpp)
target_link_libraries(ee_memory_tests PRIVATE ee)
set_target_properties(ee_memory_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME ee_memory COMMAND ee_memory_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ee_interpreter: R5900 integer subset dispatch, delay slots, fail-fast on unknown opcodes.
add_executable(ee_interpreter_tests tests/EeInterpreterTest.cpp)
target_link_libraries(ee_interpreter_tests PRIVATE ee)
set_target_properties(ee_interpreter_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME ee_interpreter COMMAND ee_interpreter_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ee_realcode (Gate A): interpret real sceVu0 functions from the captured RAM
# image, verified against C++ oracles. Skips if the image isn't present.
add_executable(ee_realcode_tests tests/EeRealCodeTest.cpp)
target_link_libraries(ee_realcode_tests PRIVATE ee)
set_target_properties(ee_realcode_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME ee_realcode COMMAND ee_realcode_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ──────────────────────────────────────────────────────────────────────────────
# clock — pure procedural clock logic (geometry + projection math). NO Vulkan.
# ──────────────────────────────────────────────────────────────────────────────
add_library(clock STATIC
    src/clock/Projection.cpp
    src/clock/RodField.cpp
    src/clock/ClockState.cpp
    src/clock/ClockOrb.cpp
)
target_include_directories(clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clock PUBLIC glm::glm)
target_compile_definitions(clock PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

# clock_dump — headless: render the rod field flat to an offscreen image and
# write RGBA8 (pixel-diff harness; reuses ResourceManager::downloadImage).
add_executable(clock_dump
    tools/clockdump/main.cpp
    src/clock/ClockRenderer.cpp
    src/app/TimeSync.cpp
    src/core/VulkanContext.cpp
    src/core/WindowContext.cpp
    src/renderer/ResourceManager.cpp
    src/renderer/PassRecorder.cpp
    src/renderer/PipelineBuilder.cpp
    src/renderer/ShaderLoader.cpp
)
add_dependencies(clock_dump Shaders)
target_include_directories(clock_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clock_dump PRIVATE clock SDL3::SDL3 vk-bootstrap VulkanMemoryAllocator glm::glm Vulkan::Vulkan ImGui)
target_compile_definitions(clock_dump PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
if (WIN32)
    target_compile_definitions(clock_dump PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
set_target_properties(clock_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# clock pure-logic tests (math/geometry/projection — Vulkan-free).
foreach(t clock_fixed124:Fixed124Test clock_rotation:RotationBasisTest clock_projection:ProjectionFitTest clock_rodfield:RodFieldTest clock_state:ClockStateTest clock_orb:ClockOrbTest)
    string(REPLACE ":" ";" parts ${t})
    list(GET parts 0 tname)
    list(GET parts 1 tsrc)
    add_executable(${tname}_tests tests/${tsrc}.cpp)
    target_link_libraries(${tname}_tests PRIVATE clock)
    set_target_properties(${tname}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
    add_test(NAME ${tname} COMMAND ${tname}_tests)
endforeach()
