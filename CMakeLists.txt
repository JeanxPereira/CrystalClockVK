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
    src/parity/Compare.cpp
    src/parity/FromScene.cpp
)
add_library(ClockStb STATIC src/parity/StbImage.cpp)
target_include_directories(ClockStb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
target_link_libraries(ClockParity PUBLIC ClockStb)
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

add_library(ClockScene STATIC src/scene/Arithmetic.cpp src/scene/ClockState.cpp src/scene/Camera.cpp)
target_include_directories(ClockScene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(ClockScene PRIVATE /fp:strict)
else()
    target_compile_options(ClockScene PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The scene.json input readers: the tests' fixtures and the app's start file.
add_library(SceneInputs STATIC src/scene/SceneInputs.cpp)
target_include_directories(SceneInputs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SceneInputs PUBLIC nlohmann_json::nlohmann_json)
add_library(SceneTestSupport STATIC tests/scene/SceneFixture.cpp)
target_link_libraries(SceneTestSupport PUBLIC SceneInputs ClockScene nlohmann_json::nlohmann_json)

get_filename_component(SCENE_FIXTURE_DEFAULT ${CLOCK_FIXTURE}/../scene.json ABSOLUTE)
set(CLOCK_SCENE ${SCENE_FIXTURE_DEFAULT} CACHE FILEPATH "The scene fixture of whole3-clock (tools/scene/export_fixture.mjs)")
set(SCENE_FIXTURE ${CLOCK_SCENE})
# The font file and the program the text reads (facts/text.md), in the main checkout's References/dumps.
get_filename_component(CLOCK_DUMPS_DEFAULT ${CLOCK_FIXTURE}/../../../dumps/hddosd-host ABSOLUTE)
set(CLOCK_DUMPS ${CLOCK_DUMPS_DEFAULT} CACHE PATH "HDD OSD 1.10U's files: FNTOSD and hddosd.elf")
add_executable(ArithmeticTest tests/scene/ArithmeticTest.cpp)
target_link_libraries(ArithmeticTest PRIVATE SceneTestSupport)
add_test(NAME ArithmeticTest COMMAND ArithmeticTest)
if(EXISTS ${SCENE_FIXTURE})
    add_test(NAME ArithmeticSceneTest COMMAND ArithmeticTest ${SCENE_FIXTURE})
endif()

target_sources(ClockScene PRIVATE src/scene/FrameHead.cpp)
file(GLOB HEAD_SCENES CONFIGURE_DEPENDS ${CLOCK_FIXTURE}/../../*/scene.json ${CLOCK_FIXTURE}/../../synthetic-head/scene-*.json)
if(HEAD_SCENES)
    add_executable(HeadTest tests/scene/HeadTest.cpp)
    target_link_libraries(HeadTest PRIVATE SceneTestSupport)
    add_test(NAME HeadTest COMMAND HeadTest ${HEAD_SCENES})
endif()

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

add_library(ClockRender STATIC src/render/Device.cpp src/render/NativeRenderer.cpp)
add_dependencies(ClockRender Shaders)
target_include_directories(ClockRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockRender PUBLIC ClockVma ClockStb vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan SDL3::SDL3 ClockSceneNative)

add_executable(DeviceTest tests/render/DeviceTest.cpp)
target_link_libraries(DeviceTest PRIVATE ClockRender)
add_test(NAME DeviceTest COMMAND DeviceTest)

target_sources(ClockScene PRIVATE src/scene/Orbs.cpp)
add_executable(OrbsTest tests/scene/OrbsTest.cpp)
target_link_libraries(OrbsTest PRIVATE SceneTestSupport)
if(EXISTS ${SCENE_FIXTURE})
    add_test(NAME OrbsTest COMMAND OrbsTest ${SCENE_FIXTURE})
endif()
target_sources(ClockScene PRIVATE src/scene/Rods.cpp)
target_link_libraries(ClockScene PRIVATE nlohmann_json::nlohmann_json)
add_executable(RodsTest tests/scene/RodsTest.cpp)
target_link_libraries(RodsTest PRIVATE SceneTestSupport)
if(EXISTS ${SCENE_FIXTURE})
    add_test(NAME RodsTest COMMAND RodsTest ${SCENE_FIXTURE} ${CMAKE_CURRENT_SOURCE_DIR}/facts/data/rod-mesh.json)
endif()
add_executable(StateTest tests/scene/StateTest.cpp)
target_link_libraries(StateTest PRIVATE SceneTestSupport)
if(EXISTS ${SCENE_FIXTURE})
    add_test(NAME StateTest COMMAND StateTest ${SCENE_FIXTURE})
endif()
target_sources(ClockScene PRIVATE src/scene/Clock.cpp src/scene/Font.cpp src/scene/ProgramImage.cpp src/scene/Text.cpp)
get_filename_component(SCENE_PASSES ${CLOCK_FIXTURE}/../passes.json ABSOLUTE)
add_executable(TextTest tests/scene/TextTest.cpp)
target_link_libraries(TextTest PRIVATE SceneTestSupport)
if(EXISTS ${CLOCK_DUMPS}/FNTOSD)
    add_test(NAME TextTest COMMAND TextTest ${CLOCK_DUMPS}/FNTOSD ${CLOCK_DUMPS}/hddosd.elf)
endif()
add_executable(ClockParityTest tests/scene/ClockParityTest.cpp)
target_link_libraries(ClockParityTest PRIVATE SceneTestSupport ClockParity)
if(EXISTS ${SCENE_FIXTURE} AND EXISTS ${SCENE_PASSES} AND EXISTS ${CLOCK_FIXTURE}/frame.json AND EXISTS ${CLOCK_DUMPS}/FNTOSD)
    set(SCENE_PARITY_FIXTURE ${CMAKE_BINARY_DIR}/scene-fixture/whole3-clock)
    add_test(NAME ClockParityTest COMMAND ClockParityTest ${SCENE_FIXTURE} ${CMAKE_SOURCE_DIR}/facts/data/rod-mesh.json ${SCENE_PASSES} ${CLOCK_FIXTURE} ${SCENE_PARITY_FIXTURE}
        ${CLOCK_DUMPS}/FNTOSD ${CLOCK_DUMPS}/hddosd.elf)
    set_tests_properties(ClockParityTest PROPERTIES FIXTURES_SETUP ClockSceneFixture)
    add_test(NAME ParityClockScene COMMAND ParityTool ${SCENE_PARITY_FIXTURE} ${CMAKE_BINARY_DIR}/parity-report/whole3-clock-scene ${CMAKE_SOURCE_DIR}/bin/shaders ${CMAKE_SOURCE_DIR}/tools/ParityTool/budgets/hddosd-110U-whole3-clock.json)
    set_tests_properties(ParityClockScene PROPERTIES FIXTURES_REQUIRED ClockSceneFixture)
endif()

# The app's scene: the same sources with NativeArithmetic only. EeArithmetic's functions are defined in
# Arithmetic.cpp alone, which this library leaves out, so any use of them in the app fails to link.
add_library(ClockSceneNative STATIC src/scene/ClockState.cpp src/scene/Camera.cpp src/scene/FrameHead.cpp src/scene/Orbs.cpp src/scene/Rods.cpp src/scene/Clock.cpp
    src/scene/Font.cpp src/scene/ProgramImage.cpp src/scene/Text.cpp)
target_include_directories(ClockSceneNative PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ClockSceneNative PRIVATE SCENE_NATIVE_ONLY)
target_link_libraries(ClockSceneNative PRIVATE nlohmann_json::nlohmann_json)

get_filename_component(CLOCK_REFERENCES_DEFAULT ${CLOCK_FIXTURE}/../../.. ABSOLUTE)
set(CLOCK_REFERENCES ${CLOCK_REFERENCES_DEFAULT} CACHE PATH "References folder with textures/ and fixtures/ (the main checkout's)")
set(CLOCK_TEXTURES ${CLOCK_REFERENCES}/textures)
set(CLOCK_START ${CMAKE_SOURCE_DIR}/resources/clock/start.json)

add_executable(NativeRendererTest tests/render/NativeRendererTest.cpp)
target_link_libraries(NativeRendererTest PRIVATE ClockRender ClockSceneNative SceneInputs)
if(EXISTS ${CLOCK_SCENE} AND EXISTS ${CLOCK_TEXTURES}/tbp-2bc0-64x64.png AND EXISTS ${CLOCK_DUMPS}/FNTOSD)
    add_test(NAME NativeRendererTest COMMAND NativeRendererTest ${CMAKE_SOURCE_DIR}/bin/shaders ${CLOCK_SCENE} ${CMAKE_SOURCE_DIR}/facts/data/rod-mesh.json ${CLOCK_TEXTURES}
        ${CLOCK_DUMPS}/FNTOSD ${CLOCK_DUMPS}/hddosd.elf ${CLOCK_START})
else()
    add_test(NAME NativeRendererTest COMMAND NativeRendererTest ${CMAKE_SOURCE_DIR}/bin/shaders)
endif()

add_executable(CrystalClock src/app/Main.cpp src/app/DebugPanel.cpp src/app/Png.cpp)
target_link_libraries(CrystalClock PRIVATE ClockRender ClockSceneNative SceneInputs ImGui)
target_compile_definitions(CrystalClock PRIVATE
    CLOCK_SHADERS="${CMAKE_SOURCE_DIR}/bin/shaders" CLOCK_TEXTURES="${CLOCK_TEXTURES}" CLOCK_START="${CLOCK_START}"
    CLOCK_MESH="${CMAKE_SOURCE_DIR}/facts/data/rod-mesh.json" CLOCK_SCREENSHOTS="${CMAKE_SOURCE_DIR}/out/screenshots"
    CLOCK_FONT="${CLOCK_DUMPS}/FNTOSD" CLOCK_PROGRAM="${CLOCK_DUMPS}/hddosd.elf")
target_link_libraries(ParityTool PRIVATE ClockRender ClockSceneNative SceneInputs)

# opening: integration
set(OPENING_FIXTURES ${FIXTURE_ROOT} CACHE PATH "The opening fixtures (opening.json, passes.json): the main checkout References/fixtures")
set(OPENING_TEXTURES ${CLOCK_REFERENCES}/textures/opening CACHE PATH "The opening textures extracted from GS memory (git-ignored)")
add_library(OpeningTestSupport STATIC tests/scene/opening/OpeningFixture.cpp)
target_include_directories(OpeningTestSupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/scene/opening)
target_link_libraries(OpeningTestSupport PUBLIC SceneTestSupport)
add_executable(FromSceneOpeningTest tests/scene/opening/FromSceneOpeningTest.cpp)
target_link_libraries(FromSceneOpeningTest PRIVATE OpeningTestSupport ClockParity)
add_test(NAME FromSceneOpeningTest COMMAND FromSceneOpeningTest)
set(OPENING_FULL_FIXTURE ${OPENING_FIXTURES}/hddosd-110U-opening-full/opening.json)
if(EXISTS ${OPENING_FULL_FIXTURE})
    add_test(NAME OpeningFixtureTest COMMAND FromSceneOpeningTest ${OPENING_FULL_FIXTURE})
    list(APPEND OPENING_ACTIVE OpeningFixtureTest)
endif()
if(OPENING_ACTIVE)
    string(REPLACE ";" " " OPENING_ACTIVE_TEXT "${OPENING_ACTIVE}")
    message(STATUS "opening gates active: ${OPENING_ACTIVE_TEXT}")
else()
    message(STATUS "opening gates inactive: no fixture at ${OPENING_FULL_FIXTURE}")
endif()

# opening: lib
set(OPENING_VU0_VECTORS ${CMAKE_CURRENT_SOURCE_DIR}/tests/scene/opening/vu0_vectors.json)
add_test(NAME ArithmeticOpeningTest COMMAND ArithmeticTest --opening ${OPENING_VU0_VECTORS})
add_executable(Vu0Test tests/scene/opening/Vu0Test.cpp)
target_link_libraries(Vu0Test PRIVATE OpeningTestSupport)
set(OPENING_INPUTS_FIXTURE ${OPENING_FIXTURES}/hddosd-110U-opening2-inputs/opening.json)
add_test(NAME Vu0Test COMMAND Vu0Test ${OPENING_VU0_VECTORS} ${OPENING_INPUTS_FIXTURE} ${OPENING_FULL_FIXTURE})

# opening: handoff
add_executable(HandoffTest tests/scene/opening/HandoffTest.cpp)
target_sources(HandoffTest PRIVATE src/scene/opening/Handoff.cpp)
target_link_libraries(HandoffTest PRIVATE OpeningTestSupport)
set(HANDOFF_ARGS)
foreach(CAPTURE opening2-handoff-probes opening3-illegal opening3-disc65-b opening3-disc69 opening3-disc6a opening3-disc6b opening3-disc6c opening3-disc6d opening3-disc6e
        opening3-disc6f opening3-disc70 opening3-disc71 opening3-disc73 opening3-forced opening3-ready-b opening3-ill74-c opening3-ill72-b)
    if(EXISTS ${OPENING_FIXTURES}/hddosd-110U-${CAPTURE}/opening.json)
        list(APPEND HANDOFF_ARGS ${OPENING_FIXTURES}/hddosd-110U-${CAPTURE}/opening.json)
    else()
        list(APPEND HANDOFF_ARGS --missing=${CAPTURE})
    endif()
endforeach()
add_test(NAME HandoffTest COMMAND HandoffTest ${HANDOFF_ARGS})
message(STATUS "opening handoff: HandoffTest on ${HANDOFF_ARGS}")
set(ENTRY_SCENE ${OPENING_FIXTURES}/hddosd-110U-whole-boot-opening/scene.json)
add_executable(ClockEntryTest tests/scene/opening/ClockEntryTest.cpp)
target_link_libraries(ClockEntryTest PRIVATE OpeningTestSupport)
if(EXISTS ${ENTRY_SCENE})
    add_test(NAME ClockEntryTest COMMAND ClockEntryTest ${ENTRY_SCENE} ${CMAKE_CURRENT_SOURCE_DIR}/facts/data/rod-mesh.json)
endif()
add_library(ClockScreen STATIC src/app/ClockScreen.cpp)
target_include_directories(ClockScreen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockScreen PUBLIC ClockSceneNative SceneInputs)
add_executable(ClockScreenTest tests/scene/opening/ClockScreenTest.cpp)
target_link_libraries(ClockScreenTest PRIVATE ClockScreen)
add_test(NAME ClockScreenTest COMMAND ClockScreenTest ${CMAKE_CURRENT_SOURCE_DIR}/resources/clock/start-boot.json ${CMAKE_CURRENT_SOURCE_DIR}/facts/data/rod-mesh.json)

# opening: timeline
add_executable(TimelineTest tests/scene/opening/TimelineTest.cpp src/scene/opening/Timeline.cpp)
target_link_libraries(TimelineTest PRIVATE OpeningTestSupport)
if(EXISTS ${OPENING_FULL_FIXTURE})
    add_test(NAME TimelineTest COMMAND TimelineTest ${OPENING_FIXTURES} ${CLOCK_DUMPS}/hddosd.elf)
endif()

# opening: cubes
add_executable(CubesTest tests/scene/opening/CubesTest.cpp src/scene/opening/Cubes.cpp src/scene/opening/Timeline.cpp)
target_link_libraries(CubesTest PRIVATE OpeningTestSupport)
if(EXISTS ${OPENING_FULL_FIXTURE})
    add_test(NAME CubesTest COMMAND CubesTest ${OPENING_FULL_FIXTURE} ${OPENING_FIXTURES}/hddosd-110U-opening-full/passes.json ${CLOCK_DUMPS}/hddosd.elf)
endif()
