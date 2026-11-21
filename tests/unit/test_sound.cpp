// Tests for the GameOS sound API on SDL3_mixer (GameOS/gameos/gameos_sound.cpp).
//
// The mixer is created without an audio device and pulled with
// MIX_Generate(), so the tests are deterministic and need no sound card.

#include "mc2test.h"

#include "gameos.hpp"
#include "gameos_sound.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cmath>
#include <vector>

static const int kRate = 22050; // the mixer's rate (gos_CreateAudio)

struct AudioFixture {
    AudioFixture() { ok = gos_CreateAudio(false); }
    ~AudioFixture() { gos_DestroyAudio(); }
    bool ok;
};

// Mono 16-bit square wave at full scale.
static HGOSAUDIO makeTone(int ms)
{
    std::vector<int16_t> pcm(kRate * ms / 1000);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = ((i / 25) & 1) ? 20000 : -20000;
    gosAudio_Format fmt = {};
    fmt.wFormatTag = 1;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = kRate * 2;
    HGOSAUDIO h = nullptr;
    gosAudio_CreateResource(&h, gosAudio_UserMemory, nullptr, &fmt, pcm.data(), (int)(pcm.size() * 2));
    return h;
}

// Mixes `ms` milliseconds; returns mean absolute level (0..1) of left and
// right. The mixer output is stereo S16 (gos_CreateAudio).
static void mix(int ms, double* left, double* right)
{
    std::vector<int16_t> buf((size_t)kRate * ms / 1000 * 2);
    const int got = MIX_Generate(gos_GetAudioMixer(), buf.data(), (int)(buf.size() * sizeof(int16_t)));
    double l = 0, r = 0;
    const size_t frames = got > 0 ? (size_t)got / (2 * sizeof(int16_t)) : 0;
    for (size_t i = 0; i < frames; ++i) {
        l += std::fabs(buf[2 * i] / 32768.0);
        r += std::fabs(buf[2 * i + 1] / 32768.0);
    }
    if (left) *left = frames ? l / frames : 0;
    if (right) *right = frames ? r / frames : 0;
}

TEST(pan_gains_only_attenuate_the_opposite_side)
{
    CHECK_NEAR(gos_PanLeftGain(0.0f), 1.0f, 1e-6);
    CHECK_NEAR(gos_PanRightGain(0.0f), 1.0f, 1e-6);
    CHECK_NEAR(gos_PanLeftGain(1.0f), 0.0f, 1e-6);
    CHECK_NEAR(gos_PanRightGain(1.0f), 1.0f, 1e-6);
    CHECK_NEAR(gos_PanLeftGain(-0.25f), 1.0f, 1e-6);
    CHECK_NEAR(gos_PanRightGain(-0.25f), 0.75f, 1e-6);
}

TEST(sound_plays_once_and_stops)
{
    AudioFixture f;
    REQUIRE(f.ok);
    CHECK(!gos_AudioHasDevice());

    HGOSAUDIO tone = makeTone(100);
    REQUIRE(tone != nullptr);
    gosAudio_AssignResourceToChannel(3, tone);
    CHECK(gosAudio_GetChannelPlayMode(3) == gosAudio_Stop); // assigning does not play

    gosAudio_SetChannelPlayMode(3, gosAudio_PlayOnce);
    CHECK(gosAudio_GetChannelPlayMode(3) == gosAudio_PlayOnce);

    double l, r;
    mix(50, &l, &r);
    CHECK(l > 0.1 && r > 0.1); // mono plays on both sides
    mix(200, nullptr, nullptr);
    CHECK(gosAudio_GetChannelPlayMode(3) == gosAudio_Stop); // finished
    mix(50, &l, &r);
    CHECK(l < 1e-4 && r < 1e-4);

    gosAudio_DestroyResource(&tone);
    CHECK(tone == nullptr);
}

TEST(sound_loop_pause_continue_stop)
{
    AudioFixture f;
    REQUIRE(f.ok);
    HGOSAUDIO tone = makeTone(50);
    gosAudio_AssignResourceToChannel(0, tone);
    gosAudio_SetChannelPlayMode(0, gosAudio_Loop);
    mix(300, nullptr, nullptr); // several times the clip length
    CHECK(gosAudio_GetChannelPlayMode(0) == gosAudio_Loop);

    gosAudio_SetChannelPlayMode(0, gosAudio_Pause);
    double l, r;
    mix(50, &l, &r);
    CHECK(l < 1e-4);
    CHECK(gosAudio_GetChannelPlayMode(0) != gosAudio_Stop); // paused is busy

    gosAudio_SetChannelPlayMode(0, gosAudio_Continue);
    mix(50, &l, &r);
    CHECK(l > 0.1);

    gosAudio_SetChannelPlayMode(0, gosAudio_Stop);
    CHECK(gosAudio_GetChannelPlayMode(0) == gosAudio_Stop);
    gosAudio_DestroyResource(&tone);
}

