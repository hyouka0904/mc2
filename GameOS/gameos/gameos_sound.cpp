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
#include <cmath>
#include <vector>

// HGOSAUDIO (gameos.hpp) is a pointer to this
// "class" as in gameos.hpp: MSVC mangles class and struct differently
class gosAudio {
public:
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

    // channel -1 is the mixer: master volume / balance and the 3D listener
    gosAudio_ChannelInfo* channel(int i) {
        return (i >= 0 && i < NUM_CHANNELS) ? &channel_info_[i] : (i == -1 ? &mixer_info_ : nullptr);
    }
    // Applies volume, panning, frequency and 3D settings of a channel
    void apply(int i);
    void applyAll3D() {
        for (int i = 0; i < NUM_CHANNELS; ++i)
            if (is3d_[i])
                apply(i);
    }
    void set3D(int i) { is3d_[i] = true; }
    bool is3D(int i) const { return is3d_[i]; }
    MIX_Track* track(int i) { return (i >= 0 && i < NUM_CHANNELS) ? tracks_[i] : nullptr; }

private:
    MIX_Mixer* mixer_ = nullptr;
    bool has_device_ = false;
    MIX_Track* tracks_[NUM_CHANNELS] = {};
    gosAudio_ChannelInfo channel_info_[NUM_CHANNELS];
    gosAudio_ChannelInfo mixer_info_;
    bool is3d_[NUM_CHANNELS] = {};
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
        channel_info_[i].fFrequency = 1.0f;
        channel_info_[i].fMinDistance = 1.0f;
        channel_info_[i].fMaxDistance = 1e9f; // DirectSound's default
        is3d_[i] = false;
    }
    // listener at the origin looking down +z, real world factors
    memset(&mixer_info_, 0, sizeof(mixer_info_));
    mixer_info_.fVolume = 1.0f;
    mixer_info_.fFrontZ = 1.0f;
    mixer_info_.fTopY = 1.0f;
    mixer_info_.fDoppler = mixer_info_.fRolloff = mixer_info_.fDistance = 1.0f;
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

