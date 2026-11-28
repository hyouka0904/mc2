#define MAINMENU_CPP
/*************************************************************************************************\
MainMenu.cpp			: Implementation of the MainMenu component.
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//
\*************************************************************************************************/

#ifdef LINUX_BUILD
#include "platform_windows.h"
struct DDSURFACEDESC2 {
};
#else
#include<windows.h>
#include<ddraw.h>
#endif
#include"mainmenu.h"
#include"mclib.h"
#include"inifile.h"
#include"logisticsdata.h"
#include"logisticsdialog.h"
#include"abutton.h"
#include"optionsscreenwrapper.h"
#include "../res/resource.h"
#include"mechlopedia.h"
#include"gamesound.h"
#include"aanimobject.h"
#include"multplyr.h"

#include"prefs.h"
#include "update/gos_update.h"
#include "mc2_version.h"
#include <math.h>
extern CPrefs prefs;


#define MM_MSG_NEW_CAMPAIGN 90
#define MM_MSG_SAVE			92
#define MM_MSG_LOAD			91
#define MM_MSG_MULTIPLAYER	93
#define MM_MSG_RETURN_TO_GAME 94
#define MM_MSG_OPTIONS		95
#define MM_MSG_ENCYCLOPEDIA	96
#define MM_MSG_EXIT			97
#define MM_MSG_SINGLE_MISSION	98
#define MM_MSG_LEGAL		99

extern volatile bool mc2IsInMouseTimer;
extern volatile bool mc2IsInDisplayBackBuffer;

void MouseTimerKill();

extern void (*AsynFunc)(RECT& WinRect,DDSURFACEDESC2& mouseSurfaceDesc );




extern bool bInvokeOptionsScreenFlag;
bool	MainMenu::bDrawMechlopedia = false;;

void SplashIntro::init()
{
	FullPathFileName path;
	path.init( artPath, "mcl_splashscreenintro", ".fit" );

	FitIniFile file;
	if ( NO_ERR != file.open( path ) )
	{
		char errorStr[256];
		sprintf( errorStr, "couldn't open file %s", (const char*)path );
		Assert(0,0,errorStr );
	}

	LogisticsScreen::init( file, "Static", "Text", "Rect", "Button" );
}

MainMenu::MainMenu(  )
{
	optionsScreenWrapper = NULL;
	bOptions = 0;
	bSave = bLoad = 0;
	helpTextArrayID = 0;
	mechlopedia = 0;
	bDrawMechlopedia = 0;
	tuneId = -1;
	bLoadSingle = 0; 
	bLoadCampaign = 0;
	introOver = 0;
	bHostLeftDlg = 0;
	bUpdateDlg = 0;
	bUpdateNoticeHidden = 0;
	bUpdateResultShown = 0;
	bUpdateSizeShown = 0;
	bUpdateLater = 0;

	introMovie = 0;
}

//-------------------------------------------------------------------------------------------------

MainMenu::~MainMenu()
{
	if ( optionsScreenWrapper )
		delete optionsScreenWrapper;
}

int MainMenu::init( FitIniFile& file )
{
	file.seekBlock("Tunes");
	file.readIdLong("TuneId",tuneId);

	LogisticsScreen::init( file, "Static", "Text", "Rect", "Button" );

	FullPathFileName name;
	name.init( artPath, "mcl_sp", ".fit" );
	FitIniFile file2;
	if ( NO_ERR != file2.open( name ) )
	{
		char errorStr[256];
		sprintf( errorStr, "couldn't open file %s", (const char*)name );
		Assert(0,0,errorStr );
	}

	background.init( file2, "Static", "Text", "Rect", "Button" );

	for ( int i = 0; i < buttonCount; i++ )
	{
		buttons[i].setMessageOnRelease();
		buttons[i].setPressFX(LOG_VIDEOBUTTONS);
		buttons[i].setHighlightFX(LOG_DIGITALHIGHLIGHT);
	}

	beginAnim.initWithBlockName( &file, "InAnim" );
	endAnim.initWithBlockName( &file, "OutAnim" );


	intro.init();

	FullPathFileName path;
	path.init( artPath, "mcl_mp_loadmap", ".fit" );

	FitIniFile mpFile;

	if ( NO_ERR != mpFile.open( path ) )
	{
		char error[256];
		sprintf( error, "couldn't open file %s", (const char*)path );
		Assert( 0, 0, error );
		return -1;		
	}

	singleLoadDlg.init( &mpFile );

	introMovie = 0;

	path.init( moviePath, "msft", MC2_MOVIE_EXT);

	RECT movieRect;
	movieRect.top = 0;
	movieRect.left = 0;
	movieRect.right = Environment.screenWidth;
	movieRect.bottom = Environment.screenHeight;

	introMovie = new MC2Movie;
	introMovie->init(path,movieRect,true);

	return 0;
}

