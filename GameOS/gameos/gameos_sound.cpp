// GameOS sound API on top of SDL3_mixer.
//
// Every gosAudio channel is one MIX_Track; resources are MIX_Audio objects
// decoded to memory. When no audio device can be opened (headless machines,
// SDL_AUDIODRIVER=dummy without a device) the mixer is created without a
// device so the API keeps working and can be pulled with MIX_Generate().

#include "gameos.hpp"
#include "gameos_sound.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <vector>

// HGOSAUDIO (gameos.hpp) is a pointer to this
struct gosAudio {
    MIX_Audio* audio = nullptr;
};

namespace {

class SoundEngine {
public:
    static const int NUM_CHANNELS = 32;

    bool init(int frequency, bool stereo, bool open_device);
    void destroy();

    MIX_Mixer* mixer() const { return mixer_; }
    bool hasDevice() const { return has_device_; }

    gosAudio* createFromFile(const char* path);
    gosAudio* createFromMemory(const gosAudio_Format& fmt, const void* data, int size);
    void destroyAudio(gosAudio* audio);

    gosAudio_ChannelInfo* channel(int i) {
        return (i >= 0 && i < NUM_CHANNELS) ? &channel_info_[i] : nullptr;
    }
    MIX_Track* track(int i) { return (i >= 0 && i < NUM_CHANNELS) ? tracks_[i] : nullptr; }

private:
    MIX_Mixer* mixer_ = nullptr;
    bool has_device_ = false;
    MIX_Track* tracks_[NUM_CHANNELS] = {};
    gosAudio_ChannelInfo channel_info_[NUM_CHANNELS];
    std::vector<gosAudio*> audio_list_;
};

bool SoundEngine::init(int frequency, bool stereo, bool open_device)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SPEW(("AUDIO", "SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError()));
        return false;
    }
    if (!MIX_Init()) {
        SPEW(("AUDIO", "MIX_Init: %s\n", SDL_GetError()));
        return false;
    }

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = stereo ? 2 : 1;
    spec.freq = frequency;

    if (open_device)
        mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    has_device_ = mixer_ != nullptr;
    if (!mixer_) {
        if (open_device)
            SPEW(("AUDIO", "No audio device (%s); sound is mixed but not played\n", SDL_GetError()));
        mixer_ = MIX_CreateMixer(&spec);
        if (!mixer_) {
            SPEW(("AUDIO", "MIX_CreateMixer: %s\n", SDL_GetError()));
            return false;
        }
    }

    SDL_AudioSpec got;
    if (MIX_GetMixerFormat(mixer_, &got))
        SPEW(("AUDIO", "Mixer at %d Hz, %d channel(s)%s\n", got.freq, got.channels,
              has_device_ ? "" : " (no device)"));

    for (int i = 0; i < NUM_CHANNELS; ++i) {
        tracks_[i] = MIX_CreateTrack(mixer_);
        if (!tracks_[i]) {
            SPEW(("AUDIO", "MIX_CreateTrack: %s\n", SDL_GetError()));
            return false;
        }
        memset(&channel_info_[i], 0, sizeof(channel_info_[i]));
        channel_info_[i].ePlayMode = gosAudio_Stop;
        channel_info_[i].fVolume = 1.0f;
    }
    return true;
}

void SoundEngine::destroy()
{
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (tracks_[i])
            MIX_DestroyTrack(tracks_[i]);
        tracks_[i] = nullptr;
    }
    for (gosAudio* a : audio_list_) {
        MIX_DestroyAudio(a->audio);
        delete a;
    }
    audio_list_.clear();
    if (mixer_)
        MIX_DestroyMixer(mixer_);
    mixer_ = nullptr;
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

gosAudio* SoundEngine::createFromFile(const char* path)
{
    MIX_Audio* audio = MIX_LoadAudio(mixer_, path, true /* decode now */);
    if (!audio) {
        SPEW(("AUDIO", "gosAudio_CreateResource: failed to load %s: %s\n", path, SDL_GetError()));
        return nullptr;
    }
    gosAudio* a = new gosAudio;
    a->audio = audio;
    audio_list_.push_back(a);
    return a;
}

gosAudio* SoundEngine::createFromMemory(const gosAudio_Format& fmt, const void* data, int size)
{
    SDL_AudioSpec spec;
    // WAVE PCM convention: 8-bit samples are unsigned, 16-bit signed LE
    spec.format = fmt.wBitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels = fmt.nChannels;
    spec.freq = (int)fmt.nSamplesPerSec;
    if (spec.channels < 1 || spec.freq <= 0 || size <= 0) {
        SPEW(("AUDIO", "gosAudio_CreateResource: bad PCM format\n"));
        return nullptr;
    }
    MIX_Audio* audio = MIX_LoadRawAudio(mixer_, data, (size_t)size, &spec); // copies
    if (!audio) {
        SPEW(("AUDIO", "MIX_LoadRawAudio: %s\n", SDL_GetError()));
        return nullptr;
    }
    gosAudio* a = new gosAudio;
    a->audio = audio;
    audio_list_.push_back(a);
    return a;
}

void SoundEngine::destroyAudio(gosAudio* audio)
{
    // Stop every channel that uses it, not just the first one found
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        if (channel_info_[i].hAudio == audio) {
            MIX_StopTrack(tracks_[i], 0);
            MIX_SetTrackAudio(tracks_[i], nullptr);
            channel_info_[i].hAudio = nullptr;
            channel_info_[i].ePlayMode = gosAudio_Stop;
        }
    }
    audio_list_.erase(std::remove(audio_list_.begin(), audio_list_.end(), audio), audio_list_.end());
    MIX_DestroyAudio(audio->audio);
    delete audio;
}

