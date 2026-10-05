cmake_minimum_required(VERSION 3.30)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
project(CrystalClockVK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

option(CLOCK_BUILD_TESTS "Build the tests tree (tests/CMakeLists.txt, when present) instead of the app" OFF)
set(CLOCK_DEPS_DIR ${CMAKE_SOURCE_DIR}/build/_deps CACHE PATH "Dependency sources shared by the app and tests build trees")
if(CLOCK_BUILD_TESTS)
    set(CLOCK_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
else()
    set(CLOCK_BIN_DIR ${CMAKE_SOURCE_DIR}/bin)
endif()

find_package(Git REQUIRED)
function(clock_fetch NAME REPOSITORY TAG)
    string(TOUPPER ${NAME} UPPER)
    set(DIR ${CLOCK_DEPS_DIR}/${NAME}-src)
    if(NOT EXISTS ${DIR}/.git)
        file(REMOVE_RECURSE ${DIR})
        execute_process(COMMAND ${GIT_EXECUTABLE} clone --quiet --depth 1 --branch ${TAG} ${REPOSITORY} ${DIR} RESULT_VARIABLE STATUS)
        if(NOT STATUS EQUAL 0)
            file(REMOVE_RECURSE ${DIR})
            message(FATAL_ERROR "clone of ${REPOSITORY} ${TAG} failed")
        endif()
    endif()
    set(FETCHCONTENT_SOURCE_DIR_${UPPER} ${DIR} CACHE PATH "" FORCE)
    FetchContent_Declare(${NAME} GIT_REPOSITORY ${REPOSITORY} GIT_TAG ${TAG})
    FetchContent_MakeAvailable(${NAME})
endfunction()

clock_fetch(SDL3 https://github.com/libsdl-org/SDL.git release-3.4.4)
clock_fetch(vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0)
clock_fetch(vk-bootstrap https://github.com/charles-lunarg/vk-bootstrap.git v1.4.341)
clock_fetch(glm https://github.com/g-truc/glm.git 1.0.1)
clock_fetch(json https://github.com/nlohmann/json.git v3.11.3)

if(NOT CLOCK_BUILD_TESTS)
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
endif()

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Ensure Vulkan SDK is in PATH or VULKAN_SDK is set.")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CLOCK_BIN_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

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
        COMMENT "Compiling shader: ${SHADER_NAME}"
        VERBATIM
    )
    list(APPEND SHADER_SPIRV_OUTPUTS ${SHADER_SPV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

find_package(Vulkan REQUIRED)
if(NOT CLOCK_BUILD_TESTS)
target_link_libraries(ImGui PRIVATE Vulkan::Vulkan SDL3::SDL3)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CLOCK_BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CLOCK_BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CLOCK_BIN_DIR})

add_library(ClockVma STATIC src/core/VmaUsage.cpp)
target_link_libraries(ClockVma PUBLIC VulkanMemoryAllocator Vulkan::Vulkan)

add_library(ClockStb STATIC src/core/StbImage.cpp)
target_include_directories(ClockStb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

add_library(ClockScene STATIC src/scene/Arithmetic.cpp src/scene/ClockState.cpp src/scene/Camera.cpp
    src/scene/FrameHead.cpp src/scene/Orbs.cpp src/scene/Rods.cpp src/scene/Clock.cpp src/scene/Font.cpp src/scene/ProgramImage.cpp src/scene/Text.cpp
    src/scene/ClockDate.cpp src/scene/Menus.cpp src/scene/Cubes.cpp)
target_include_directories(ClockScene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockScene PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(ClockScene PRIVATE /fp:strict)
else()
    target_compile_options(ClockScene PRIVATE -ffp-contract=off -fno-fast-math)
endif()

add_library(SceneInputs STATIC src/scene/SceneInputs.cpp)
target_include_directories(SceneInputs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SceneInputs PUBLIC nlohmann_json::nlohmann_json)

# The app's scene: the same sources with NativeArithmetic only. EeArithmetic's functions are defined in
# Arithmetic.cpp alone, which this library leaves out, so any use of them in the app fails to link.
add_library(ClockSceneNative STATIC src/scene/ClockState.cpp src/scene/Camera.cpp src/scene/FrameHead.cpp src/scene/Orbs.cpp src/scene/Rods.cpp src/scene/Clock.cpp
    src/scene/Font.cpp src/scene/ProgramImage.cpp src/scene/Text.cpp src/scene/ClockDate.cpp src/scene/Menus.cpp src/scene/Cubes.cpp)
target_include_directories(ClockSceneNative PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ClockSceneNative PRIVATE SCENE_NATIVE_ONLY)
target_link_libraries(ClockSceneNative PRIVATE nlohmann_json::nlohmann_json)
target_sources(ClockScene PRIVATE src/scene/ColdRamps.cpp)
target_sources(ClockScene PRIVATE src/scene/ColdTime.cpp)
target_sources(ClockScene PRIVATE src/scene/ColdCamera.cpp)
target_sources(ClockScene PRIVATE src/scene/ColdFont.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/ColdRamps.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/ColdTime.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/ColdCamera.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/ColdFont.cpp)

add_library(ClockRender STATIC src/render/Device.cpp src/render/NativeRenderer.cpp)
add_dependencies(ClockRender Shaders)
target_include_directories(ClockRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockRender PUBLIC ClockVma ClockStb vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan SDL3::SDL3 ClockSceneNative)

# The clock's assets from the console's own resource files (src/assets: no Vulkan), and their decoded cache.
add_library(ClockAssets STATIC src/assets/Bytes.cpp src/assets/Expand.cpp src/assets/Romdir.cpp src/assets/ElfImage.cpp src/assets/ImageCipher.cpp
    src/assets/ClockTextures.cpp src/assets/Resources.cpp src/assets/AssetPack.cpp src/assets/Program.cpp)
target_include_directories(ClockAssets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ClockInput STATIC src/app/Input.cpp src/app/Screens.cpp)
target_include_directories(ClockInput PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockInput PUBLIC SDL3::SDL3 ClockScene)

add_library(ClockScreen STATIC src/app/ClockScreen.cpp)
target_include_directories(ClockScreen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockScreen PUBLIC ClockSceneNative SceneInputs)

# The app's opening: the same sources with NativeArithmetic only, linked into CrystalClock.
add_library(OpeningSceneNative STATIC src/scene/opening/Timeline.cpp src/scene/opening/Cubes.cpp src/scene/opening/Fog.cpp src/scene/opening/Lights.cpp src/scene/opening/Flat.cpp
    src/scene/opening/Handoff.cpp src/scene/opening/Opening.cpp src/scene/opening/Towers.cpp src/scene/opening/TowerVu1.cpp)
target_include_directories(OpeningSceneNative PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(OpeningSceneNative PRIVATE SCENE_NATIVE_ONLY)
target_link_libraries(OpeningSceneNative PUBLIC ClockSceneNative)
if(MSVC)
    target_compile_options(OpeningSceneNative PRIVATE /fp:strict)
else()
    target_compile_options(OpeningSceneNative PRIVATE -ffp-contract=off -fno-fast-math)
endif()

add_library(BootScreens STATIC src/app/OpeningScreen.cpp src/app/BootChain.cpp)
target_link_libraries(BootScreens PUBLIC OpeningSceneNative ClockScreen)

set(CLOCK_START ${CMAKE_SOURCE_DIR}/resources/clock/start.json)
set(CLOCK_START_BOOT ${CMAKE_SOURCE_DIR}/resources/clock/start-boot.json)

if(NOT CLOCK_BUILD_TESTS)
add_executable(CrystalClock src/app/Main.cpp src/app/DebugPanel.cpp src/app/Png.cpp src/app/ClockAssets.cpp)
target_link_libraries(CrystalClock PRIVATE ClockRender ClockSceneNative SceneInputs ImGui ClockAssets ClockInput BootScreens)
target_compile_definitions(CrystalClock PRIVATE
    CLOCK_SHADERS="${CLOCK_BIN_DIR}/shaders" CLOCK_START="${CLOCK_START}" CLOCK_START_BOOT="${CLOCK_START_BOOT}"
    CLOCK_SCREENSHOTS="${CMAKE_SOURCE_DIR}/out/screenshots"
    CLOCK_MENUS_START="${CMAKE_SOURCE_DIR}/resources/menus/start-menu.json" CLOCK_CLOCK_START="${CMAKE_SOURCE_DIR}/resources/menus/start-clock.json")
endif()

if(CLOCK_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    enable_testing()
    add_subdirectory(tests)
endif()
