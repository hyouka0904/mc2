//===========================================================================//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef MC2MOVIE_H
#include"mc2movie.h"
#endif

#ifndef FILE_H
#include"file.h"
#endif

#ifndef GAMESOUND_H
#include"gamesound.h"
#endif

#ifndef PREFS_H
#include"prefs.h"
#endif

#include"paths.h"
#include"gameos.hpp"
#include"mods/gos_mods.h"

#define PL_MPEG_IMPLEMENTATION
#include"pl_mpeg/pl_mpeg.h"

#include <string.h>

//-----------------------------------------------------------------------
float averageFrameRate = 0.0f;
long currentFrameNum = 0;
float last30Frames[30] = {
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};

extern float frameLength;

// How far the decoded sound runs ahead of the picture, in seconds: enough to
// cover the mixer's buffer so the sound does not stutter.
static const double MOVIE_AUDIO_LEAD = 0.1;

// pl_mpeg callbacks, friends of MC2Movie
struct MC2MovieDecoder
{
	static void onVideo (plm_t* plm, plm_frame_t* frame, void* user);
	static void onAudio (plm_t* plm, plm_samples_t* samples, void* user);
};

static char* copyString (const char* s)
{
	char* c = new char[strlen(s) + 1];
	strcpy(c, s);
	return c;
}

//-----------------------------------------------------------------------
// Class MC2Movie
MC2Movie::MC2Movie (void)
{
	MC2Rect.bottom = MC2Rect.top = MC2Rect.left = MC2Rect.right = 0;
	forceStop = false;
	stillPlaying = false;
	paused = false;
	separateWAVE = false;
	soundStarted = false;
	waveName = NULL;
	m_MC2Name = NULL;
	plm = NULL;
	texture = 0;
	textureWidth = textureHeight = 0;
	pcm = NULL;
}

MC2Movie::~MC2Movie (void)
{
	// Deleted while playing (mission aborted, screen left): silence it
	if (stillPlaying)
		stopSound();
	close();
	delete [] waveName;
	delete [] m_MC2Name;
}

void MC2Movie::close (void)
{
	if (plm)
	{
		plm_destroy(plm);
		plm = NULL;
	}
	if (texture)
	{
		gos_DestroyTexture(texture);
		texture = 0;
	}
	gosAudio_DestroyPCMStream(&pcm);
	stillPlaying = false;
}

//-----------------------------------------------------------------------
void MC2Movie::init (const char *MC2Name, RECT mRect, bool useWaveFile)
{
	close();
	delete [] waveName;
	delete [] m_MC2Name;
	waveName = NULL;

	char name[1024];
	_splitpath(MC2Name,NULL,NULL,name,NULL);
	m_MC2Name = copyString(name);
	MC2Rect = mRect;
	forceStop = false;
	paused = false;
	soundStarted = false;

	// The original game plays a separate sound track unless the sound is off
	separateWAVE = false;
	if (useWaveFile && (prefs.DigitalMasterVolume != 0))
	{
		FullPathFileName wave;
		wave.init(soundPath, name, ".wav");
		if (fileExists(wave))
		{
			separateWAVE = true;
			waveName = copyString(name);
		}
	}

	FullPathFileName path;
	path.init(moviePath, name, MC2_MOVIE_EXT);
	const char* modPath = gos_ModResolvePath(path);
	plm = plm_create_with_filename(modPath ? modPath : (const char*)path);
	if (!plm)
	{
		SPEW(("MOVIE", "cannot open %s\n", (const char*)path));
		return;
	}
	if (!plm_probe(plm, 5000 * 1024) || !plm_get_num_video_streams(plm))
	{
		SPEW(("MOVIE", "no MPEG-1 video in %s\n", (const char*)path));
		close();
		return;
	}

	plm_set_video_decode_callback(plm, MC2MovieDecoder::onVideo, this);
	plm_set_audio_decode_callback(plm, MC2MovieDecoder::onAudio, this);
	plm_set_loop(plm, 0);

	// The movie's own sound plays when there is no separate track
	bool movieSound = !separateWAVE && (prefs.DigitalMasterVolume != 0) &&
		plm_get_num_audio_streams(plm) > 0;
	if (movieSound)
	{
		pcm = gosAudio_CreatePCMStream(plm_get_samplerate(plm), 2);
		movieSound = (pcm != NULL);
	}
	plm_set_audio_enabled(plm, movieSound ? 1 : 0);
	if (movieSound)
	{
		plm_set_audio_stream(plm, 0);
		plm_set_audio_lead_time(plm, MOVIE_AUDIO_LEAD);
		gosAudio_SetPCMStreamVolume(pcm, prefs.DigitalMasterVolume / 255.0f);
	}

	stillPlaying = true;
}

