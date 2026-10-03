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
        COMMAND ${GLSLC} -o ${SHADER_SPV} ${SHADER_SRC}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader: ${SHADER_NAME} → ${SHADER_NAME}.spv"
        VERBATIM
    )
    list(APPEND SHADER_SPIRV_OUTPUTS ${SHADER_SPV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

find_package(Vulkan REQUIRED)
target_link_libraries(ImGui PRIVATE Vulkan::Vulkan SDL3::SDL3)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(json)

add_library(ClockVma STATIC src/core/VmaUsage.cpp)
target_link_libraries(ClockVma PUBLIC VulkanMemoryAllocator Vulkan::Vulkan)

add_library(ClockParity STATIC
    src/core/HeadlessContext.cpp
    src/parity/GsParityRenderer.cpp
    src/parity/Fixture.cpp
    src/parity/StbImage.cpp
    src/parity/Compare.cpp
)
add_dependencies(ClockParity Shaders)
target_include_directories(ClockParity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
target_link_libraries(ClockParity PUBLIC ClockVma nlohmann_json::nlohmann_json vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan)

set(CLOCK_FIXTURE ${CMAKE_SOURCE_DIR}/References/fixtures/hddosd-110U-whole3-clock/f0 CACHE PATH "Parity fixture")

enable_testing()

add_executable(HeadlessTest tests/HeadlessTest.cpp)
target_link_libraries(HeadlessTest PRIVATE ClockParity)
add_test(NAME HeadlessTest COMMAND HeadlessTest)

add_executable(RendererTest tests/RendererTest.cpp)
target_link_libraries(RendererTest PRIVATE ClockParity)
add_test(NAME RendererTest COMMAND RendererTest ${CMAKE_SOURCE_DIR}/bin/shaders)

add_executable(FixtureTest tests/FixtureTest.cpp)
target_link_libraries(FixtureTest PRIVATE ClockParity)
set(PARITY_ACTIVE "")
if(EXISTS ${CLOCK_FIXTURE}/frame.json)
    add_test(NAME FixtureTest COMMAND FixtureTest ${CLOCK_FIXTURE})
    list(APPEND PARITY_ACTIVE FixtureTest)
endif()

add_executable(CompareTest tests/CompareTest.cpp)
target_link_libraries(CompareTest PRIVATE ClockParity)
add_test(NAME CompareTest COMMAND CompareTest)

add_executable(ParityTool tools/ParityTool/main.cpp)
target_link_libraries(ParityTool PRIVATE ClockParity)

get_filename_component(FIXTURE_ROOT ${CLOCK_FIXTURE}/../.. ABSOLUTE)
function(add_parity_test NAME CAPTURE)
    set(FIXTURE ${FIXTURE_ROOT}/hddosd-110U-${CAPTURE}/f0)
    if(EXISTS ${FIXTURE}/frame.json)
        set(PARITY_ACTIVE ${PARITY_ACTIVE} ${NAME} PARENT_SCOPE)
        add_test(NAME ${NAME} COMMAND ParityTool ${FIXTURE} ${CMAKE_BINARY_DIR}/parity-report/${CAPTURE} ${CMAKE_SOURCE_DIR}/bin/shaders ${CMAKE_SOURCE_DIR}/tools/ParityTool/budgets/hddosd-110U-${CAPTURE}.json)
    endif()
endfunction()
add_parity_test(ParityClock whole3-clock)
add_parity_test(ParityConfig whole3-config)
add_parity_test(ParityToClock whole2-to-clock)
if(PARITY_ACTIVE)
    string(REPLACE ";" " " PARITY_ACTIVE_TEXT "${PARITY_ACTIVE}")
    message(STATUS "parity gates active: ${PARITY_ACTIVE_TEXT}")
else()
    message(STATUS "parity gates inactive: no fixture at ${CLOCK_FIXTURE}")
endif()

add_library(ClockRender STATIC src/render/Device.cpp)
target_include_directories(ClockRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockRender PUBLIC ClockVma vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan SDL3::SDL3)

add_executable(DeviceTest tests/render/DeviceTest.cpp)
target_link_libraries(DeviceTest PRIVATE ClockRender)
add_test(NAME DeviceTest COMMAND DeviceTest)

add_executable(CrystalClock src/app/Main.cpp)
target_link_libraries(CrystalClock PRIVATE ClockRender)