void MainMenu::begin()
{
	status = RUNNING;
	promptToQuit = 0;
	beginAnim.begin();
	beginFadeIn( 0 );
	endAnim.end();
	background.beginFadeIn( 0 );
	endResult = RUNNING;
	musicStarted = false;
	bLoadSingle = 0;
	bLoadCampaign = 0;
	promptToDisconnect = 0; 
	bLegal = 0;
	bUpdateDlg = 0;

	// no host left dlg, sometimes we get this begin call 2x's due to netlib weirdness
	if ( !introMovie )
	{
			while (mc2IsInMouseTimer)
				;

			//ONLY set the mouse BLT data at the end of each update.  NO MORE FLICKERING THEN!!!
			// BLOCK THREAD WHILE THIS IS HAPPENING
			mc2IsInDisplayBackBuffer = true;

			mc2UseAsyncMouse = prefs.asyncMouse;
			if ( !mc2UseAsyncMouse)
				MouseTimerKill();

			mc2IsInDisplayBackBuffer = false;

			AsynFunc = NULL;

			//Force mouse Cursors to smaller or larger depending on new video mode.
			userInput->initMouseCursors("cursors");
			userInput->mouseOn();
			userInput->setMouseCursor( mState_LOGISTICS );

			DWORD localRenderer = prefs.renderer;
			if (prefs.renderer != 0 && prefs.renderer != 3)
				localRenderer = 0;

   			bool localFullScreen = prefs.fullScreen;
   			bool localWindow = !prefs.fullScreen;
   			if (Environment.fullScreen && prefs.fullScreen)
   				localFullScreen = false;


			// make sure we get into 800 x 600 mode
			if ( Environment.screenWidth != 800 )
			{
			
				if (prefs.renderer == 3)
					gos_SetScreenMode(800,600,16,0,0,0,true,localFullScreen,0,localWindow,0,localRenderer);
				else
					gos_SetScreenMode(800,600,16,prefs.renderer,0,0,0,localFullScreen ,0,localWindow,0,localRenderer);
			}


	}
}

void MainMenu::end()
{
	endAnim.end();
	bHostLeftDlg = 0;

}

void MainMenu::setDrawBackground( bool bNewDrawBackground )
{
	bDrawBackground = bNewDrawBackground;
	if ( bDrawBackground && !introOver)
	{
		intro.begin();
	}
}

