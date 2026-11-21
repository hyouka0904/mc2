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

#endif