TEST(sound_volume_scales_linearly)
{
    AudioFixture f;
    REQUIRE(f.ok);
    HGOSAUDIO tone = makeTone(500);
    gosAudio_AssignResourceToChannel(1, tone);
    gosAudio_AllocateChannelSliders(1, gosAudio_Volume);

    gosAudio_SetChannelSlider(1, gosAudio_Volume, 1.0f, 0, 0);
    gosAudio_SetChannelPlayMode(1, gosAudio_Loop);
    double full, half, r;
    mix(100, &full, &r);
    gosAudio_SetChannelSlider(1, gosAudio_Volume, 0.5f, 0, 0);
    mix(100, &half, &r);
    REQUIRE(full > 0.1);
    // SDL2_mixer clamped at 128/254: volumes above 0.5 were all equal
    CHECK_NEAR(half / full, 0.5, 0.05);

    float v = 0;
    gosAudio_GetChannelSlider(1, gosAudio_Volume, &v, nullptr, nullptr);
    CHECK_NEAR(v, 0.5f, 1e-6);
    gosAudio_SetChannelSlider(1, gosAudio_Volume, 3.0f, 0, 0); // clamped
    gosAudio_GetChannelSlider(1, gosAudio_Volume, &v, nullptr, nullptr);
    CHECK_NEAR(v, 1.0f, 1e-6);
    gosAudio_DestroyResource(&tone);
}

TEST(sound_panning_attenuates_one_side)
{
    AudioFixture f;
    REQUIRE(f.ok);
    HGOSAUDIO tone = makeTone(500);
    gosAudio_AssignResourceToChannel(2, tone);
    gosAudio_SetChannelPlayMode(2, gosAudio_Loop);

    double l, r;
    gosAudio_SetChannelSlider(2, gosAudio_Panning, -1.0f, 0, 0); // hard left
    mix(100, &l, &r);
    CHECK(l > 0.1);
    CHECK(r < 1e-3);

    gosAudio_SetChannelSlider(2, gosAudio_Panning, 0.5f, 0, 0); // half right
    mix(100, &l, &r);
    CHECK_NEAR(l / r, 0.5, 0.05);

    gosAudio_SetChannelSlider(2, gosAudio_Panning, 0.0f, 0, 0); // centre
    mix(100, &l, &r);
    CHECK_NEAR(l / r, 1.0, 0.02);

    float p = 9;
    gosAudio_GetChannelSlider(2, gosAudio_Panning, &p, nullptr, nullptr);
    CHECK_NEAR(p, 0.0f, 1e-6);
    gosAudio_DestroyResource(&tone);
}

TEST(destroying_a_resource_stops_every_channel_using_it)
{
    AudioFixture f;
    REQUIRE(f.ok);
    HGOSAUDIO tone = makeTone(500);
    gosAudio_AssignResourceToChannel(4, tone);
    gosAudio_AssignResourceToChannel(5, tone);
    gosAudio_SetChannelPlayMode(4, gosAudio_Loop);
    gosAudio_SetChannelPlayMode(5, gosAudio_Loop);
    mix(20, nullptr, nullptr);

    gosAudio_DestroyResource(&tone);
    CHECK(gosAudio_GetChannelPlayMode(4) == gosAudio_Stop);
    CHECK(gosAudio_GetChannelPlayMode(5) == gosAudio_Stop);
    double l, r;
    mix(50, &l, &r);
    CHECK(l < 1e-4);

    // an unassigned resource is freed too (used to leak)
    HGOSAUDIO idle = makeTone(10);
    gosAudio_DestroyResource(&idle);
    CHECK(idle == nullptr);
}

TEST(missing_file_gives_null_resource)
{
    AudioFixture f;
    REQUIRE(f.ok);
    HGOSAUDIO h = (HGOSAUDIO)1;
    gosAudio_CreateResource(&h, gosAudio_StreamedFile, "/nonexistent/sound.wav", nullptr, nullptr, 0, false);
    CHECK(h == nullptr);
}
