//===========================================================================//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef PREFS_H
#define PREFS_H

#ifndef MCLIB_H
#include"mclib.h"
#endif


// The player's settings (options.cfg) and the settings to reset to
// (orgprefs.cfg) live in the user data directory next to the saved games, so
// a read-only game directory (Program Files, AppImage, .app) works. Set by
// initUserPrefsPaths(), which also moves the files of older versions out of
// the game directory.
extern char gOptionsCfgPath[1024];
extern char gOrgPrefsCfgPath[1024];
void initUserPrefsPaths();

class CPrefs {
public:
	CPrefs();
	// pPath: a .cfg file; NULL = gOptionsCfgPath
	int load( const char* pPath = NULL );
	int save();
	int applyPrefs(bool bApplyResolution = 1);

	void setNewName( const char* pNewName );
	void setNewUnit( const char* pNewUnit );
	void setNewIP( const char* pNewIP );

public:
	long DigitalMasterVolume;
	long MusicVolume;
	long sfxVolume;
	long RadioVolume;
	long BettyVolume;

	bool useShadows;
	bool useWaterInterestTexture;
	bool useHighObjectDetail;

	long GameDifficulty;
	bool useUnlimitedAmmo;

	long renderer;
	//long resolution;
    int resolutionX;
    int resolutionY;

	bool fullScreen;
	long gammaLevel;
	bool useLeftRightMouseProfile; // if false, use old style commands
	long baseColor;
	long highlightColor;
	long faction;
	char insigniaFile[256];
	char unitName[10][256];
	char playerName[10][256];
	char ipAddresses[10][24];

	bool	pilotVideos;
	bool	useNonWeaponEffects;
	bool	useLocalShadows;
	bool	asyncMouse;
	long	fogPos;
	char	bitDepth; // 0 == 16, 1 == 32

	bool	saveTranscripts;
	bool	tutorials;

#if 0
	long FilterState;
	long TERRAIN_TXM_SIZE;
	long ObjectTextureSize;
	bool	useRealLOS;
	float doubleClickThreshold;
	long dragThreshold;
	DWORD BaseVertexColor;		//This color is applied to all vertices in game as Brightness correction.
#endif
};

extern CPrefs prefs;
#endif /*PREFS_H*/