int	MainMenu::handleMessage( unsigned long what, unsigned long who )
{
	switch ( who )
	{
		case MM_MSG_NEW_CAMPAIGN:
			if ( MPlayer )
			{
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				endResult = MM_MSG_NEW_CAMPAIGN;
				promptToDisconnect = true;
			}
			else
			{
				endAnim.begin();
				beginAnim.end();
				bLoadCampaign = true;
				LogisticsSaveDialog::instance()->beginCampaign();
				if ( LogisticsSaveDialog::instance()->isDone() )
				{
					LogisticsData::instance->startNewCampaign( LogisticsSaveDialog::instance()->getFileName());
					status = RESTART;
				}
			}
			break;
		case MM_MSG_SAVE:
			if ( MPlayer )
			{
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				endResult = who;
				promptToDisconnect = true;
			}
			else
			{
		
				// need to pop dialog here
				LogisticsSaveDialog::instance()->begin();
				endAnim.begin();
				beginAnim.end();
				bSave = true;
			}
			break;

		case MM_MSG_LOAD:

			if ( MPlayer )
			{
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				endResult = who;
				promptToDisconnect = true;
			}
			else
			{
			
				// need to pop dialog here
				LogisticsSaveDialog::instance()->beginLoad();
				endAnim.begin();
				beginAnim.end();
				bLoad = true;
			}
			break;
		case MM_MSG_MULTIPLAYER:
			if ( MPlayer )
			{
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				endResult = who;
				promptToDisconnect = true;
			}
			else
			{
				endAnim.begin();
				beginAnim.end();
				endResult = MULTIPLAYERRESTART;
				LogisticsData::instance->startMultiPlayer();
			}
			break;

		case MM_MSG_RETURN_TO_GAME:
			{
			if ( !bDrawBackground )
			{
				endAnim.begin();
				beginAnim.end();
				endResult = NEXT;
				soundSystem->playDigitalSample( LOG_MAINMENUBUTTON );
				soundSystem->playDigitalMusic(LogisticsData::instance->getCurrentMissionTune());
			}
			}
			break;
		case MM_MSG_OPTIONS:
			// need to throw up the options screen here...
			if (!optionsScreenWrapper)
			{
				optionsScreenWrapper = new OptionsScreenWrapper;
				optionsScreenWrapper->init();
			}
			optionsScreenWrapper->begin();

			bOptions = true;
			break;
		case MM_MSG_ENCYCLOPEDIA:
			bDrawMechlopedia = true;
			beginFadeOut(1.0);
			if ( !mechlopedia )
			{
				mechlopedia = new Mechlopedia;
				mechlopedia->init();
			}

			mechlopedia->begin();
			break;
		case MM_MSG_EXIT:
			promptToQuit = 1;
			// may need to set the text here
			LogisticsOKDialog::instance()->setText( IDS_DIALOG_QUIT_PROMPT,
					IDS_DIALOG_NO, IDS_DIALOG_YES );
				
			LogisticsOKDialog::instance()->begin();
			getButton( who )->press( 0 );
			break;

		case MM_MSG_SINGLE_MISSION:

			if ( MPlayer )
			{
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				endResult = who;
				promptToDisconnect = true;
			}
			else
			{
				bLoadSingle = true;
				endAnim.begin();
				beginAnim.end();
				singleLoadDlg.beginSingleMission();
				getButton( who )->press( 0 );
			}
			break;

		case MM_MSG_LEGAL:
			{
				bLegal = 1;
				// may need to set the text here
				if ( !LogisticsLegalDialog::instance() )
				{
					FullPathFileName path;
					path.init( artPath, "mcl_dialoglegal", ".fit" );
					FitIniFile file;
					file.open( path );
					LogisticsLegalDialog::init( file );
				}
				LogisticsLegalDialog::instance()->setText( IDS_DIALOG_OK,
						IDS_DIALOG_OK, IDS_DIALOG_OK );
				//Needs to be this long for LOC!
				// -fs
				char realText[2048];
				cLoadString(IDS_LAWYER_BABBLE, realText, 2047 );
				char lawyerBabble[2048];
				DWORD pIDLen = 64;
				char pID[64];
				sprintf( pID, "INVALID ID" );
				gos_LoadDataFromRegistry("PID", pID, &pIDLen);
				sprintf( lawyerBabble, realText, pID );
				LogisticsLegalDialog::instance()->setText( lawyerBabble );
				LogisticsLegalDialog::instance()->begin();
				LogisticsLegalDialog::instance()->setFont( IDS_LAWYER_BABBLE_FONT );				
				getButton( who )->press( 0 );
			}
			break;
		default:
			break;
	}

	return 0;
}

void MainMenu::skipIntro()
{
	if ( introMovie )
	{
		introMovie->stop();
		delete introMovie;
		introMovie = NULL;

	}
}