SoundEngine* g_sound_engine = nullptr;

SoundEngine* engine()
{
    gosASSERT(g_sound_engine);
    return g_sound_engine;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////
bool gos_CreateAudio(bool open_device)
{
    if (!g_sound_engine) {
        g_sound_engine = new SoundEngine();
        if (!g_sound_engine->init(22050, true, open_device)) {
            g_sound_engine->destroy();
            delete g_sound_engine;
            g_sound_engine = nullptr;
            return false;
        }
    }
    return true;
}

void gos_DestroyAudio()
{
    if (g_sound_engine) {
        g_sound_engine->destroy();
        delete g_sound_engine;
        g_sound_engine = nullptr;
    }
}

MIX_Mixer* gos_GetAudioMixer()
{
    return g_sound_engine ? g_sound_engine->mixer() : nullptr;
}

bool gos_AudioHasDevice()
{
    return g_sound_engine && g_sound_engine->hasDevice();
}

//////////////////////////////////////////////////////////////////////////////////
// Creates a resource to be played later
//
void __stdcall gosAudio_CreateResource(HGOSAUDIO* hgosaudio, enum gosAudio_ResourceType res_type,
                                       const char* file_name, gosAudio_Format* ga_wf, void* data,
                                       int size, bool only2D)
{
    (void)only2D;
    gosASSERT(hgosaudio);
    *hgosaudio = nullptr;
    if (!g_sound_engine)
        return; // no audio: callers treat NULL as "no sound"

    if (res_type == gosAudio_StreamedFile) {
        gosASSERT(file_name);
        *hgosaudio = g_sound_engine->createFromFile(file_name);
    } else {
        gosASSERT(res_type == gosAudio_UserMemory && data && ga_wf);
        *hgosaudio = g_sound_engine->createFromMemory(*ga_wf, data, size);
    }
}

//////////////////////////////////////////////////////////////////////////////////
// Destroys a resource; channels playing it are stopped.
//
void __stdcall gosAudio_DestroyResource(HGOSAUDIO* hgosaudio)
{
    gosASSERT(hgosaudio);
    gosAudio* audio = (gosAudio*)*hgosaudio;
    *hgosaudio = nullptr;
    if (audio && g_sound_engine)
        g_sound_engine->destroyAudio(audio);
}

//////////////////////////////////////////////////////////////////////////////////
void __stdcall gosAudio_AllocateChannelSliders(int Channel, DWORD properties)
{
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    gosASSERT(ci);
    if (ci)
        ci->dwProperties = properties;
}

//////////////////////////////////////////////////////////////////////////////////
// Prepares a channel to play a resource; does not start playback.
//
void __stdcall gosAudio_AssignResourceToChannel(int Channel, HGOSAUDIO hgosaudio)
{
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    gosASSERT(ci);
    if (ci)
        ci->hAudio = hgosaudio;
}

//////////////////////////////////////////////////////////////////////////////////
void __stdcall gosAudio_SetChannelSlider(int Channel, enum gosAudio_Properties prop, float value1,
                                         float value2, float value3)
{
    (void)value2;
    (void)value3;
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    MIX_Track* track = engine()->track(Channel);
    gosASSERT(ci);
    if (!ci)
        return;

    switch (prop) {
    case gosAudio_Common:
        break;
    case gosAudio_Volume:
        ci->fVolume = saturate(value1);
        MIX_SetTrackGain(track, ci->fVolume);
        break;
    case gosAudio_Panning: {
        ci->fPanning = value1 < -1.0f ? -1.0f : (value1 > 1.0f ? 1.0f : value1);
        // Panning only attenuates the opposite side; centre is full level.
        if (ci->fPanning == 0.0f) {
            MIX_SetTrackStereo(track, nullptr);
        } else {
            MIX_StereoGains gains;
            gains.left = gos_PanLeftGain(ci->fPanning);
            gains.right = gos_PanRightGain(ci->fPanning);
            MIX_SetTrackStereo(track, &gains);
        }
        break;
    }
    case gosAudio_Frequency:
        // The game never changes frequency per channel
        PAUSE(("Cannot change frequency on a specific channel\n"));
        break;
    default:
        PAUSE(("Slider not supported yet\n"));
    }
}

void __stdcall gosAudio_GetChannelSlider(int Channel, enum gosAudio_Properties prop, float* value1,
                                         float* value2, float* value3)
{
    (void)value2;
    (void)value3;
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    gosASSERT(ci && value1);
    if (!ci || !value1)
        return;

    switch (prop) {
    case gosAudio_Common:
        break;
    case gosAudio_Volume:
        *value1 = ci->fVolume;
        break;
    case gosAudio_Panning:
        *value1 = ci->fPanning; // -1 ... +1
        break;
    case gosAudio_Frequency:
        PAUSE(("Cannot read frequency of a specific channel\n"));
        break;
    default:
        PAUSE(("Slider not supported yet\n"));
    }
}

//////////////////////////////////////////////////////////////////////////////////
// Play, Loop, Stop, Pause, or Continue a particular channel
//
void __stdcall gosAudio_SetChannelPlayMode(int Channel, enum gosAudio_PlayMode ga_pm)
{
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    MIX_Track* track = engine()->track(Channel);
    gosASSERT(ci);
    if (!ci)
        return;

    ci->ePlayMode = ga_pm;

    switch (ga_pm) {
    case gosAudio_PlayOnce:
    case gosAudio_Loop: {
        gosAudio* audio = (gosAudio*)ci->hAudio;
        if (!audio || !MIX_SetTrackAudio(track, audio->audio))
            break;
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, ga_pm == gosAudio_Loop ? -1 : 0);
        if (!MIX_PlayTrack(track, props))
            SPEW(("AUDIO", "MIX_PlayTrack(%d): %s\n", Channel, SDL_GetError()));
        SDL_DestroyProperties(props);
        break;
    }
    case gosAudio_Pause:
        MIX_PauseTrack(track);
        break;
    case gosAudio_Continue:
        MIX_ResumeTrack(track);
        break;
    case gosAudio_Stop:
        MIX_StopTrack(track, 0);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////////
// Current play mode of a channel; gosAudio_Stop once playback has finished.
//
gosAudio_PlayMode __stdcall gosAudio_GetChannelPlayMode(int Channel)
{
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    MIX_Track* track = engine()->track(Channel);
    if (!ci)
        return gosAudio_Stop;
    // A paused channel still counts as busy, as it did with SDL2_mixer
    const bool busy = MIX_TrackPlaying(track) || MIX_TrackPaused(track);
    return busy ? ci->ePlayMode : gosAudio_Stop;
}