void SoundEngine::apply(int i)
{
    const gosAudio_ChannelInfo& ci = channel_info_[i];
    MIX_Track* track = tracks_[i];
    float gain = ci.fVolume;
    float pan = ci.fPanning;
    float pitch = ci.fFrequency > 0.0f ? ci.fFrequency : 1.0f;
    if (is3d_[i]) {
        const gosSpatial s = gos_Spatialize(mixer_info_, ci);
        gain *= s.gain;
        pan = s.pan;
        pitch *= s.pitch;
    }
    MIX_SetTrackGain(track, gain);
    // Panning only attenuates the opposite side; centre is full level.
    if (pan == 0.0f) {
        MIX_SetTrackStereo(track, nullptr);
    } else {
        MIX_StereoGains gains;
        gains.left = gos_PanLeftGain(pan);
        gains.right = gos_PanRightGain(pan);
        MIX_SetTrackStereo(track, &gains);
    }
    MIX_SetTrackFrequencyRatio(track, pitch);
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

gosSpatial gos_Spatialize(const gosAudio_ChannelInfo& lis, const gosAudio_ChannelInfo& src)
{
    gosSpatial out = {1.0f, 0.0f, 1.0f};
    const float units = lis.fDistance > 0.0f ? lis.fDistance : 1.0f; // metres per unit
    const float dx = src.fPosX - lis.fPosX, dy = src.fPosY - lis.fPosY, dz = src.fPosZ - lis.fPosZ;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float r = len * units;

    // distance attenuation (distances in metres)
    const float minr = std::max(src.fMinDistance * units, 1e-6f);
    const float maxr = src.fMaxDistance * units;
    if (r >= maxr)
        out.gain = 0.0f;
    else if (r > minr)
        out.gain = minr / (minr + lis.fRolloff * (r - minr));
    if (len <= 1e-6f)
        return out; // at the listener: centred, no Doppler

    const float ux = dx / len, uy = dy / len, uz = dz / len; // listener -> source

    // right = top x front (left-handed, as DirectSound: front +z, top +y -> right +x)
    float rx = lis.fTopY * lis.fFrontZ - lis.fTopZ * lis.fFrontY;
    float ry = lis.fTopZ * lis.fFrontX - lis.fTopX * lis.fFrontZ;
    float rz = lis.fTopX * lis.fFrontY - lis.fTopY * lis.fFrontX;
    const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rl > 1e-6f)
        out.pan = std::max(-1.0f, std::min(1.0f, (ux * rx + uy * ry + uz * rz) / rl));

    // Doppler: listener moving towards the source / source towards the listener
    const float c = 343.0f / units; // speed of sound in units per second
    const float v_lis = lis.fVelX * ux + lis.fVelY * uy + lis.fVelZ * uz;
    const float v_src = -(src.fVelX * ux + src.fVelY * uy + src.fVelZ * uz);
    const float denom = c - lis.fDoppler * v_src;
    if (lis.fDoppler > 0.0f && denom > 1e-3f)
        out.pitch = std::max(0.25f, std::min(4.0f, (c + lis.fDoppler * v_lis) / denom));
    else if (lis.fDoppler > 0.0f)
        out.pitch = 4.0f;
    return out;
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
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    gosASSERT(ci);
    if (!ci)
        return;

    if (Channel == -1) {
        // the mixer: master volume and the 3D listener
        switch (prop) {
        case gosAudio_Common:
            break;
        case gosAudio_Volume:
            ci->fVolume = saturate(value1);
            MIX_SetMixerGain(engine()->mixer(), ci->fVolume);
            break;
        case gosAudio_Panning:
            ci->fPanning = value1 < -1.0f ? -1.0f : (value1 > 1.0f ? 1.0f : value1);
            break;
        case gosAudio_Position: ci->fPosX = value1; ci->fPosY = value2; ci->fPosZ = value3; break;
        case gosAudio_Velocity: ci->fVelX = value1; ci->fVelY = value2; ci->fVelZ = value3; break;
        case gosAudio_FrontOrientation: ci->fFrontX = value1; ci->fFrontY = value2; ci->fFrontZ = value3; break;
        case gosAudio_TopOrientation: ci->fTopX = value1; ci->fTopY = value2; ci->fTopZ = value3; break;
        case gosAudio_Doppler: ci->fDoppler = std::max(0.0f, value1); break;
        case gosAudio_Rolloff: ci->fRolloff = std::max(0.0f, value1); break;
        case gosAudio_Distance: ci->fDistance = value1 > 0.0f ? value1 : 1.0f; break;
        case gosAudio_Reverb: ci->fReverb = value1; break; // stored only: no reverb effect
        case gosAudio_Decay: ci->fDecay = value1; break;
        default:
            PAUSE(("Mixer slider not supported\n"));
            return;
        }
        engine()->applyAll3D();
        return;
    }

    switch (prop) {
    case gosAudio_Common:
        break;
    case gosAudio_Volume:
        ci->fVolume = saturate(value1);
        break;
    case gosAudio_Panning:
        ci->fPanning = value1 < -1.0f ? -1.0f : (value1 > 1.0f ? 1.0f : value1);
        break;
    case gosAudio_Frequency:
        ci->fFrequency = value1 > 0.0f ? value1 : 1.0f;
        break;
    case gosAudio_Position:
        ci->fPosX = value1; ci->fPosY = value2; ci->fPosZ = value3;
        engine()->set3D(Channel);
        break;
    case gosAudio_Velocity:
        ci->fVelX = value1; ci->fVelY = value2; ci->fVelZ = value3;
        engine()->set3D(Channel);
        break;
    case gosAudio_MinMaxDistance:
        ci->fMinDistance = value1 > 0.0f ? value1 : 1.0f;
        ci->fMaxDistance = value2 > ci->fMinDistance ? value2 : 1e9f;
        engine()->set3D(Channel);
        break;
    case gosAudio_ConeAngles: // stored only: sources are omnidirectional
        ci->fConeInner = value1; ci->fConeOuter = value2;
        break;
    case gosAudio_ConeOrientation:
        ci->fConeX = value1; ci->fConeY = value2; ci->fConeZ = value3;
        break;
    default:
        PAUSE(("Slider not supported yet\n"));
        return;
    }
    engine()->apply(Channel);
}

void __stdcall gosAudio_GetChannelSlider(int Channel, enum gosAudio_Properties prop, float* value1,
                                         float* value2, float* value3)
{
    gosAudio_ChannelInfo* ci = engine()->channel(Channel);
    gosASSERT(ci && value1);
    if (!ci || !value1)
        return;
    float a = 0.0f, b = 0.0f, c = 0.0f;
    switch (prop) {
    case gosAudio_Common: break;
    case gosAudio_Volume: a = ci->fVolume; break;
    case gosAudio_Panning: a = ci->fPanning; break; // -1 ... +1
    case gosAudio_Frequency: a = ci->fFrequency; break;
    case gosAudio_Position: a = ci->fPosX; b = ci->fPosY; c = ci->fPosZ; break;
    case gosAudio_Velocity: a = ci->fVelX; b = ci->fVelY; c = ci->fVelZ; break;
    case gosAudio_MinMaxDistance: a = ci->fMinDistance; b = ci->fMaxDistance; break;
    case gosAudio_FrontOrientation: a = ci->fFrontX; b = ci->fFrontY; c = ci->fFrontZ; break;
    case gosAudio_TopOrientation: a = ci->fTopX; b = ci->fTopY; c = ci->fTopZ; break;
    case gosAudio_Doppler: a = ci->fDoppler; break;
    case gosAudio_Rolloff: a = ci->fRolloff; break;
    case gosAudio_Distance: a = ci->fDistance; break;
    case gosAudio_ConeAngles: a = ci->fConeInner; b = ci->fConeOuter; break;
    case gosAudio_ConeOrientation: a = ci->fConeX; b = ci->fConeY; c = ci->fConeZ; break;
    default:
        PAUSE(("Slider not supported yet\n"));
    }
    *value1 = a;
    if (value2)
        *value2 = b;
    if (value3)
        *value3 = c;
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
