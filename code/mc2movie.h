//------------------------------------------------------------------
//
// Movie class
//
// Plays the game's movies: MPEG-1 video with MP2 sound in an MPEG program
// stream (.mpg), decoded with pl_mpeg. Frames are converted to RGBA and drawn
// as one textured quad, so every graphics backend can show them. The sound
// comes from data/sound/<name>.wav when the game asks for a separate track
// and that file exists, otherwise from the movie itself.
//
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef MC2MOVIE_H
#define MC2MOVIE_H

//--------------------------------------------------------------------------
#ifndef DSTD_H
#include"dstd.h"
#endif

#ifndef HEAP_H
#include"heap.h"
#endif

#include"platform_windows.h"
#include"gameos.hpp"

#include <stdint.h>
#include <vector>

// Extension of the movie files (the originals were Bink .bik files; the data
// build converts them to MPEG-1).
#define MC2_MOVIE_EXT ".mpg"

typedef struct plm_t plm_t;

//--------------------------------------------------------------------------
class MC2Movie
{
	public:
		MC2Movie (void);
		~MC2Movie (void);

		// Opens the movie (data/movies/<name>.mpg; the directory and
		// extension of MC2Name are ignored) to be shown in mRect. With
		// useWaveFile the sound comes from data/sound/<name>.wav if it exists.
		// A movie that cannot be opened counts as finished.
		void init (const char *MC2Name, RECT mRect, bool useWaveFile);

		// Advances playback by one game frame. Returns true when the movie
		// is DONE playing!!
		bool update (void);

		// Draws the current frame into the movie rectangle.
		void render (void);

		// Immediately stops playback.
		void stop (void)
		{
			forceStop = true;
		}

		// Pauses or resumes playback (and the movie's own sound).
		void pause (bool pauseState);

		// Restarts the movie from the beginning. Can be called anytime.
		void restart (void);

		// Moves or resizes the movie on screen.
		void setRect (RECT vRect)
		{
			MC2Rect = vRect;
		}

		bool isPlaying (void)
		{
			return stillPlaying;
		}

		char *getMovieName (void)
		{
			return m_MC2Name;
		}

		// Seconds of movie played so far
		double getTime (void) const;

	private:
		MC2Movie (const MC2Movie&);
		MC2Movie& operator= (const MC2Movie&);

		friend struct MC2MovieDecoder; // pl_mpeg callbacks (mc2movie.cpp)
		void close (void);
		void stopSound (void);

		RECT		MC2Rect;			// where the movie is drawn
		bool		forceStop;			// should the movie end now?
		bool		stillPlaying;		// is the movie still running?
		bool		paused;

		bool		separateWAVE;		// sound comes from data/sound/<name>.wav
		bool		soundStarted;
		char*		waveName;
		char*		m_MC2Name;

		plm_t*		plm;
		DWORD		texture;			// gos texture holding the current frame
		int			textureWidth;
		int			textureHeight;
		std::vector<uint8_t> rgba;		// frame converted for the texture
		HGOSPCMSTREAM pcm;				// the movie's own sound, if played
};

typedef MC2Movie *MC2MoviePtr;

//--------------------------------------------------------------------------
#endif
