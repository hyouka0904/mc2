// SPDX-License-Identifier: GPL-3.0-only
#ifndef GAMEOS_SOUND_H
#define GAMEOS_SOUND_H

// Internals of the GameOS sound implementation that tests and the main loop
// need. The game itself only uses the gosAudio_* API from gameos.hpp.

typedef struct MIX_Mixer MIX_Mixer;

// open_device = false mixes without playing (tests pull with MIX_Generate)
bool gos_CreateAudio(bool open_device = true);
void gos_DestroyAudio();

// The SDL3_mixer mixer behind the gosAudio channels (NULL before
// gos_CreateAudio). Without a device it can be pulled with MIX_Generate().
MIX_Mixer* gos_GetAudioMixer();
bool gos_AudioHasDevice();

// Stereo gains for a pan value in -1 (left) .. +1 (right). Panning only
// attenuates the opposite side: centre plays both sides at full level.
inline float gos_PanLeftGain(float pan) { return pan > 0.0f ? 1.0f - pan : 1.0f; }
inline float gos_PanRightGain(float pan) { return pan < 0.0f ? 1.0f + pan : 1.0f; }

// 3D sound (gosAudio_Position, _Velocity, _MinMaxDistance on a channel; the
// listener's position, velocity, orientation and the Doppler / rolloff /
// distance factors on channel -1). Result for one source:
//   gain   distance attenuation: 1 within MinDistance, then
//          min / (min + rolloff * (r - min)), 0 beyond MaxDistance
//   pan    -1 (left) .. +1 (right) from the listener's orientation
//   pitch  Doppler frequency ratio (speed of sound 343 m/s; fDistance is
//          metres per unit), clamped to 0.25 .. 4
struct _gosAudio_ChannelInfo;
struct gosSpatial {
    float gain;
    float pan;
    float pitch;
};
gosSpatial gos_Spatialize(const struct _gosAudio_ChannelInfo& listener, const struct _gosAudio_ChannelInfo& source);

// Output limiter. SDL3_mixer adds tracks with a hard clamp at full scale,
// which crackles when several loud sounds play at once. The tracks are
// therefore mixed at kGosMixHeadroom and the limiter applies the make-up
// gain 1 / kGosMixHeadroom: levels below kCeiling pass unchanged, louder
// peaks are turned down at once and the gain recovers over kReleaseSeconds.
const float kGosMixHeadroom = 0.5f;
struct gosLimiter {
    static constexpr float kCeiling = 0.95f;
    static constexpr float kReleaseSeconds = 0.15f;
    float gain = 1.0f;    // current gain reduction (1 = none)
    float release = 1.0f; // per-frame recovery coefficient
    void setRate(int frequency);
    // pcm: interleaved float frames, samples = frames * channels
    void process(float* pcm, int samples, int channels, float makeup);
};

#endif