void MainMenu::update()
{

	if ( bDrawBackground || MPlayer || LogisticsData::instance->isSingleMission() )
	{
		getButton( MM_MSG_SAVE )->disable( true );
	}
	else
		getButton( MM_MSG_SAVE )->disable( false );

	// LAN multiplayer: available from the start menu (not from inside a
	// single player mission).
	getButton( MM_MSG_MULTIPLAYER )->disable( !bDrawBackground && !MPlayer );

	if ( introMovie )
	{
		userInput->mouseOff();

		if (userInput->getKeyDown(KEY_SPACE) || userInput->getKeyDown(KEY_ESCAPE) || userInput->getKeyDown(KEY_LMOUSE))
		{
			introMovie->stop();
		}

		bool result = introMovie->update();
		if (result)
		{
			
			//Movie's Over.
			//Whack it.
			delete introMovie;
			introMovie = NULL;
		}

		return;
	}

	if (!musicStarted)
	{
		musicStarted = true;
		soundSystem->setMusicVolume( prefs.MusicVolume );
		soundSystem->playDigitalMusic(tuneId);
	}

	if ( endAnim.isDone() )
	{
		status = endResult;
	}

	if ( bDrawMechlopedia )
	{
		mechlopedia->update();
		if ( mechlopedia->getStatus() == NEXT )
		{
			beginFadeIn( 0 );
			bDrawMechlopedia = 0;
			if ( !bDrawBackground )
				status = NEXT;
		}
		return;
	}

	if ( bOptions )
	{
		OptionsScreenWrapper::status_type result = optionsScreenWrapper->update();
		if (result == OptionsScreenWrapper::opt_DONE)
		{
			optionsScreenWrapper->end();
			bOptions = 0;
		}

		return;
	}

	if ( (bSave || bLoad || bLoadCampaign) && endAnim.isDone() )
	{
		LogisticsSaveDialog::instance()->update();

		if ( LogisticsSaveDialog::instance()->getStatus() == LogisticsScreen::YES 
			&& LogisticsSaveDialog::instance()->isDone() )
		{
			
			char name[1024];
			strcpy( name, savePath );
			strcat( name, LogisticsSaveDialog::instance()->getFileName() );
			int index = strlen( name ) - 4;
			if ( S_stricmp( &name[index], ".fit" ) !=0 ) 
				strcat( name, ".fit" );

			
			FitIniFile file;
			if ( bSave )
			{
				// make sure the save game directory exists, if not create it
				CreateDirectory( savePath, NULL );

				if ( NO_ERR != file.createWithCase( name ) )
				{
					char errorStr[1088];
					snprintf( errorStr, sizeof(errorStr), "couldn't open the file %s", name );
					Assert( 0, 0, errorStr );
				}
				else
				{
					LogisticsData::instance->save( file );
					LogisticsSaveDialog::instance()->end();
					file.close();
				}
				bSave = bLoad = 0;
				status = NEXT;
				

			}
			else if ( bLoadCampaign )
			{
				LogisticsData::instance->startNewCampaign( LogisticsSaveDialog::instance()->getFileName());
				status = endResult = RESTART;
//				background.beginFadeOut( 1.0f );
//				beginFadeOut( 1.0f );
				bLoadCampaign = 0;
			}
			else
			{
				const bool do_not_make_lower = true;
				if ( NO_ERR != file.open( name, READ, 50, do_not_make_lower ) )
				{
					char errorStr[1088];
					snprintf( errorStr, sizeof(errorStr), "couldn't open the file %s", name );
					Assert( 0, 0, errorStr );
				}
				else
					LogisticsData::instance->load( file );
				LogisticsSaveDialog::instance()->end();
				bSave = bLoad = 0;
				status = RESTART;
				file.close();

			}
		}
		else if ( LogisticsSaveDialog::instance()->getStatus() == LogisticsScreen::NO &&
			LogisticsSaveDialog::instance()->isDone())
		{
			LogisticsSaveDialog::instance()->end();
			bSave = bLoad = bLoadCampaign = 0 ;
			if ( !bDrawBackground )
				status = NEXT;
			else
			{
				beginAnim.begin();
				endAnim.end();
			}
		}
		return;
	}
	else if ( bLoadSingle && endAnim.isDone())
	{
		singleLoadDlg.update();
		if ( singleLoadDlg.isDone() )
		{
			if ( singleLoadDlg.getStatus() == YES )
			{
				const char* pName = singleLoadDlg.getMapFileName();
				if (pName)
				{
					LogisticsData::instance->setSingleMission( pName );
					status = SKIPONENEXT;
				}
			}

			bLoadSingle = 0;
			beginAnim.begin();
			endAnim.end();
		}
	}

	else if ( promptToQuit )
	{
		LogisticsOKDialog::instance()->update();
		{
			if ( LogisticsOKDialog::instance()->getStatus() == LogisticsScreen::YES )
			{
				soundSystem->playDigitalSample( LOG_EXITGAME );
				gos_TerminateApplication();
				promptToQuit = 0;

			}
			else if ( LogisticsOKDialog::instance()->getStatus() == LogisticsScreen::NO)
			{
				if ( LogisticsOKDialog::instance()->isDone() )
					promptToQuit = 0;
			}

			

		}
	}
	else if ( bLegal )
	{
		LogisticsLegalDialog::instance()->update();
		if ( LogisticsLegalDialog::instance()->isDone() )
		{
			LogisticsLegalDialog::instance()->end();
			bLegal = 0;
		}
	}
	else if ( bHostLeftDlg )
	{
		LogisticsOneButtonDialog::instance()->update();
		if ( LogisticsOneButtonDialog::instance()->isDone() )
		{
			LogisticsOneButtonDialog::instance()->end();
			bHostLeftDlg = 0;
		}

		if ( MPlayer ) // has to be done, but can't be done when initialized
		{
			MPlayer->closeSession();
			delete MPlayer;
			MPlayer = NULL;
		}
	}
	else if ( bUpdateDlg )
	{
		// The size estimate arrived while the prompt is open
		if ( bUpdateDlg == UPDATE_DLG_AVAILABLE && !bUpdateSizeShown )
		{
			char text[1024];
			availablePromptText( text, sizeof(text) );
			if ( bUpdateSizeShown )
				LogisticsOKDialog::instance()->setText( text );
		}
		LogisticsOKDialog::instance()->update();
		if ( LogisticsOKDialog::instance()->isDone() )
		{
			const bool yes = YES == LogisticsOKDialog::instance()->getStatus();
			const long kind = bUpdateDlg;
			bUpdateDlg = 0;
			if ( kind == UPDATE_DLG_AVAILABLE )
			{
				if ( !yes )
					bUpdateLater = true; // until the next start; Options keeps an update button
				else if ( gos_update::canInstall() )
				{
					bUpdateResultShown = 0;
					gos_update::startDownload();
				}
				else
				{
					// Keep reminding on later starts until the new version runs
					gos_OpenURL( gos_update::available().url.c_str() );
					bUpdateNoticeHidden = true;
				}
			}
			else if ( kind == UPDATE_DLG_INSTALL && yes )
			{
				std::string error;
				if ( gos_update::installDownloaded( &error ) )
				{
					gos_TerminateApplication(); // the new version starts by itself
				}
				else if ( !error.empty() )
				{
					updateFailure = error;
					showUpdateDialog( UPDATE_DLG_FAILED );
				}
				else
					bUpdateNoticeHidden = true; // macOS: the disk image is open
			}
			else if ( kind == UPDATE_DLG_FAILED && yes )
			{
				gos_OpenURL( gos_update::available().url.c_str() );
				bUpdateNoticeHidden = true;
			}
		}
	}
	else if ( promptToDisconnect )
	{
		LogisticsOKDialog::instance()->update();
		if ( LogisticsOKDialog::instance()->isDone() )
		{
			if ( YES == LogisticsOKDialog::instance()->getStatus() )
			{
				if ( MPlayer )
				{
					MPlayer->closeSession();
					delete MPlayer;
					MPlayer = NULL;
				}
				long oldRes = endResult;
				endResult = 0;

				handleMessage( oldRes, oldRes );

				setDrawBackground( true );
			}
			else
				handleMessage( NEXT, NEXT );
			
			promptToDisconnect = 0;
		}
	}
	else
	{
		if ( bDrawBackground  )
		{
			if ( !intro.animObjects[0].isDone() )
			{
				intro.update();
				background.update();
				if (userInput->getKeyDown(KEY_ESCAPE) || (Environment.Renderer == 3))
				{
					introOver = true;
					userInput->mouseOn();
					soundSystem->playDigitalSample( LOG_MAINMENUBUTTON );

				}
				else if ( !introOver )
					return;
			}
			else
			{
				background.update();
				if ( !introOver )
					soundSystem->playDigitalSample( LOG_MAINMENUBUTTON );

				introOver = true;
				userInput->mouseOn();

			}
		}

		beginAnim.update();
		endAnim.update();

		LogisticsScreen::update();
		if ( updateNoticeVisible() )
		{
			gos_update::startEstimate(); // once per version, in the background
			// The download finished (or failed) while the menu was up
			const gos_update::DownloadStatus ds = gos_update::downloadStatus();
			if ( !bUpdateResultShown && ds == gos_update::DownloadStatus::Ready )
			{
				bUpdateResultShown = true;
				showUpdateDialog( UPDATE_DLG_INSTALL );
				return;
			}
			if ( !bUpdateResultShown && ds == gos_update::DownloadStatus::Failure )
			{
				bUpdateResultShown = true;
				updateFailure = gos_update::downloadError();
				showUpdateDialog( UPDATE_DLG_FAILED );
				return;
			}
		}
		if ( updateNoticeVisible() && userInput->isLeftClick() )
		{
			GUI_RECT r;
			updateNoticeRect( r );
			const long mx = userInput->getMouseX(), my = userInput->getMouseY();
			const gos_update::DownloadStatus ds = gos_update::downloadStatus();
			if ( mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom
				&& ds != gos_update::DownloadStatus::Downloading )
			{
				if ( ds == gos_update::DownloadStatus::Ready )
					showUpdateDialog( UPDATE_DLG_INSTALL );
				else if ( ds == gos_update::DownloadStatus::Failure )
				{
					updateFailure = gos_update::downloadError();
					showUpdateDialog( UPDATE_DLG_FAILED );
				}
				else
					showUpdateDialog( UPDATE_DLG_AVAILABLE );
				soundSystem->playDigitalSample( LOG_MAINMENUBUTTON );
				return;
			}
		}
		if ( (!bLoadSingle) && userInput->isLeftClick() && !inside( userInput->getMouseX(), userInput->getMouseY() ) )
		{
			handleMessage( 0, MM_MSG_RETURN_TO_GAME );
		}
	}
}