//-----------------------------------------------------------------------
void MC2MovieDecoder::onVideo (plm_t*, plm_frame_t* frame, void* user)
{
	MC2Movie* self = (MC2Movie*)user;
	const int w = (int)frame->width;
	const int h = (int)frame->height;
	if (w <= 0 || h <= 0)
		return;
	if (!self->texture || w != self->textureWidth || h != self->textureHeight)
	{
		if (self->texture)
			gos_DestroyTexture(self->texture);
		self->texture = gos_NewEmptyTexture(gos_Texture_Solid, "movie", ((DWORD)h << 16) | (DWORD)w,
			gosHint_DisableMipmap | gosHint_DontShrink);
		self->textureWidth = w;
		self->textureHeight = h;
		// pl_mpeg writes only R, G and B: the alpha bytes stay opaque
		self->rgba.assign((size_t)w * h * 4, 0xff);
	}
	plm_frame_to_rgba(frame, self->rgba.data(), w * 4);
	gos_UpdateTextureRGBA(self->texture, self->rgba.data());
}

void MC2MovieDecoder::onAudio (plm_t*, plm_samples_t* samples, void* user)
{
	MC2Movie* self = (MC2Movie*)user;
	gosAudio_QueuePCM(self->pcm, samples->interleaved, (int)samples->count);
}

//-----------------------------------------------------------------------
void MC2Movie::stopSound (void)
{
	if (separateWAVE && soundStarted)
		soundSystem->stopSupportSample();
	if (pcm)
		gosAudio_ClearPCM(pcm);
}

//-----------------------------------------------------------------------
//Handles tickling the movie to make sure we keep playing back
bool MC2Movie::update (void)
{
	if (!stillPlaying)
		return true;

	if (forceStop)
	{
		stopSound();
		close();
		return true;
	}

	if (!soundStarted && separateWAVE)
	{
		soundStarted = true;
		soundSystem->playDigitalStream(waveName);
	}

	if (!paused)
	{
		// frameLength is the length of the last game frame (fixed in
		// deterministic runs), so playback keeps real-time speed
		plm_decode(plm, frameLength > 0.0f ? frameLength : 1.0 / 30.0);
		if (plm_has_ended(plm))
		{
			// The decoded sound is left to finish; close() drops the stream
			stillPlaying = false;
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------
void MC2Movie::pause (bool pauseState)
{
	paused = pauseState;
	forceStop = false;
	if (pcm)
		gosAudio_PausePCMStream(pcm, pauseState);
}

//-----------------------------------------------------------------------
void MC2Movie::restart (void)
{
	forceStop = false;
	paused = false;
	if (!plm)
		return;
	plm_rewind(plm);
	if (pcm)
	{
		gosAudio_ClearPCM(pcm);
		gosAudio_PausePCMStream(pcm, false);
	}
	stillPlaying = true;
}

double MC2Movie::getTime (void) const
{
	return plm ? plm_get_time(plm) : 0.0;
}

//-----------------------------------------------------------------------
// Draws the current frame as one textured quad.
void MC2Movie::render (void)
{
	if (!stillPlaying || !texture)
		return;

	gos_SetRenderState(gos_State_Texture, texture);
	gos_SetRenderState(gos_State_Filter, gos_FilterBiLinear);
	gos_SetRenderState(gos_State_AlphaMode, gos_Alpha_OneZero);
	gos_SetRenderState(gos_State_AlphaTest, 0);
	gos_SetRenderState(gos_State_ZCompare, 0);
	gos_SetRenderState(gos_State_ZWrite, 0);
	gos_SetRenderState(gos_State_TextureAddress, gos_TextureClamp);

	const float l = (float)MC2Rect.left, r = (float)MC2Rect.right;
	const float t = (float)MC2Rect.top, b = (float)MC2Rect.bottom;
	gos_VERTEX v[4];
	memset(v, 0, sizeof(v));
	const float pos[4][4] = { {l, t, 0, 0}, {r, t, 1, 0}, {r, b, 1, 1}, {l, b, 0, 1} };
	for (int i = 0; i < 4; ++i)
	{
		v[i].x = pos[i][0];
		v[i].y = pos[i][1];
		v[i].z = 0.0f;
		v[i].rhw = 0.5f;
		v[i].argb = 0xffffffff;
		v[i].frgb = 0xff000000;
		v[i].u = pos[i][2];
		v[i].v = pos[i][3];
	}
	gos_DrawQuads(v, 4);
}
