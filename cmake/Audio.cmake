add_library(ClockAudioCore STATIC src/audio/SpuWrite.cpp src/audio/EeSoundQueue.cpp)
target_include_directories(ClockAudioCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ClockAudioData STATIC src/audio/data/SndImage.cpp src/audio/data/HdBank.cpp src/audio/data/SqSequence.cpp)
target_include_directories(ClockAudioData PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioData PUBLIC ClockAudioCore ClockAssets)

add_library(ClockAudioDriver STATIC
    src/audio/driver/Driver.cpp src/audio/driver/Sequencer.cpp src/audio/driver/Ramp.cpp src/audio/driver/Init.cpp src/audio/driver/Effects.cpp src/audio/driver/Libsd.cpp
    src/audio/driver/Memory.cpp src/audio/driver/Registers.cpp src/audio/driver/IopClock.cpp)
target_include_directories(ClockAudioDriver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioDriver PUBLIC ClockAudioCore ClockAssets)

add_library(ClockAudioSnapshot STATIC src/audio/driver/SnapshotBuilder.cpp)
target_include_directories(ClockAudioSnapshot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioSnapshot PUBLIC ClockAudioDriver ClockAudioData ClockAssets)

add_library(ClockAudioSpu2 STATIC src/audio/spu2/Spu2.cpp src/audio/spu2/Restore.cpp src/audio/spu2/Snapshot.cpp src/audio/spu2/Envelope.cpp src/audio/spu2/Adpcm.cpp
    src/audio/spu2/Gauss.cpp src/audio/spu2/Mixer.cpp src/audio/spu2/Reverb.cpp)
target_include_directories(ClockAudioSpu2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioSpu2 PUBLIC ClockAudioCore ClockAssets nlohmann_json::nlohmann_json)

add_library(ClockAudioFacade STATIC src/audio/ClockSound.cpp)
target_include_directories(ClockAudioFacade PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioFacade PUBLIC ClockAudioSnapshot ClockAudioDriver ClockAudioSpu2 ClockAudioCore ClockAssets)

add_library(ClockAudioLive STATIC src/audio/Output.cpp src/audio/LiveAudio.cpp)
target_include_directories(ClockAudioLive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudioLive PUBLIC SDL3::SDL3 ClockAudioFacade)

add_library(ClockAudio INTERFACE)
target_include_directories(ClockAudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockAudio INTERFACE ClockAudioCore ClockAudioData ClockAudioDriver ClockAudioSnapshot ClockAudioSpu2 ClockAudioFacade)

if(MSVC)
    foreach(target ClockAudioSpu2 ClockAudioDriver)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2>)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_CHECKS "")
    endforeach()
endif()