void MainMenu::render()
{

	if (introMovie)
	{
		introMovie->render();
		return;
	}
	if ( bDrawMechlopedia && (fadeTime > fadeOutTime || !fadeOutTime) )
	{
		mechlopedia->render();
		return;
	}

	
	if ( bOptions )
	{
		optionsScreenWrapper->render();
		return;
	}

	//DO NOT play the splash screen animation in software.
	// WOW does it beat up the framerate!
	float xDelta = 0.f;
	float yDelta = 0.f;  
	long color = 0xff000000;

	if (Environment.Renderer != 3)
	{

		if ( beginAnim.isAnimating() && !beginAnim.isDone() )
		{
			xDelta = beginAnim.getXDelta();
			yDelta = beginAnim.getYDelta();

			float time = beginAnim.getCurrentTime();
			float endTime = beginAnim.getMaxTime();
			if ( endTime )
			{
				color = interpolateColor( 0x00000000, 0x7f000000, time/endTime );

			}
		}

		else if (endAnim.isAnimating() /*&& !endAnim.isDone()*/)
		{
			xDelta = endAnim.getXDelta();
			yDelta = endAnim.getYDelta();

			float time = endAnim.getCurrentTime();
			float endTime = endAnim.getMaxTime();
			if ( endTime && (time <= endTime))
			{
				color = interpolateColor( 0x7f000000, 0x00000000, time/endTime );
			}
		}

		GUI_RECT rect = { 0, 0, Environment.screenWidth, Environment.screenHeight };
		drawRect( rect, color );

		if ( bDrawBackground )
		{
			background.render();
			intro.render();
			if ( !intro.animObjects[0].isDone() && !introOver && !bHostLeftDlg )
				return;


		}
	}
	else
	{
		GUI_RECT rect = { 0, 0, Environment.screenWidth, Environment.screenHeight };
		drawRect( rect, color );
	}

	if ( !xDelta && !yDelta )
	{
		drawShadowText( 0xffc66600, 0xff000000, textObjects[1].font.getTempHandle(), 
			textObjects[1].globalX(), textObjects[1].globalTop(),
			textObjects[1].globalRight(), textObjects[1].globalBottom(),
			true, textObjects[1].text, false, textObjects[1].font.getSize(), 1, 1 );
	}

	textObjects[1].showGUIWindow( false );

	
	if ( (!bSave && !bLoad && !bLoadSingle && !bLoadCampaign) || (!endAnim.isDone() && endResult != RESTART ) )	
		LogisticsScreen::render( xDelta, yDelta );
	else if ( bLoadSingle )
		singleLoadDlg.render();
	else
		LogisticsSaveDialog::instance()->render();
 
	if ( updateNoticeVisible() && !bUpdateDlg && !xDelta && !yDelta )
	{
		GUI_RECT r;
		updateNoticeRect( r );
		char fmt[256];
		char text[512];
		const gos_update::DownloadStatus ds = gos_update::downloadStatus();
		const std::string version = gos_update::available().version;
		int64_t received = 0, total = 0;
		if ( ds == gos_update::DownloadStatus::Downloading && gos_update::downloadBytes( &received, &total ) )
		{
			cLoadString( IDS_UPDATE_DOWNLOADING, fmt, 255 );
			snprintf( text, sizeof(text), fmt, version.c_str(),
				gos_update::formatProgress( received, total ).c_str(),
				(int)(gos_update::downloadProgress() * 100.f) );
		}
		else if ( ds == gos_update::DownloadStatus::Downloading )
		{
			cLoadString( IDS_UPDATE_PREPARING, fmt, 255 );
			snprintf( text, sizeof(text), fmt, version.c_str() );
		}
		else
		{
			cLoadString( ds == gos_update::DownloadStatus::Ready ? IDS_UPDATE_READY : IDS_UPDATE_NOTICE, fmt, 255 );
			snprintf( text, sizeof(text), fmt, version.c_str() );
		}
		// Pulse gently so it is noticed without being in the way
		const float t = (float)fmod( gos_GetElapsedTime( 1 ), 1.5 ) / 1.5f;
		const float a = t < 0.5f ? t * 2.f : (1.f - t) * 2.f;
		const long color = interpolateColor( 0xffc66600, 0xffffd040, a );
		drawShadowText( color, 0xff000000, textObjects[1].font.getTempHandle(),
			r.left, r.top, r.right, r.bottom,
			true, text, false, textObjects[1].font.getSize(), 1, 1 );
	}

	if ( promptToQuit || promptToDisconnect || bUpdateDlg )
	{
		LogisticsOKDialog::instance()->render();
	}
	if ( bLegal )
	{
		LogisticsLegalDialog::instance()->render();
	}
	if ( bHostLeftDlg )
	{
		LogisticsOneButtonDialog::instance()->render();
	}



	
}

