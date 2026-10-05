#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio/ClockSound.hpp"

struct SDL_AudioStream;

namespace audio {

struct LiveAudioOptions {
    bool mute = false;
    Video video = Video::Ntsc;
    std::filesystem::path wav;
    std::filesystem::path commandLog;
};

struct LiveAudioStats {
    uint64_t frames = 0, samples = 0, underruns = 0, overruns = 0, clipped = 0;
    uint32_t peak = 0;
    uint32_t minQueuedFrames = 0, maxQueuedFrames = 0;
    bool deviceOpen = false, failed = false;
};

class LiveAudio {
public:
    LiveAudio(ClockSoundSources sources, const LiveAudioOptions& options);
    ~LiveAudio();
    LiveAudio(const LiveAudio&) = delete;
    LiveAudio& operator=(const LiveAudio&) = delete;

    void startClock();
    void queueSquare(bool hide);
    void send(uint32_t id, uint32_t a1, uint32_t a2, uint32_t a3);
    void step();
    void setMuted(bool muted) { m_muted = muted; }
    bool muted() const { return m_muted; }
    void setVolume(float volume) { m_volume = volume; }
    float volume() const { return m_volume; }
    const LiveAudioStats& stats() const { return m_stats; }
    std::string finish();

private:
    template <class F> void guarded(F&& body);

    ClockSound m_sound;
    LiveAudioOptions m_options;
    SDL_AudioStream* m_stream = nullptr;
    bool m_sdl = false;
    bool m_muted = false;
    float m_volume = 1.0f;
    LiveAudioStats m_stats;
    std::vector<int16_t> m_recorded;
    std::string m_error;
};

}