// Only on the real main menu (not the in-mission menu), once the intro is
// over and no other dialog is up.
bool MainMenu::updateNoticeVisible()
{
	// After "Later" a download started in Options brings the notice back
	const bool later = bUpdateLater && gos_update::downloadStatus() == gos_update::DownloadStatus::Idle;
	return bDrawBackground && introOver && !bUpdateNoticeHidden && !later && !MPlayer
		&& gos_update::status() == gos_update::Status::Available
		&& !promptToQuit && !promptToDisconnect && !bLegal && !bHostLeftDlg
		&& !bOptions && !bSave && !bLoad && !bLoadSingle && !bLoadCampaign && !bDrawMechlopedia;
}

void MainMenu::showUpdateDialog( long kind )
{
	const gos_update::Release rel = gos_update::available();
	char fmt[512];
	char text[1024];
	LogisticsOKDialog* dlg = LogisticsOKDialog::instance();
	if ( kind == UPDATE_DLG_AVAILABLE )
	{
		const bool install = gos_update::canInstall();
		const long prompt = install ? IDS_UPDATE_PROMPT_INSTALL : IDS_UPDATE_PROMPT;
		availablePromptText( text, sizeof(text) );
		dlg->setText( prompt, IDS_UPDATE_LATER, install ? IDS_UPDATE_INSTALL : IDS_UPDATE_DOWNLOAD );
	}
	else if ( kind == UPDATE_DLG_INSTALL )
	{
#ifdef __APPLE__
		const long prompt = IDS_UPDATE_READY_PROMPT_MAC;
#else
		const long prompt = IDS_UPDATE_READY_PROMPT;
#endif
		cLoadString( prompt, fmt, 511 );
		snprintf( text, sizeof(text), fmt, rel.version.c_str() );
		dlg->setText( prompt, IDS_UPDATE_LATER, IDS_UPDATE_INSTALL_NOW );
	}
	else
	{
		cLoadString( IDS_UPDATE_FAILED, fmt, 511 );
		snprintf( text, sizeof(text), fmt, updateFailure.c_str() );
		dlg->setText( IDS_UPDATE_FAILED, IDS_UPDATE_LATER, IDS_UPDATE_DOWNLOAD );
	}
	dlg->setText( text );
	dlg->begin();
	bUpdateDlg = kind;
}

// "Version x is available...", plus the download size when the game installs it
void MainMenu::availablePromptText( char* text, size_t size )
{
	const gos_update::Release rel = gos_update::available();
	const bool install = gos_update::canInstall();
	char fmt[512];
	cLoadString( install ? IDS_UPDATE_PROMPT_INSTALL : IDS_UPDATE_PROMPT, fmt, 511 );
	snprintf( text, size, fmt, rel.version.c_str(), MC2_VERSION );
	bUpdateSizeShown = !install;
	if ( !install )
		return;
	int64_t bytes = 0;
	char line[256];
	if ( gos_update::downloadEstimate( &bytes ) )
	{
		cLoadString( IDS_UPDATE_SIZE, fmt, 255 );
		snprintf( line, sizeof(line), fmt, gos_update::formatSize( bytes ).c_str() );
		bUpdateSizeShown = true;
	}
	else
		cLoadString( IDS_UPDATE_SIZE_CALCULATING, line, 255 );
	const size_t len = strlen( text );
	snprintf( text + len, size - len, "\n\n%s", line );
}

// Over the earth in the bottom left corner, clear of the menu buttons
void MainMenu::updateNoticeRect( GUI_RECT& rect )
{
	const float sx = Environment.screenWidth / 800.f;
	const float sy = Environment.screenHeight / 600.f;
	rect.left = (long)(20 * sx);
	rect.right = (long)(215 * sx);
	rect.top = (long)(470 * sy);
	rect.bottom = (long)(530 * sy);
}

void MainMenu::setHostLeftDlg( const char* playerName )
{
	char leaveStr[256];
	char formatStr[256];

	cLoadString( IDS_PLAYER_LEFT, leaveStr, 255 );
	sprintf( formatStr, leaveStr, playerName );

	LogisticsOneButtonDialog::instance()->setText( IDS_PLAYER_LEFT,
					IDS_DIALOG_OK, IDS_DIALOG_OK );
	LogisticsOneButtonDialog::instance()->setText( formatStr );

	if ( MPlayer && MPlayer->playerInfo[MPlayer->commanderID].booted )
	{
		LogisticsOneButtonDialog::instance()->setText( IDS_MP_PLAYER_KICKED,
					IDS_DIALOG_OK, IDS_DIALOG_OK );

	}
	LogisticsOneButtonDialog::instance()->begin();
	bHostLeftDlg = true;

}




//*************************************************************************************************
// end of file ( MainMenu.cpp )
