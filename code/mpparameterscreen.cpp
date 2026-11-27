
#define MPPARAMETERSCREEN_CPP
/*************************************************************************************************\
MPParameterScreen.cpp			: Implementation of the MPParameterScreen component.
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//
\*************************************************************************************************/

#include"mpparameterscreen.h"
#include"prefs.h"
#include"inifile.h"
#include"userinput.h"
#include "../resource.h"
#include "lanaddresses.h"
#include"assert.h"
#include"multplyr.h"
#include "net/gos_net.h"
#include"mechbayscreen.h"
#include"logisticsdata.h"
#include"comndr.h"
#include"missionbriefingscreen.h"
#include"chatwindow.h"

extern bool quitGame;

#ifndef GAMESOUND_H
#include"gamesound.h"
#endif

#define	MP_PLAYER_PREFS 120
#define MAP_INFO		121


#define MP_LOAD_MAP 101
#define CHECK_BUTTON 200
#define MP_INCREMENT_DROPWEIGHT 198
#define MP_DECREMENT_DROPWEIGHT 199
#define MP_INCREMENT_CBILLS 200
#define MP_DECREMENT_CBILLS 201
#define MP_INCREMENT_TIME 202
#define MP_DECREMENT_TIME 203
#define MP_INCREMENT_RP 204
#define MP_DECREMENT_RP 205



#define MP_AIRSTRIKES 206
#define MP_SENSOR_PROBE 207
#define MP_MINELAYER	208
#define MP_REPAIR		209
#define MP_LOCKGAME		210
#define MP_BOOTPLAYER	211
#define MP_SCOUTS		212
#define MP_RECOVERY		213
#define MP_GUARDTOWER	214
#define MP_RESOURCE_BLDG	215
#define MP_UNLIMITED_AMMO	216
#define MP_VARIANTS			217
#define MP_ALLTECH			218
#define MP_LEGACY_QUICKSTART		219
#define MP_RP_FORMMECHS		220
#define MP_PREPARE_LOADOUT 221

#define MP_INCREMENT_PLAYER_CBILLS 250
#define MP_DECREMENT_PLAYER_CBILLS 251


static const int FIRST_BUTTON_ID = 1000010;
static const int OK_BUTTON_ID = 1000001;
static const int CANCEL_BUTTON_ID = 1000002;

MPParameterScreen* MPParameterScreen::s_instance = NULL;


GUID NO_VERSION_GUID = {
	0,0,0,0,0,0,0,0,0,0,0
};


const char* pPurchaseFiles[5] = 
{
	"purchase_Steiner",
	"purchase_Davion",
	"purchase_Liao",
	"purchase_Clan",
	"purchase_All"
};

MPParameterScreen::MPParameterScreen()
{
	status = RUNNING;
	helpTextArrayID = 12;
	bMapInfoDlg = 0;
	bShowNoMapDlg = 0;
	bBootDlg = 0;
	bErrorDlg = 0;
	bHostLeftDlg = 0;
	bDisconnectDlg = 0;

	chatToSend = IDS_MP_PARAMETER_NO_MAP_CHAT;

	s_instance = this;
}

MPParameterScreen::~MPParameterScreen()
{
	//ONLY destroy the 0 member.  All of the other were set equal which means deleting their pointers will crash!!
	// Well DONE.
	//
	playerParameters[0].factionDropList.destroy();
	playerParameters[0].teamNumberDropList.destroy();

	for (int i=1;i<MAX_MC_PLAYERS;i++)
	{
		playerParameters[i].factionDropList.specialDestroy();
		playerParameters[i].teamNumberDropList.specialDestroy();
	}
}

int MPParameterScreen::indexOfButtonWithID(int id)
{
	int i;
	for (i = 0; i < buttonCount; i++)
	{
		if (buttons[i].getID() == id)
		{
			return i;
		}
	}
	return -1;
}

void MPParameterScreen::init(FitIniFile* file)
{
	LogisticsScreen::init( *file, "Static", "Text", "Rect", "Button" );

	if ( buttonCount )
	{
		for ( int i = 0; i < buttonCount; i++ )
		{
			long id = buttons[i].getID();
			if ( (id < MP_INCREMENT_DROPWEIGHT || id > MP_DECREMENT_RP)
				 && id != MP_LOCKGAME && id != MP_BOOTPLAYER ) 
				buttons[i].setMessageOnRelease();
			if (buttons[i].getID() == 0)
			{
				buttons[i].setID(FIRST_BUTTON_ID + i);
			}

			if ( id != MB_MSG_NEXT && id != MB_MSG_PREV
				&& id != MP_PLAYER_PREFS && id != MB_MSG_MAINMENU )
			{
				buttons[i].setPressFX( LOG_VIDEOBUTTONS );
				buttons[i].setHighlightFX( LOG_DIGITALHIGHLIGHT );
				buttons[i].setDisabledFX( LOG_WRONGBUTTON );
			}
		}
	}

    auto* start = static_cast<aAnimButton*>(getButton(MB_MSG_NEXT));
    prepareButton = *start;
    prepareButton.setID(MP_PREPARE_LOADOUT);
    prepareButton.setText(IDS_MP_PREPARE_LOADOUT);
    prepareButton.setTextOutline(false);
    // The original label made room for a right-side arrow.
    prepareButton.setTextOffset(8, -2);
    prepareButton.setMessageOnRelease();
    addChild(&prepareButton);

    // The original art defines the Quick Start row and its outline separately.
    if (auto* legacyQuickStart = getButton(MP_LEGACY_QUICKSTART))
        legacyQuickStart->showGUIWindow(false);

    start->move(-79, -77);
    start->resize(260, 35);
    for (int i = 0; i < rectCount; ++i)
    {
        auto& row = rects[i];
        if (row.getHelpID() != IDS_MP_PARAMETERS_QUICKSTART_HELP)
            continue;

        start->move(row.globalLeft() - start->globalLeft(), row.globalTop() - start->globalTop());
        start->resize(row.width(), row.height());
        row.setHelpID(0);
        row.showGUIWindow(false);
        break;
    }
    lobbyActionFont.init(IDS_MP_PARAM_QUICKSTART_FONT);
    hoverLabelFont.init(IDS_CHAT_FONT);

	int curY = 0;
	playerParameters[0].init( file, "PlayerParams" );

	for ( int i = 1; i < MAX_MC_PLAYERS; i++ )
	{
		playerParameters[i] = playerParameters[0];
		curY = i * (playerParameters[i].bottom() - playerParameters[i].top() + 5);
		playerParameters[i].move( 0, curY );
		
	}

	FullPathFileName path;
	path.init( artPath, "mcl_mp_loadmap", ".fit" );

	FitIniFile mpFile;

	if ( NO_ERR != mpFile.open( path ) )
	{
		char error[256];
		sprintf( error, "couldn't open file %s", (const char*)path );
		Assert( 0, 0, error );
		return;		
	}

	mpLoadMap.init( &mpFile );
	mapInfoDlg.init();

	for (int i = 0; i < editCount; i++ )
	{
		edits[i].setNumeric( true );
		edits[i].allowIME( false );
	}

	edits[0].limitEntry( 4 );
	edits[1].limitEntry( 3 );
	edits[2].limitEntry( 2 );
	edits[3].limitEntry( 6 );



}

void MPParameterScreen::begin()
{

	fadeOutTime = 0.f;
	fadeTime = 0.f;
	MPlayer->setMode(MULTIPLAYER_MODE_PARAMETERS);
	playerCount = 0;
	bLoading = 0;
	bMapInfoDlg = 0;
	bShowNoMapDlg  = 0;
	bBootDlg = 0;
	bErrorDlg = 0;
	bDisconnectDlg = 0;

    const bool returningFromPreparation = status == PREPAREMULTIPLAYER;
    if (!returningFromPreparation)
    {
        hasPreparation = false;
        MPlayer->initParametersScreen();
    }

	if ( MPlayer->isHost() )
	{
		// A new room starts with the first multiplayer map instead of none.
		if ( !strlen( MPlayer->missionSettings.map ) )
		{
			FullPathFileName defaultMap;
			defaultMap.init( missionPath, "mc2_m01", ".fit" );
			if ( fileExists( defaultMap ) )
				strcpy( MPlayer->missionSettings.map, "mc2_m01" );
		}
		if ( strlen( MPlayer->missionSettings.map ) )
			setMission( MPlayer->missionSettings.map, !returningFromPreparation && status != UP && status != NEXT );
	}
	else if ( strlen( MPlayer->missionSettings.map ) )
		setMissionClientOnly( MPlayer->missionSettings.map );

	delayTime = 0.f;
	status = RUNNING;

	bHostLeftDlg = 0;
	bootPlayerID = -1;

	for ( int i = 0; i < MAX_MC_PLAYERS; i++ )
	{
		playerParameters[i].insigniaName.Empty();
	}

	bWaitingToStart = 0;

}


void MPParameterScreen::end()
{
	if ( status == PREVIOUS )
		MPlayer->closeSession();
	else if ( status == GOTOSPLASH )
	{
		MPlayer->closeSession();
		delete MPlayer;
		MPlayer = NULL;
	}

	bLoading = 0;
	statics[15].setTexture( ( unsigned long)0 );

	delayTime = 0.f;
	bHostLeftDlg = 0; 
}

void MPParameterScreen::render(int xOffset, int yOffset )
{
	if ( VERSION_STATUS_UNKNOWN == MPlayer->getVersionStatus() )
	{
		if ( bErrorDlg )
		{
			LogisticsOneButtonDialog::instance()->render();
		}
		return;
	}
    auto* start = getButton(MB_MSG_NEXT);
    const bool showStart = start->isShowing();
    // Keep the original input handling and draw the start action without the button art.
    start->showGUIWindow(false);
    LogisticsScreen::render(xOffset, yOffset);
    start->showGUIWindow(showStart);

    if (showStart)
        renderLobbyAction(*start, IDS_MP_START_GAME, xOffset, yOffset);
    prepareButton.move(xOffset, yOffset);
    prepareButton.render();
    prepareButton.move(-xOffset, -yOffset);

	if ( bMapInfoDlg )
	{
		mapInfoDlg.render();
	}
	else if ( bLoading )
	{
		mpLoadMap.render( );
	}
	else if ( bShowNoMapDlg || bBootDlg || bDisconnectDlg )
	{
		LogisticsOKDialog::instance()->render();

	}
	else if ( bErrorDlg )
	{
		LogisticsOneButtonDialog::instance()->render();
	}
	else if ( !xOffset && !yOffset )
	{
		aPlayerParams* pFocus = 0;

		for ( int i = 0; i < playerCount; i++ )
		{
			if ( playerParameters[i].hasFocus() )
				pFocus = & playerParameters[i];
			
			playerParameters[i].render();
				 
		}

		if ( pFocus )
			pFocus->render();

		ChatWindow::instance()->render(xOffset, yOffset);
		renderHoverLabel();
		if ( MPlayer->isHost() )
			RenderLanAddressList( textObjects[11] );
	}

	if ( bHostLeftDlg )
	{
		LogisticsOneButtonDialog::instance()->render();
	}


}

void MPParameterScreen::render()
{
	render(0, 0);
}

// The lock and remove buttons are icons only: name them while hovered.
void MPParameterScreen::renderHoverLabel()
{
    const long x = userInput->getMouseX();
    const long y = userInput->getMouseY();
    aButton* button = nullptr;
    int label = 0;
    if (auto* lock = getButton(MP_LOCKGAME); lock && lock->isShowing() && lock->pointInside(x, y))
    {
        button = lock;
        label = lock->isPressed() ? IDS_MP_UNLOCK_GAME_LABEL : IDS_MP_LOCK_GAME_LABEL;
    }
    else if (auto* boot = getButton(MP_BOOTPLAYER); boot && boot->isShowing() && boot->pointInside(x, y))
    {
        button = boot;
        label = IDS_MP_REMOVE_PLAYER_LABEL;
    }
    if (!button)
        return;

    char text[128];
    cLoadString(label, text, sizeof(text) - 1);
    const long padding = 4;
    const long width = hoverLabelFont.width(text) + 2 * padding;
    const long height = hoverLabelFont.height() + 2 * padding;
    const long left = button->globalLeft() + (button->width() - width) / 2;
    const long top = button->globalTop() - height - 2;
    const GUI_RECT bounds = {left, top, left + width, top + height};
    drawRect(bounds, 0xe0000000);
    drawEmptyRect(bounds, 0xff1e94ff, 0xff1e94ff); // BGR orange
    // A region exactly as wide as the measured text wraps the last word.
    hoverLabelFont.render(text, left + padding, top + padding, width + 16, height - 2 * padding,
                          0xffffffff, false, 0);
}

void MPParameterScreen::renderLobbyAction(aButton& button, int labelId, int xOffset, int yOffset)
{
    const GUI_RECT bounds = {button.globalLeft() + xOffset, button.globalTop() + yOffset,
                             button.globalRight() + xOffset, button.globalBottom() + yOffset};
    const bool enabled = button.isEnabled();
    const bool hovered = enabled && xOffset == 0 && yOffset == 0 &&
                         button.pointInside(userInput->getMouseX(), userInput->getMouseY());
    const DWORD foreground = hovered ? 0xffffb14a : enabled ? 0xffff941e : 0xff858585;
    // The Load Map texture has a black center. Additive blending keeps it transparent.
    if (auto* loadMap = getButton(MP_LOAD_MAP))
        loadMap->drawAdditiveArtwork(bounds, foreground);
    else
    {
        // Untextured geometry uses BGR vertex colors.
        const DWORD border = hovered ? 0xff4ab1ff : enabled ? 0xff1e94ff : foreground;
        drawEmptyRect(bounds, border, border);
    }

    char label[64];
    cLoadString(labelId, label, sizeof(label));
    DWORD width, height;
    lobbyActionFont.getSize(width, height, label);
    const int left = bounds.left + (bounds.right - bounds.left - static_cast<int>(width)) / 2;
    const int top = bounds.top + (bounds.bottom - bounds.top - static_cast<int>(height)) / 2;
    lobbyActionFont.render(label, left, top, bounds.right - left, bounds.bottom - top, foreground, false, 0);
}

static bool canReadyForMap()
{
    FullPathFileName path;
    path.init(missionPath, MPlayer->missionSettings.map, ".fit");
    return fileExists(path) &&
        MPParameterScreen::getGUIDFromFile(MPlayer->missionSettings.map) == MPlayer->missionSettings.mapGuid;
}

bool MPParameterScreen::prepareLoadout()
{
    auto* player = MPlayer->getPlayerInfo(MPlayer->commanderID);
    if (MPlayer->missionSettings.inProgress || !canReadyForMap())
        return false;

    const int faction = player->faction >= 0 && player->faction < 4 ? player->faction : 0;
    const auto revision = MPlayer->missionSettings.preparationRevision;
    if (!hasPreparation || preparationRevision != revision ||
        preparationBudget != player->cBills || preparationFaction != faction)
    {
        LogisticsData::instance->setPurchaseFile(pPurchaseFiles[MPlayer->missionSettings.allTech ? 4 : faction]);
        if (LogisticsData::instance->setCurrentMission(MPlayer->missionSettings.map) != NO_ERR)
            return false;
        preparationRevision = revision;
        preparationBudget = player->cBills;
        preparationFaction = faction;
        hasPreparation = true;
    }
    cancelPreparation();
    return true;
}

void MPParameterScreen::cancelPreparation()
{
    auto* player = MPlayer->getPlayerInfo(MPlayer->commanderID);
    if (!player->ready && !player->loadoutCost && !player->preparedRevision)
        return;

    player->ready = false;
    player->loadoutCost = 0;
    player->preparedRevision = 0;
    MPlayer->sendPlayerUpdate(0, 5, MPlayer->commanderID);
}

void MPParameterScreen::completePreparation()
{
    auto* player = MPlayer->getPlayerInfo(MPlayer->commanderID);
    const int remaining = LogisticsData::instance->getCBills();
    const int faction = player->faction >= 0 && player->faction < 4 ? player->faction : 0;
    player->ready = hasPreparation && preparationRevision == MPlayer->missionSettings.preparationRevision
        && preparationBudget == player->cBills && preparationFaction == faction
        && remaining >= 0 && remaining <= preparationBudget
        && LogisticsData::instance->getCurrentDropWeight() <= MPlayer->missionSettings.dropWeight;
    player->preparedRevision = preparationRevision;
    player->loadoutCost = player->ready ? preparationBudget - remaining : 0;
    MPlayer->sendPlayerUpdate(0, 5, MPlayer->commanderID);
}

bool MPParameterScreen::canStartGame()
{
    if (!MPlayer->isHost() || !MPlayer->allPlayersReady() ||
        !MPlayer->missionSettings.map[0] || MPlayer->missionSettings.dropWeight < 30)
        return false;

    long count = 0;
    const auto* players = MPlayer->getPlayers(count);
    if (count < 2 || count > MPlayer->missionSettings.maxPlayers)
        return false;
    for (long i = 1; i < count; ++i)
        if (players[i].team != players[0].team)
            return true;
    return false;
}

int	MPParameterScreen::handleMessage( unsigned long message, unsigned long who)
{
	if ( RUNNING == status )
	{

		switch ( who )
		{
		case 57/*MB_MSG_MAINMENU*/:
			{
				getButton( 57/*MB_MSG_MAINMENU*/ )->press( 0 );
				if (MPlayer->commanderID > -1) {
					MC2Player* pInfo = MPlayer->getPlayerInfo( MPlayer->commanderID );
					if ( pInfo->ready )
						pInfo->ready = 0;
					if (!MPlayer->isHost())
					{
						MPlayer->sendPlayerUpdate( 0, 5, MPlayer->commanderID );
					}
				}
				status = MAINMENU;
			}
			break;
		case 51/*MB_MSG_PREV*/:
			{
				getButton( 51/*MB_MSG_PREV*/ )->press( 0 );
				LogisticsOKDialog::instance()->setText( IDS_PROMPT_TO_DISCONNECT, IDS_DIALOG_NO, IDS_DIALOG_YES );
				LogisticsOKDialog::instance()->begin();
				bDisconnectDlg = true;

			}
			break;
		case 50/*MB_MSG_NEXT*/:
			{
                auto* next = getButton(MB_MSG_NEXT);
                next->press(false);
                if (!next->isEnabled() || delayTime || !canStartGame())
                    break;
                MPlayer->setInProgress(true);
                delayTime = frameLength;
				char text[256];
				cLoadString( IDS_MPLAYER_GAME_ABOUT_TO_START, text, 255 );
				MPlayer->sendChat( 0, -1, text );
			}
			break;
        case MP_PREPARE_LOADOUT:
            if (prepareButton.isEnabled() && prepareLoadout())
                status = PREPAREMULTIPLAYER;
            break;

		case MP_LOAD_MAP:
			{
				//load map
				getButton( 101/*load map button*/ )->press( 0 );
				bLoading = true;
				mpLoadMap.begin();
				return 1;
			}
			break;
		case 102/*add ai player*/:
			{
				//add ai
				getButton( 102/*add ai player*/ )->press( 0 );
				status = DOWN;
				return 1;
			}
			break;

		case MAP_INFO:
			bMapInfoDlg = true;
			mapInfoDlg.setMap( MPlayer->missionSettings.map );
			mapInfoDlg.begin();
			break;

		case MP_PLAYER_PREFS:
			{

				MC2Player* pInfo = MPlayer->getPlayerInfo( MPlayer->commanderID );
				if ( pInfo->ready )
					pInfo->ready = 0;
				if (!MPlayer->isHost())
				{
					MPlayer->sendPlayerUpdate( 0, 5, MPlayer->commanderID );
				}
	
				status = UP;
			}

			break;

		case MP_LOCKGAME:
			if ( getButton( MP_LOCKGAME )->isPressed() )
			{
				MPlayer->setLocked( true );
			}
			else
				MPlayer->setLocked( false );

			break;
		case MP_BOOTPLAYER:
			{
				for( int i = 0; i < playerCount; i++ )
				{
					if ( playerParameters[i].isSelected() )
					{
						if ( MPlayer->commanderID == playerParameters[i].getCommanderID() )
						{
							soundSystem->playDigitalSample( LOG_WRONGBUTTON );
							return 0;
						}
						char str[256];
						cLoadString( IDS_MP_PARAMTERS_BOOT_PROMPT, str, 255 );
						char finalStr[256];
						for ( int j = 0; j < MAX_MC_PLAYERS; j++ )
						{
							if ( MPlayer->playerList[j].commanderID 
								== playerParameters[i].getCommanderID() )
							{
								sprintf( finalStr, str, MPlayer->playerList[j].name );
								LogisticsOKDialog::instance()->setText( IDS_DIALOG_OK, IDS_DIALOG_NO, IDS_DIALOG_YES );
								LogisticsOKDialog::instance()->setText( finalStr );
								LogisticsOKDialog::instance()->begin();
								bBootDlg = true;
								bootPlayerID = MPlayer->playerList[j].commanderID;
								return 0;
							}
						}
						break;						
					}
				}

				// if we got here, there was no selected player
				LogisticsOneButtonDialog::instance()->setText( IDS_MP_PARAMETERS_CANT_BOOT, IDS_DIALOG_OK, IDS_DIALOG_OK );
				LogisticsOneButtonDialog::instance()->begin();
				bErrorDlg = true;
			}

			break;

		case MP_INCREMENT_DROPWEIGHT:
			MPlayer->missionSettings.dropWeight +=10;
			MPParameterScreen::resetCheckBoxes();
			if ( MPlayer->missionSettings.dropWeight > 1200 )
				MPlayer->missionSettings.dropWeight = 1200;			
			break;
		case MP_DECREMENT_DROPWEIGHT:
			MPlayer->missionSettings.dropWeight -=10;
			MPParameterScreen::resetCheckBoxes();
			if ( MPlayer->missionSettings.dropWeight < 30 )
				MPlayer->missionSettings.dropWeight = 30;			

			break;		
		case MP_INCREMENT_CBILLS:
			MPlayer->missionSettings.defaultCBills +=5000;
			if ( MPlayer->missionSettings.defaultCBills < 50000 )
				MPlayer->missionSettings.defaultCBills = 50000;
			MPlayer->redistributeRP();

			MPParameterScreen::resetCheckBoxes();
			
			
			break;
		case MP_DECREMENT_CBILLS:
			MPlayer->missionSettings.defaultCBills -=5000;
			if ( MPlayer->missionSettings.defaultCBills < 50000 )
				MPlayer->missionSettings.defaultCBills = 50000;
			MPlayer->redistributeRP();

			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_INCREMENT_TIME:
			if ( MPlayer->missionSettings.timeLimit < 0 )
			{
				MPlayer->missionSettings.timeLimit = 0;
			}
			MPlayer->missionSettings.timeLimit += 60;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_DECREMENT_TIME:
			if ( MPlayer->missionSettings.timeLimit <= 300 )
				MPlayer->missionSettings.timeLimit = -1;
			else
				MPlayer->missionSettings.timeLimit -= 60;

			MPParameterScreen::resetCheckBoxes();
			break;

		case MP_INCREMENT_RP:
			if ( MPlayer->missionSettings.resourcePoints < 0 )
				MPlayer->missionSettings.resourcePoints = 0;

			MPlayer->missionSettings.resourcePoints += 1000;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_DECREMENT_RP:
			MPlayer->missionSettings.resourcePoints -= 1000;
			if ( MPlayer->missionSettings.resourcePoints < 0 )
				MPlayer->missionSettings.resourcePoints = 0;

			MPParameterScreen::resetCheckBoxes();
			
			break;

		case MP_AIRSTRIKES:
			MPlayer->missionSettings.airStrike ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_SENSOR_PROBE:
			MPlayer->missionSettings.sensorProbe ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_MINELAYER:
			MPlayer->missionSettings.mineLayer ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_REPAIR:
			MPlayer->missionSettings.repairVehicle ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_SCOUTS:
			MPlayer->missionSettings.scoutCopter ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_RECOVERY:
			MPlayer->missionSettings.recoveryTeam ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_GUARDTOWER:
			MPlayer->missionSettings.guardTower ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_RESOURCE_BLDG:
			MPlayer->missionSettings.resourceBuilding ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_UNLIMITED_AMMO:
			MPlayer->missionSettings.unlimitedAmmo ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_VARIANTS:
			MPlayer->missionSettings.variants ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;
		case MP_ALLTECH:
			MPlayer->missionSettings.allTech ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;

		case MP_RP_FORMMECHS:
			MPlayer->missionSettings.resourceForMechs ^= 1;
			MPParameterScreen::resetCheckBoxes();
			
			break;

		}
	}

	return 0;

}

void MPParameterScreen::initializeMap ( const char* fileName )
{
	s_instance->setMission(fileName, true);
}

void MPParameterScreen::setMission( const char* fileName, bool resetData )
{
	FullPathFileName path;
	path.init( missionPath, fileName, ".fit" );
	FitIniFile missionFile;
	
	if ( NO_ERR != missionFile.open( (char*)(const char*)path ) )
	{
		char errorStr[256];
		sprintf( errorStr, "couldn't open file %s", fileName );
		Assert( 0, 0, errorStr );
		return;
	}

	if ( resetData )
	{
		long result = 0;

		char missionName[256];
		result = missionFile.seekBlock( "MissionSettings" );
		Assert( result == NO_ERR, 0, "Coudln't find the mission settings block in the mission file" );

		bool bRes;
		result = missionFile.readIdBoolean( "MissionNameUseResourceString", bRes );
		Assert( result == NO_ERR, 0, "couldn't find the MissionNameUseResourceString" );
		if ( bRes )
		{
			unsigned long lRes;
			result = missionFile.readIdULong( "MissionNameResourceStringID", lRes );
			Assert( result == NO_ERR, 0, "couldn't find the MissionNameResourceStringID" );
			cLoadString( lRes, missionName, 255 );
		}
		else
		{
			result = missionFile.readIdString( "MissionName", missionName, 255 );
			Assert( result == NO_ERR, 0, "couldn't find the missionName" );
		}
		
		gosASSERT( strlen( missionName ) < MAXLEN_MAP_NAME );

		strcpy( MPlayer->missionSettings.map, fileName );
		strcpy( MPlayer->missionSettings.name, missionName );

			
		long readLong = 0;
		result = missionFile.readIdLong( "ResourcePoints", readLong );
		MPlayer->missionSettings.resourcePoints = readLong;
		readLong = 0;
		result = missionFile.readIdLong("AdditionalCBills", readLong );
		MPlayer->missionSettings.defaultCBills = readLong;
		if (MPlayer->isHost())
			MPlayer->redistributeRP();
		float fTmp;
		result = missionFile.readIdFloat( "DropWeightLimit", fTmp );
		MPlayer->missionSettings.dropWeight = fTmp;
		result = missionFile.readIdFloat( "TimeLimit", MPlayer->missionSettings.timeLimit );
		if ( result != NO_ERR )
			MPlayer->missionSettings.timeLimit = -1.f;
		result = missionFile.readIdBoolean( "UnlimitedAmmoEnabledDefault", MPlayer->missionSettings.unlimitedAmmo );
		result = missionFile.readIdBoolean( "NoVariantsEnabledDefault", MPlayer->missionSettings.variants );
		MPlayer->missionSettings.variants ^= 1;
		result = missionFile.readIdBoolean( "AllTechEnabledDefault", MPlayer->missionSettings.allTech );
		result = missionFile.readIdBoolean( "AirStrikesEnabledDefault", MPlayer->missionSettings.airStrike );
		result = missionFile.readIdBoolean( "ArtilleryPieceEnabledDefault", MPlayer->missionSettings.guardTower );
		result = missionFile.readIdBoolean( "RepairVehicleEnabledDefault", MPlayer->missionSettings.repairVehicle );
		result = missionFile.readIdBoolean( "SalvageCraftEnabledDefault", MPlayer->missionSettings.recoveryTeam );
		result = missionFile.readIdBoolean( "SensorProbesEnabledDefault", MPlayer->missionSettings.sensorProbe );
		result = missionFile.readIdBoolean( "ScoutCoptersEnabledDefault", MPlayer->missionSettings.scoutCopter );
		result = missionFile.readIdBoolean( "MineLayersEnabledDefault", MPlayer->missionSettings.mineLayer );
		result = missionFile.readIdBoolean( "ResourceBuildingsEnabledDefault", MPlayer->missionSettings.resourceBuilding );
		result = missionFile.readIdBoolean( "ScoutCoptersEnabledDefault", MPlayer->missionSettings.scoutCopter );
		result = missionFile.readIdBoolean( "RPsForMechsEnabledDefault", MPlayer->missionSettings.resourceForMechs );
		result = missionFile.readIdString( "DownloadURL", MPlayer->missionSettings.url, 255 );
		unsigned long lTmp;
		if ( NO_ERR == missionFile.readIdULong( "MaximumNumberOfTeams", lTmp ) )
			MPlayer->missionSettings.maxTeams = lTmp;
		else
			MPlayer->missionSettings.maxTeams = 8;

		if ( NO_ERR ==  missionFile.readIdULong( "MaximumNumberOfPlayers", lTmp ) )
			MPlayer->missionSettings.maxPlayers = lTmp;
		else
			MPlayer->missionSettings.maxPlayers = 8;
		
		unsigned long tmp;
		result = missionFile.readIdULong( "MissionType", tmp );
			MPlayer->missionSettings.missionType = tmp;

		// divvy up the cBills!
		

 	mapName = MPlayer->missionSettings.map;
	}

	long textureHandle = MissionBriefingScreen::getMissionTGA( fileName );
	statics[15].setTexture( textureHandle );
	statics[15].setUVs(0, 127, 127, 0 );
	statics[15].setColor( 0xffffffff );

	getButton( MAP_INFO )->disable( false );

	GUID tmpGuid = getGUIDFromFile( fileName );
	MPlayer->missionSettings.mapGuid = tmpGuid;




}

int __cdecl sortPlayers( const void* p1, const void* p2 )
{
	MC2Player* player1 = *(MC2Player**)p1;
	MC2Player* player2 = *(MC2Player**)p2;

	if ( player1->team < player2->team )
		return -1;
	else if ( player1->team > player2->team )
		return 1;
	else if ( player1->teamSeniority > player2->teamSeniority )
		return -1;

	return 1;
	
}

void MPParameterScreen::update()
{

	if ( MPlayer->commanderID < 0 )  // don't do anything until we've been initalized
		return;

	if ( VERSION_STATUS_UNKNOWN == MPlayer->getVersionStatus() )
	{
		return;
	}
	else if ( VERSION_STATUS_BAD == MPlayer->getVersionStatus() )
	{
		if ( bErrorDlg )
		{
			LogisticsOneButtonDialog::instance()->update();
			if ( LogisticsOneButtonDialog::instance()->isDone() )
			{
				LogisticsOneButtonDialog::instance()->end();
				bHostLeftDlg = 0;
				bErrorDlg = 0;

				// might need to kill the game here
				quitGame = true;
			}

		}
		else
		{
			LogisticsOneButtonDialog::instance()->setText( IDS_MP_CONNECTION_ERROR_WRONG_VERSION,
				IDS_DIALOG_OK, IDS_DIALOG_OK );
			LogisticsOneButtonDialog::instance()->begin();
			bErrorDlg = true;
		}
  
		return;

	}	
	
	char text[256];
	
	// game not front and center at the top of the screen
	textObjects[0].setText( MPlayer->sessionName );
    // sebi: !MPlayer->sessionName - always false
	//if ( !MPlayer->sessionName || !strlen( MPlayer->sessionName ) )
	if ( !strlen( MPlayer->sessionName ) )
	{
		if ( MPlayer->isHost() )
			textObjects[0].setText( IDS_STRING26150 );
		else
			textObjects[0].setText( IDS_WAITHOST2 );
	}

	// hack for some reason we don't get here from the zone
	userInput->mouseOn();
	userInput->setMouseCursor( mState_NORMAL );

	//Yet Another hack.  FadeOutscreen will overwriting us if we were booted and rejoined.
	// If our status is RUNNING, reset the FadeOutTime.
	if (status != GOTOSPLASH)
		fadeOutTime = 0.0f;

    auto* start = getButton(MB_MSG_NEXT);
    start->setText(IDS_MP_START_GAME);
    start->showGUIWindow(MPlayer->isHost());
    start->disable(delayTime > 0.0f || !canStartGame());
    prepareButton.disable(MPlayer->missionSettings.inProgress || !canReadyForMap());

	char str[256];
	cLoadString( IDS_MP_PARAM_HOST_IP, str, 255 );
	char IPAddressStr[256];
	// The host lists every LAN address it has (one may be a VPN or a
	// virtual adapter); clients show the address they joined.
	if ( MPlayer->isHost() )
		snprintf( IPAddressStr, sizeof(IPAddressStr), str, LanAddressSummary().c_str() );
	else
		snprintf( IPAddressStr, sizeof(IPAddressStr), str, MPlayer->sessionIPAddress );
	textObjects[11].setText( IPAddressStr );

	if ( !bWaitingToStart && MPlayer->missionSettings.inProgress )
	{
		bWaitingToStart = true;
		soundSystem->playBettySample( BETTY_DEPLOY_MSG );
	}
	else if ( !MPlayer->missionSettings.inProgress )
	{
		bWaitingToStart = false;
	}

	if ( delayTime )
		delayTime += frameLength;

	if ( delayTime > 5.f )
	{
		if (canStartGame())
		{

			getButton( 50/*MB_MSG_NEXT*/ )->press( 0 );
			MPlayer->sendMissionSetup(0, 6, NULL);

			status = NEXT;
			
		}
		else {
			if (MPlayer->isHost())
				MPlayer->setInProgress(false);
			delayTime = 0.f;
			bWaitingToStart = 0;
		}
	}

	if ( MPlayer )
	{
		FullPathFileName path;
		path.init( missionPath, MPlayer->missionSettings.map, ".pak" );
		if ( !fileExists( path ) ) // disable the ready button if the map isn't around...
		{
			getButton( MAP_INFO )->disable( true );
		}
		else
		{
			getButton( MAP_INFO )->disable( false );
			if ( !statics[15].getColor() )
			{
				long textureHandle = MissionBriefingScreen::getMissionTGA( MPlayer->missionSettings.map );
				if ( textureHandle )
				{
					statics[15].setTexture( textureHandle );
					statics[15].setUVs( 0, 127, 127, 0 );
					statics[15].setColor( 0xffffffff );
				}
			}
		}
	}


	// load up my own icon and send out to everyone else....
/*	MC2Player* pPlayer = MPlayer->getPlayerInfo( MPlayer->commanderID );

	if ( pPlayer && !MPlayer->insigniaList[MPlayer->commanderID])
	{

		FullPathFileName path;
		path.init( "data\\multiplayer\\insignia\\", pPlayer->insigniaFile, ".tga" );

		File file;
		if ( NO_ERR == file.open( path ) )
		{
			long size = file.getLength();

			unsigned char* pData = new unsigned char[size];

			file.read( pData, size );
			MPlayer->sendPlayerInsignia( (char*)pPlayer->insigniaFile, pData, size );
			MPlayer->insigniaList[MPlayer->commanderID] = 1;
		}
	}*/


	if ( MPlayer->launchedFromLobby )
		getButton( MB_MSG_PREV )->disable( true );
	else
		getButton( MB_MSG_PREV )->disable( false );

	if ( bMapInfoDlg )
	{
		mapInfoDlg.update();
		if ( mapInfoDlg.isDone() )
		{
			bMapInfoDlg = 0;
		}
	}
	else if ( bHostLeftDlg || bErrorDlg )
	{
		LogisticsOneButtonDialog::instance()->update();
		if ( LogisticsOneButtonDialog::instance()->isDone() )
		{
			LogisticsOneButtonDialog::instance()->end();
			bHostLeftDlg = 0;
			bErrorDlg = 0;
		}
	}
	else if ( bShowNoMapDlg || bBootDlg || bDisconnectDlg)
	{
		LogisticsOKDialog::instance()->update();
		if ( LogisticsOKDialog::instance()->isDone() )
		{
			if ( LogisticsOKDialog::instance()->getStatus() == YES )
			{
				if ( bShowNoMapDlg )
					MPlayer->launchBrowser( MPlayer->missionSettings.url );	
				else if ( bBootDlg )
				{
					MC2Player* player= MPlayer->getPlayerInfo( bootPlayerID );
					MPlayer->bootPlayer( player->player );
				}
				else if ( bDisconnectDlg )
				{
					MPlayer->closeSession();
					status = PREVIOUS;
				}
			}

			LogisticsOKDialog::instance()->end();
			if ( bShowNoMapDlg )
			{
				mapName = MPlayer->missionSettings.map;
				

				char chatStr[256];
				char final[1024];

				cLoadString( chatToSend, chatStr, 255 );
				sprintf( final, chatStr, prefs.playerName[0] );
				MPlayer->sendChat( 0, -1, final );
			}
				bShowNoMapDlg = false;
				bBootDlg = false;
				bDisconnectDlg = false;
		}
		return;
	}


	else if ( bLoading )
	{
		mpLoadMap.update();
		if ( mpLoadMap.isDone() )
		{
			if ( mpLoadMap.getStatus() == YES )
			{
				 // need to pull in this map information....
				const char* pName = mpLoadMap.getMapFileName();
				LogisticsData::instance->setCurrentMission( pName );

				// now I need to update the other people....
				setMission(pName);

				resetCheckBoxes();
				
			}
			bLoading = false;
		}
		return;
	}
	else
	{	
		bool bEditHasFocus = 0;

		// disable necessary stuff
		if ( !MPlayer->isHost() )
		{
			if ( MPlayer->missionSettings.mapGuid == NO_VERSION_GUID )
			{
				checkVersionClientOnly( MPlayer->missionSettings.map );
			}
			if ( MPlayer->playerInfo[MPlayer->commanderID].leftSession ) // I've been booted!
			{
				if ( status != GOTOSPLASH )
				{
					status = GOTOSPLASH;
					beginFadeOut( .5 );
				}
				return;
			}
			for ( int i = MP_INCREMENT_DROPWEIGHT; i < MP_DECREMENT_RP+1; i++ )
			{
				aButton* pButton = getButton( i );
				if ( pButton )
				{
					pButton->showGUIWindow( 0 );
				}
			}

			for (int i = MP_DECREMENT_CBILLS+1; i < MP_RP_FORMMECHS+1; i++ )
			{
				aButton* pButton = getButton( i );
				if ( pButton )
				{
					pButton->disable( true );
				}
			}

			for (int i = 0; i < 3; i++ )
				edits[i].setReadOnly( true );

			getButton( 101/*load map button*/ )->showGUIWindow( false );
			getButton( MP_LOCKGAME )->showGUIWindow( false );
			getButton( MP_BOOTPLAYER )->showGUIWindow( false );

			if ( MPlayer->startLoading )
			{
				status = NEXT;
				return;
			}

			if ( mapName != MPlayer->missionSettings.map 
				&& strlen( MPlayer->missionSettings.map )) // check for new map
			{
				//
				setMissionClientOnly( MPlayer->missionSettings.map );
			}
		
		
		}
		else
		{

			// make sure King of the hill missions have a time limit
			if ( MPlayer->missionSettings.timeLimit  < 60 &&
				(MPlayer->missionSettings.missionType == MISSION_TYPE_KING_OF_THE_HILL
				|| MPlayer->missionSettings.missionType == MISSION_TYPE_LAST_MAN_ON_THE_HILL 
				|| MPlayer->missionSettings.missionType == MISSION_TYPE_TERRITORIES) )
			{
				MPlayer->missionSettings.timeLimit = 300;
			}
			if ( MPlayer->missionSettings.dropWeight < 30 )
			{
				MPlayer->missionSettings.dropWeight = 30;
			}
			if ( MPlayer->missionSettings.defaultCBills < 50000 )
			{
				MPlayer->missionSettings.defaultCBills = 50000;
				MPlayer->redistributeRP();

			}
			for ( int i = MP_INCREMENT_DROPWEIGHT; i < MP_RP_FORMMECHS+1; i++ )
			{
				aButton* pButton = getButton( i );
				if ( pButton )
				{
					pButton->disable( false );
					pButton->showGUIWindow(i != MP_LEGACY_QUICKSTART);
				}
			}

			for (int i = 0; i < 3; i++ )
				edits[i].setReadOnly( false );


			// see if they clicked the launch button and there are too many players
			if ( playerCount > MPlayer->missionSettings.maxPlayers )
			{
				if ( userInput->isLeftClick() &&
					getButton( MB_MSG_NEXT )->pointInside( userInput->getMouseX(), userInput->getMouseY() ) )
				{
					char errorStr[256];
					LogisticsOneButtonDialog::instance()->setText( IDS_PLAYER_LEFT,
						IDS_DIALOG_OK, IDS_DIALOG_OK );

					cLoadString( IDS_MP_PARAM_ERROR_TOO_MANY_PLAYERS, errorStr, 255 );
					LogisticsOneButtonDialog::instance()->setText( errorStr );
					LogisticsOneButtonDialog::instance()->begin();
					bErrorDlg = true;

				}
			}


			getButton( 101/*load map button*/ )->showGUIWindow( true );

			//now check for specifics...
			bool bDisable = MPlayer->missionSettings.timeLimit <= 0;
			getButton( MP_DECREMENT_TIME )->disable( bDisable );
			bDisable = MPlayer->missionSettings.timeLimit >= 3600.f;
			getButton( MP_INCREMENT_TIME )->disable( bDisable );

			bDisable = MPlayer->missionSettings.resourcePoints <= 0;
			getButton( MP_DECREMENT_RP )->disable( bDisable );
			bDisable = MPlayer->missionSettings.resourcePoints >= 99900.f;
			getButton( MP_INCREMENT_RP )->disable( bDisable );

			
			bDisable = MPlayer->missionSettings.defaultCBills <= 50000;
			getButton( MP_DECREMENT_CBILLS )->disable( bDisable );
			bDisable = MPlayer->missionSettings.defaultCBills >= 999000.f;
			getButton( MP_INCREMENT_CBILLS )->disable( bDisable );

			bDisable = MPlayer->missionSettings.dropWeight <= 30;
			getButton( MP_DECREMENT_DROPWEIGHT )->disable( bDisable );
			bDisable = MPlayer->missionSettings.dropWeight >= 1200.f;
			getButton( MP_INCREMENT_DROPWEIGHT )->disable( bDisable );

			getButton( MP_LOCKGAME )->showGUIWindow( true );
			getButton( MP_BOOTPLAYER )->showGUIWindow( true );
			getButton( MP_BOOTPLAYER )->disable( false );



			for(int i = 0; i < playerCount; i++ )
			{
				if ( playerParameters[i].hasFocus() )
				{
					if ( playerParameters[i].getCommanderID() == MPlayer->commanderID )	
						getButton( MP_BOOTPLAYER )->disable( true );
				}
			}
		
			
		}
		
		int oldEditFocus = -1;
		for ( int i = 0; i < editCount; i++ )
		{
			if ( edits[i].hasFocus() )
				oldEditFocus = i;
		}
		if ( !ChatWindow::instance()->pointInside(userInput->getMouseX(), userInput->getMouseY()) )
		{
			LogisticsScreen::update();
            prepareButton.update();
            if (status == PREPAREMULTIPLAYER)
                return;
		}

		if ( oldEditFocus != -1 )
		{
			bEditHasFocus = true;
			if ( !edits[oldEditFocus].hasFocus() )
			{
				EString text;
				edits[oldEditFocus].getEntry( text );
				long val = atoi( text );
				switch (oldEditFocus)
				{
					case 0:
						MPlayer->missionSettings.dropWeight = val;
						MPParameterScreen::resetCheckBoxes();
						break;
					case 1:
						MPlayer->missionSettings.defaultCBills = val * 1000;
						if (MPlayer->isHost())
							MPlayer->redistributeRP();
						MPParameterScreen::resetCheckBoxes();
						
						break;
					case 2:
						MPlayer->missionSettings.timeLimit = val * 60;
						MPParameterScreen::resetCheckBoxes();
						break;
					case 3:
						MPlayer->missionSettings.resourcePoints = val;
						MPParameterScreen::resetCheckBoxes();
						break;
				}
			}
		}

		
	/// now make all enabled so they draw correctly...
		for (int i = MP_AIRSTRIKES; i < MP_RP_FORMMECHS+1; i++ )
		{
			aButton* pButton = getButton( i );
			if ( pButton )
			{
				pButton->disable( false );
			}
		}

		if ( MPlayer )
		{
			int oldPlayerCount = playerCount;
			const MC2Player* players = MPlayer->getPlayers(playerCount);
			
			if ( oldPlayerCount != playerCount )
			{
				// new player... need to redistribute rp
				int maxCommander = -1;
				int teamID = -1;
				for( int i = 0; i < playerCount; i++ )
				{
					if ( players[i].commanderID > maxCommander )
					{
						maxCommander = players[i].commanderID;
						teamID = players[i].team;
					}
				}
			}
			
			const MC2Player* sortedPlayers[MAX_MC_PLAYERS];
			for ( int i = 0; i < playerCount; i++ )
			{
				sortedPlayers[i] = &players[i];
			}

			qsort( sortedPlayers, playerCount, sizeof( MC2Player* ), sortPlayers );

			for(int i = 0; i < playerCount; i++ )
			{
				playerParameters[i].setData( sortedPlayers[i] );
			}
			for(int i = 0; i < playerCount; i++ )
			{
                if (!ChatWindow::instance()->pointInside(userInput->getMouseX(), userInput->getMouseY()))
                    playerParameters[i].update();

				if ( playerParameters[i].hasFocus() )
					bEditHasFocus = true;


			}


			bool bPress = MPlayer->missionSettings.airStrike;
			getButton( MP_AIRSTRIKES )->press( bPress );

			bPress = MPlayer->missionSettings.sensorProbe;
			getButton( MP_SENSOR_PROBE )->press( bPress );

			bPress = MPlayer->missionSettings.mineLayer;
			getButton( MP_MINELAYER )->press( bPress );

			bPress = MPlayer->missionSettings.repairVehicle;
			getButton( MP_REPAIR )->press( bPress );

			bPress = MPlayer->missionSettings.scoutCopter;
			getButton( MP_SCOUTS )->press( bPress );

			bPress = MPlayer->missionSettings.recoveryTeam;
			getButton( MP_RECOVERY )->press( bPress );

			bPress = MPlayer->missionSettings.guardTower;
			getButton( MP_GUARDTOWER )->press( bPress );

			bPress = MPlayer->missionSettings.resourceBuilding;
			getButton( MP_RESOURCE_BLDG )->press( bPress );

			bPress = MPlayer->missionSettings.unlimitedAmmo;
			getButton( MP_UNLIMITED_AMMO )->press( bPress );

			bPress = !MPlayer->missionSettings.variants;
			getButton( MP_VARIANTS )->press( bPress );

			bPress = MPlayer->missionSettings.allTech;
			getButton( MP_ALLTECH )->press( bPress );

			bPress = MPlayer->missionSettings.resourceForMechs;
			getButton( MP_RP_FORMMECHS )->press( bPress );

			bPress = MPlayer->missionSettings.locked;
			getButton( MP_LOCKGAME )->press( bPress );

			if ( !strlen(MPlayer->missionSettings.map) )
			{
				getButton( MAP_INFO )->disable( true );
			}
//			else
//				getButton( MAP_INFO )->disable( false );

	
			textObjects[1].setText( MPlayer->missionSettings.name );

			
//			cLoadString( IDS_MP_LM_MAP_LIST_TYPE, tmp, 255 );
//			cLoadString( IDS_MP_LM_TYPE0 + MPlayer->missionSettings.missionType, tmp2, 127 );
//			sprintf( text, tmp, tmp2 );
//			textObjects[7].setText( text );
			sprintf( text, "%dk", MPlayer->missionSettings.defaultCBills/1000 );
			if ( !edits[1].hasFocus() )
				edits[1].setEntry( text  );
			sprintf( text, "%d", MPlayer->missionSettings.resourcePoints );
			if ( !edits[3].hasFocus() )
				edits[3].setEntry( text );
			sprintf( text, "%.0lf", fabs(MPlayer->missionSettings.timeLimit/60.f) );
			if ( !edits[2].hasFocus() )
			{
				if ( MPlayer->missionSettings.timeLimit/60.f <= 0 )
					cLoadString( IDS_MP_PARAM_UNLIMITED, text, 255 );
				edits[2].setEntry( text );
			}

			sprintf( text, "%d", MPlayer->missionSettings.dropWeight );
			if ( !edits[0].hasFocus() )
				edits[0].setEntry( text );

            if (MPlayer->isHost())
            {
                const bool bReady = !bEditHasFocus && canStartGame();
                getButton(MB_MSG_NEXT)->disable(delayTime > 0.0f || !bReady);
                if (!bReady && delayTime)
                {
                    char text[256];
                    cLoadString(IDS_MP_LAUNCH_ABORTED, text, 255);
                    MPlayer->sendChat(0, -1, text);
                    delayTime = 0.f;
                    bWaitingToStart = false;
                    MPlayer->setInProgress(false);
                }
            }

	

		}

		if ( !bEditHasFocus )
			ChatWindow::instance()->update();

	}

	helpTextID = 0;
	helpTextHeaderID = 0;



	/*
	for ( int i = 0; i < buttonCount; i++ )
	{
		buttons[i].update();
		if ( buttons[i].pointInside( userInput->getMouseX(), userInput->getMouseY() )
			&& userInput->isLeftClick() )
		{
			handleMessage( buttons[i].getID(), buttons[i].getID() );
		}

	}
	*/
}

GUID			MPParameterScreen::getGUIDFromFile( const char* pNewMapName)
{
	GUID retVal;
	memset( &retVal, 0xff, sizeof( GUID ) );
	FullPathFileName path;
	path.init( missionPath, pNewMapName, ".pak" );
	PacketFile pakFile;
	if ( NO_ERR != pakFile.open( (char*)(const char*)path ) )
	{
		return retVal;
	}

	int packetCount = pakFile.getNumPackets();
	pakFile.seekPacket( packetCount - 1 );
	if ( sizeof( GUID ) == pakFile.getPacketSize( ) ) 
	{
		pakFile.readPacket( packetCount - 1, (unsigned char*)&retVal );

		return retVal;
	}
	else
	{
		memset( &retVal, 0, sizeof( GUID ) );
		return retVal;
	}

}

void MPParameterScreen::setMissionClientOnly( const char* pNewMapName )
{
	FullPathFileName path;
	path.init( missionPath, pNewMapName, ".fit" );
	FitIniFile missionFile;
	
	if ( NO_ERR != missionFile.open( (char*)(const char*)path ) )
	{
		char tmp[256];
		char final[1024];
		cLoadString( IDS_MP_PARAM_NO_MAP, tmp, 255 );
		sprintf( final, tmp, MPlayer->missionSettings.map, MPlayer->missionSettings.url );
		LogisticsOKDialog::instance()->setFont(IDS_MP_PARAM_NO_MAP_FONT);
		LogisticsOKDialog::instance()->setText( final );
		LogisticsOKDialog::instance()->begin();
		bShowNoMapDlg = true;
		getButton( MAP_INFO )->disable( true );

		chatToSend = IDS_MP_PARAMETER_NO_MAP_CHAT;
		statics[15].setColor( 0 );
		return;
	}

	checkVersionClientOnly( pNewMapName );


	getButton( MAP_INFO )->disable( false );

	long textureHandle = MissionBriefingScreen::getMissionTGA( pNewMapName );
	if ( textureHandle )
	{
		statics[15].setTexture( textureHandle );
		statics[15].setUVs( 0, 127, 127, 0 );
		statics[15].setColor( 0xffffffff );
	}


 	mapName = MPlayer->missionSettings.map;
}

void MPParameterScreen::checkVersionClientOnly( const char* pNewMapName )
{
	GUID version = getGUIDFromFile( pNewMapName );
	if ( MPlayer->missionSettings.mapGuid != NO_VERSION_GUID &&
		version != MPlayer->missionSettings.mapGuid && version != NO_VERSION_GUID) // if 0 it simply wasn't in the file at all, should take this out as soon as maps are resaved
	{
		char tmp[256];
		char final[1024];
		cLoadString( IDS_MP_PARAM_MAP_WRONG_VERSION, tmp, 255 );
		sprintf( final, tmp, MPlayer->missionSettings.url );
		LogisticsOKDialog::instance()->setFont(IDS_MP_PARAM_NO_MAP_FONT);
		LogisticsOKDialog::instance()->setText( final );
		LogisticsOKDialog::instance()->begin();
		bShowNoMapDlg = true;
		getButton( MAP_INFO )->disable( true );

		chatToSend = IDS_MAP_WRONG_VERSION_CHAT;
		statics[15].setColor( 0 );	

		// need to disable the ready button
		for( int i = 0; i < MAX_MC_PLAYERS; i++ )
		{
			if ( playerParameters[i].getCommanderID() == MPlayer->commanderID )
			{
				playerParameters[i].disableReadyButton();
				break;
			}
		}

	}
}



void MPParameterScreen::resetCheckBoxes()
{
    if (MPlayer->isHost())
    {
        ++MPlayer->missionSettings.preparationRevision;
        for (auto& player : MPlayer->playerInfo)
            player.ready = false;
    }
}

void MPParameterScreen::setHostLeftDlg( const char* playerName )
{
	char leaveStr[256];
	char formatStr[256];

	cLoadString( IDS_PLAYER_LEFT, leaveStr, 255 );
	sprintf( formatStr, leaveStr, playerName );

	LogisticsOneButtonDialog::instance()->setText( IDS_PLAYER_LEFT,
					IDS_DIALOG_OK, IDS_DIALOG_OK );
	LogisticsOneButtonDialog::instance()->setText( formatStr );
	LogisticsOneButtonDialog::instance()->begin();
	bHostLeftDlg = true;

}




aPlayerParams::aPlayerParams()
{
	statics = 0;
	rects = 0;
	textObjects = 0;
	staticCount = rectCount = textCount = 0;
	bHasFocus = 0;
}

aPlayerParams::~aPlayerParams()
{
	destroy();
}

aPlayerParams& aPlayerParams::operator=( const aPlayerParams& src )
{
	aObject::operator=( src );

	addChild(&ReadyButton);
	addChild( &CBillsSpinnerDownButton );
	addChild( &CBillsSpinnerUpButton );
	addChild( &edit );

	ReadyButton = src.ReadyButton;
	CBillsSpinnerDownButton = src.CBillsSpinnerDownButton;
	CBillsSpinnerUpButton = src.CBillsSpinnerUpButton;
	edit = src.edit;
	insigniaName = src.insigniaName;
	templateItem = src.templateItem;

	staticCount = src.staticCount;

	if ( staticCount )
	{
		statics = new aObject[staticCount];

		for ( int i = 0; i < staticCount; i++ )
		{
			addChild( &statics[i] );
			statics[i] = src.statics[i];
		}
	}

	rectCount = src.rectCount;
	if ( rectCount )
	{
		rects = new aRect[rectCount];

		for ( int i = 0; i < rectCount; i++ )
		{
			addChild( &rects[i] );
			rects[i] = src.rects[i];
		}
	}

	textCount = src.textCount;
	if ( textCount )
	{
		textObjects = new aText[textCount];

		for ( int i = 0; i < textCount; i++ )
		{
			addChild( &textObjects[i] );
			textObjects[i] = src.textObjects[i];
		}
	}

	addChild(&teamNumberDropList);
	teamNumberDropList = src.teamNumberDropList;

	addChild( &factionDropList );
	factionDropList = src.factionDropList;


	return *this;
}

long aPlayerParams::init(long xPos, long yPos,long w, long h )
{
	long err;
	
	err = aObject::init(xPos,yPos,w,h);
	if (err)
		return err;

	if ( !MPlayer->isHost() )
	{
		CBillsSpinnerDownButton.showGUIWindow( 0 );
		CBillsSpinnerUpButton.showGUIWindow( 0 );
	}

	
	return (NO_ERR);
}

void aPlayerParams::init( FitIniFile* pFile, const char* blockNameParam )
{

	FitIniFile &file = (*pFile);
	pFile->seekBlock(blockNameParam);

	ReadyButton.init( file, "PlayerParamsReadyButton" );
	CBillsSpinnerDownButton.init( file, "PlayerParamsCBillsSpinnerDownButton" );
	CBillsSpinnerUpButton.init( file, "PlayerParamsCBillsSpinnerUpButton" );
	addChild(&ReadyButton);
	addChild( &CBillsSpinnerDownButton );
	addChild( &CBillsSpinnerUpButton );

	edit.init( pFile, "playerparamEdit0" );
	edit.setNumeric( true );
	edit.limitEntry( 3 );
	edit.allowIME( 0 );
	addChild( &edit );


	CBillsSpinnerDownButton.setPressFX( LOG_VIDEOBUTTONS );
	CBillsSpinnerUpButton.setPressFX( LOG_VIDEOBUTTONS );
	ReadyButton.setPressFX( LOG_VIDEOBUTTONS );

	CBillsSpinnerDownButton.setHighlightFX( LOG_DIGITALHIGHLIGHT );
	CBillsSpinnerUpButton.setHighlightFX( LOG_DIGITALHIGHLIGHT );
	ReadyButton.setHighlightFX( LOG_DIGITALHIGHLIGHT );

	CBillsSpinnerDownButton.setDisabledFX( LOG_WRONGBUTTON );
	CBillsSpinnerUpButton.setDisabledFX( LOG_WRONGBUTTON );
	ReadyButton.setDisabledFX( LOG_WRONGBUTTON );


	const char* staticName = "PlayerParamsStatic";
	const char* textName = "PlayerParamsText";
	const char* rectName = "PlayerParamsRect";

	char blockName[256];

	// init statics
	if ( staticName )
	{
		sprintf( blockName, "%s%c", staticName, 's' );
		if ( NO_ERR == file.seekBlock( blockName ) )
		{
			file.readIdLong( "staticCount", staticCount );

			if ( staticCount )
			{
				statics = new aObject[staticCount];

				char blockName[128];
				for ( int i = 0; i < staticCount; i++ )
				{
					sprintf( blockName, "%s%d", staticName, i );
					statics[i].init( &file, blockName );			
					addChild(&(statics[i]));
				}
				
			}
		}
	}

	if ( rectName )
	{
		// init rects
		sprintf( blockName, "%s%c", rectName, 's' );
		if ( NO_ERR == file.seekBlock( blockName ) )
		{
			file.readIdLong( "rectCount", rectCount );
			if ( rectCount )
			{
				rects = new aRect[rectCount];

				char blockName[128];
				for ( int i = 0; i < rectCount; i++ )
				{
					sprintf( blockName, "%s%d", rectName, i );
					rects[i].init( &file, blockName );
					addChild(&(rects[i]));
				}
			}
		}
	}

	// init texts
	if ( textName )
	{
		sprintf( blockName, "%s%c", textName, 's' );
		if ( NO_ERR == file.seekBlock( blockName ) )
		{
			if ( NO_ERR != file.readIdLong( "TextEntryCount", textCount ) )
				file.readIdLong( "TextCount", textCount );

			if ( textCount )
			{
				textObjects = new aText[textCount];
				char blockName[64];
				for ( int i = 0; i < textCount; i++ )
				{
					sprintf( blockName, "%s%d", textName, i );
					textObjects[i].init( &file, blockName );
					addChild(&(textObjects[i]));
				}
				
			}
		}
	}

	
	

	{
		char path[256];
		strcpy( path, artPath );
		strcat( path, "mcl_mp_param_droplist3.fit" );
		
		FitIniFile PNfile;
		if ( NO_ERR != PNfile.open( path ) )
		{
			char error[320];
			snprintf(error, sizeof(error), "couldn't open file %s", path );
			Assert( 0, 0, error );
			return;
		}
		teamNumberDropList.init(&PNfile, "TeamNumberDropList");
		teamNumberDropList.ListBox().setOrange( true );

		PNfile.seekBlock( "Text0" );
		templateItem.init(&PNfile, "Text0" );

	
	}
	addChild(&teamNumberDropList);

	{
		char path[256];
		strcpy( path, artPath );
		strcat( path, "mcl_mp_param_droplist4.fit" );
		
		FitIniFile PNfile;
		if ( NO_ERR != PNfile.open( path ) )
		{
			char error[320];
			snprintf(error, sizeof(error), "couldn't open file %s", path );
			Assert( 0, 0, error );
			return;
		}
		factionDropList.init(&PNfile, "FactionTypeDropList");
		factionDropList.ListBox().setOrange( true );

		int i;
		for (i = 0; i < 4; i += 1)
		{
			aStyle2TextListItem *pTmp2;
			pTmp2 = new aStyle2TextListItem;
			*pTmp2 = templateItem;
			pTmp2->setText( IDS_FACTION0 + i );
			pTmp2->resize( factionDropList.width() - 
				factionDropList.ListBox().getScrollBarWidth() - 8,
				pTmp2->height() );
			pTmp2->sizeToText();
			factionDropList.AddItem(pTmp2);
		}
		factionDropList.SelectItem(0);
	}
	addChild(&factionDropList);

	// resize after we include drop lists
	if (true)
	{
		float x = 1000000.0;
		float y = 1000000.0;
		float w = 0.0;
		float h = 0.0;
		int i;
		for (i = 0; i < numberOfChildren(); i += 1)
		{
			aObject *pChild = child(i);
			if (x > pChild->globalX())
			{
				x = pChild->globalX();
			}
			if (y > pChild->globalY())
			{
				y = pChild->globalY();
			}
			if (w < pChild->globalRight() )
			{
				w = (pChild->globalRight());
			}
			if (h < pChild->globalBottom() - pChild->globalTop() )
			{
				h = (pChild->globalBottom() - pChild->globalTop());
			}
		}

		resize( w, h );
		moveToNoRecurse( x, y );
	}





}

bool aPlayerParams::hasFocus( )
{
	if ( edit.hasFocus() )
		return true;

	if ( factionDropList.ListBox().isShowing() )
		return true;

	if ( teamNumberDropList.ListBox().isShowing() )
		return true;

	return false;
}

void aPlayerParams::destroy()
{
	removeAllChildren();

	if ( statics )
		delete [] statics;

	if ( rects )
		delete [] rects;

	if ( textObjects )
		delete [] textObjects;

	statics = 0;
	rects = 0;
	textObjects = 0;

	aObject::destroy();
}

void aPlayerParams::update()
{

	if ( !MPlayer->isHost() )
	{
		CBillsSpinnerDownButton.showGUIWindow( 0 );
		CBillsSpinnerUpButton.showGUIWindow( 0 );
	}
	if ( userInput->isLeftClick() )
	{
		if ( pointInside( userInput->getMouseX(), userInput->getMouseY() ) 
			|| factionDropList.pointInside( userInput->getMouseX(), userInput->getMouseY()) 
			|| teamNumberDropList.pointInside( userInput->getMouseX(), userInput->getMouseY()))
			bHasFocus = true;
		else
			bHasFocus = false;
	}

	if ( MPlayer->missionSettings.allTech )
	{
		if ( factionDropList.ListBox().GetItemCount() != 1 )
		{
			factionDropList.SelectItem( -1 );
			factionDropList.ListBox().removeAllItems( true );
			aStyle2TextListItem *pTmp2;
			pTmp2 = new aStyle2TextListItem;
			*pTmp2 = templateItem;
			pTmp2->setText( IDS_FACTION0 + 5 );
			pTmp2->resize( factionDropList.width() - 
				factionDropList.ListBox().getScrollBarWidth() - 8,
				pTmp2->height() );
			pTmp2->sizeToText();
			factionDropList.AddItem(pTmp2);
			factionDropList.SelectItem( 0 );
		}
	}
	else if ( factionDropList.ListBox().GetItemCount() < 4 )
	{
		factionDropList.SelectItem( -1 );
		factionDropList.ListBox().removeAllItems( true );

		for ( int i = 0; i < 4; i += 1)
		{
			aStyle2TextListItem *pTmp2;
			pTmp2 = new aStyle2TextListItem;
			*pTmp2 = templateItem;
			pTmp2->setText( IDS_FACTION0 + i );
			pTmp2->resize( factionDropList.width() - 
				factionDropList.ListBox().getScrollBarWidth() - 8,
				pTmp2->height() );
			pTmp2->sizeToText();
			factionDropList.AddItem(pTmp2);
			factionDropList.SelectItem( 0 );

		}
	}

	// don't accept any kind of input for anything but your own stuff....
	if ( commanderID != MPlayer->commanderID && !MPlayer->isHost() )
	{
		CBillsSpinnerUpButton.showGUIWindow( 0 );
		CBillsSpinnerDownButton.showGUIWindow( 0 );
		teamNumberDropList.disable( true );
		factionDropList.disable( true );
		rects[2].showGUIWindow( 0 );
		rects[7].showGUIWindow( 0 );
		rects[8].showGUIWindow( 0 );
		rects[1].showGUIWindow( 0 );

		return;
	}
	else
	{
		if ( MPlayer->isHost() )
		{
			CBillsSpinnerUpButton.showGUIWindow( 1 );
			CBillsSpinnerDownButton.showGUIWindow( 1 );
		}
		else
		{
			CBillsSpinnerUpButton.showGUIWindow( 0 );
			CBillsSpinnerDownButton.showGUIWindow( 0 );
		}
		teamNumberDropList.disable( 0 );
		factionDropList.disable( 0 );
		rects[2].showGUIWindow( 1 );
		rects[7].showGUIWindow( 1 );
		rects[8].showGUIWindow( 1 );
		rects[1].showGUIWindow( 1 );


	}

	if (!bHasFocus)
        return;

		// need to check for changes
		int oldSel = teamNumberDropList.GetSelectedItem();
		int oldFaction = factionDropList.GetSelectedItem();
		const char* pText = textObjects[1].text;
		
		long oldCBills = 0;
		
		if ( pText )
		{
			oldCBills = atoi( pText ) * 1000;
		}
		

	bool bCBillsChanged = 0;
	if (!MPlayer->getPlayerInfo(commanderID)->ready || MPlayer->isHost())
	{

		if ( (commanderID == MPlayer->commanderID || MPlayer->isHost() ) )
		{
            if (MPlayer->isHost())
            {
                edit.setReadOnly(false);
                const bool hadFocus = edit.hasFocus();
                edit.update();
                bCBillsChanged = hadFocus && !edit.hasFocus();
                CBillsSpinnerDownButton.update();
                CBillsSpinnerUpButton.update();
            }
			factionDropList.update();
			teamNumberDropList.update();		
		}
	}
	else
		edit.setReadOnly( 1 );

		int newSel = teamNumberDropList.GetSelectedItem();
		int newFaction = factionDropList.GetSelectedItem();
		EString cBillsText;
		edit.getEntry( cBillsText );
		long newCBills = 0;
		if ( cBillsText.Length() )
		{
			newCBills = atoi( cBillsText ) * 1000;
		}

		if (  bCBillsChanged )
		{
		 	MC2Player* pInfo = MPlayer->getPlayerInfo( commanderID );
			pInfo->cBills = newCBills;
			MPlayer->sendPlayerUpdate( 0, 5, commanderID );
				
			char text[256];
			sprintf(text, "%d", pInfo->cBills/5000 * (5) ); // need to round to nearest 5000
			edit.setEntry( text );
			MPParameterScreen::resetCheckBoxes();
		}

		else if (oldSel != newSel || oldFaction != newFaction)
		{
			// send out new info....
			if ( MPlayer )
			{
			
				MC2Player* pInfo = MPlayer->getPlayerInfo( commanderID );
				pInfo->ready = false;
				pInfo->faction = newFaction;
				if (MPlayer->isHost())
				{
					if (pInfo->team != newSel)
						MPlayer->setPlayerTeam(commanderID, newSel);
                    MPParameterScreen::resetCheckBoxes();
				}
				else {
					pInfo->team = newSel;
					MPlayer->sendPlayerUpdate( 0, 5, commanderID );
				}

				
			}

			bHasFocus = 0;



		}

		if ( userInput->getKeyDown( KEY_RETURN ) )
			bHasFocus = false;



}

void	aPlayerParams::disableReadyButton()
{
	ReadyButton.disable( true );
}

void	aPlayerParams::setData( const _MC2Player* data)
{
	bool bDisable =  ( data->cBills <= 0 );
	CBillsSpinnerDownButton.disable( bDisable );
	bDisable = ( data->cBills >= 1000000 );
	CBillsSpinnerUpButton.disable( bDisable );

	ReadyButton.disable(false);
	if (data->ready && data->preparedRevision == MPlayer->missionSettings.preparationRevision)
	{
		ReadyButton.press(true);
	}
	else
	{
		if ( ReadyButton.isPressed() )
			soundSystem->playDigitalSample( LOG_UNREADY );
		ReadyButton.press( 0 );
	}

	commanderID = data->commanderID;


	long textColor = 0xff000000;
	long newColor = MPlayer->colors[data->baseColor[BASECOLOR_TEAM]];
	if ( ((newColor & 0xff) + ( (newColor & 0xff00)>>8 ) + ( (newColor & 0xff0000)>>16 ))/3 < 85 )
		textColor = 0xffffffff;

	if ( textObjects )
	{
		textObjects[0].setText( data->name );
		textObjects[1].setText( data->unitName );
		textObjects[0].setColor( textColor );
		textObjects[1].setColor( textColor );
		char text[256];
		sprintf(text, "%d", data->cBills/5000 * 5 );
		if ( !edit.hasFocus() )
			edit.setEntry( text );
	}

	rects[4].setColor( MPlayer->colors[data->stripeColor] );
	rects[6].setColor( MPlayer->colors[data->baseColor[BASECOLOR_TEAM]] );

	// set up the insignia...
	// I really need to store this... really don't want to allocate a texture every time
	const char* pFileName = data->insigniaFile;
	if ( pFileName != insigniaName )
	{
		FullPathFileName path;
		path.init( "data" PATH_SEPARATOR "multiplayer" PATH_SEPARATOR "insignia" PATH_SEPARATOR, data->insigniaFile, ".tga" );

		if ( fileExists( path ) )
		{
			statics[0].setTexture( path );
			statics[0].setUVs( 0, 0, 32, 32 );
			insigniaName = pFileName;
			MPlayer->insigniaList[data->commanderID] = 1;

		}
	}


	if ( !teamNumberDropList.IsExpanded() )
	{
		int oldSel = teamNumberDropList.ListBox().GetSelectedItem();
		if ( teamNumberDropList.ListBox().GetItemCount() != MPlayer->missionSettings.maxTeams )
		{
			teamNumberDropList.ListBox().removeAllItems( true );
			for ( int i = 0; i <  MPlayer->missionSettings.maxTeams; i ++ )
			{
				aStyle2TextListItem* pTmp2 = new aStyle2TextListItem;
				char tmpStr[32];
				sprintf(tmpStr, "%d", i+1);
				*pTmp2 = templateItem;
				pTmp2->setText( tmpStr );
				pTmp2->resize( teamNumberDropList.width() - 
					teamNumberDropList.ListBox().getScrollBarWidth() - 8, pTmp2->height() );
				pTmp2->sizeToText();
				teamNumberDropList.AddItem(pTmp2);
			}
			if ( oldSel < teamNumberDropList.ListBox().GetItemCount() )
				teamNumberDropList.SelectItem(oldSel);
			else
				teamNumberDropList.SelectItem(0);
		}

		teamNumberDropList.SelectItem( data->team );
	}

	if ( !factionDropList.IsExpanded() )
	{
		factionDropList.SelectItem( data->faction );
	}
	


}


int aPlayerParams::handleMessage( unsigned long message, unsigned long who )
{
	float increment = 5000;
	if ( message == aMSG_LEFTMOUSEHELD )
	{
		increment = 1000;
	}

		switch( who )
		{
		
		case MP_INCREMENT_PLAYER_CBILLS:
			{
				
			 	MC2Player* pInfo = MPlayer->getPlayerInfo( commanderID );
				pInfo->cBills += increment;
				
				char text[256];
				sprintf(text, "%d", pInfo->cBills/5000 * (5) ); // need to round to nearest 5000
				edit.setEntry( text );
				MPParameterScreen::resetCheckBoxes();
				


				break;
			}
		case MP_DECREMENT_PLAYER_CBILLS:
			{

				MC2Player* pInfo = MPlayer->getPlayerInfo( commanderID );
				pInfo->cBills -= increment;

				char text[256];
				sprintf(text, "%d", pInfo->cBills/5000 * (5) );
				edit.setEntry( text );
				MPParameterScreen::resetCheckBoxes();



			}
			break;
		}

	return 0;
}

void aPlayerParams::render()
{
	aObject::render();
}

void aPlayerParams::move( float offsetX, float offsetY )
{
	aObject::move( offsetX, offsetY );
}



long aStyle2TextListItem::init( FitIniFile* file, const char* blockName )
{
	file->seekBlock( blockName );

	long fontResID = 0;
	file->readIdLong( "Font", fontResID );
	long textID = 0;
	file->readIdLong( "TextID", textID );
	aTextListItem::init(fontResID);
	setText(textID);
	long color = 0xff808080;
	file->readIdLong( "Color", color );
	normalColor = color;
	setColor(color);

	char tmpStr[64];
	strcpy(tmpStr, "");
	file->readIdString( "Animation", tmpStr, 63 );
	if (0 == strcmp("", tmpStr))
	{
		hasAnimation = false;
	}
	else
	{
		hasAnimation = true;
		animGroup.init(file, tmpStr);
	}

	return 0;
}

void aStyle2TextListItem::render()
{
	long color = normalColor;
	if ( hasAnimation )
	{
		if ( (int)animGroup.getState() != (int)getState() ) // the two enums share their values
			animGroup.setState( (aAnimGroup::STATE)(long)getState() );
		animGroup.update();
		color = animGroup.getCurrentColor( animGroup.getState() );
	}

/*	if (aListItem::SELECTED == getState())
	{
		color = 0.33 * ((unsigned long)normalColor) + 0.67 * ((unsigned long)0xffffffff);
	}
	else if (aListItem::HIGHLITE == getState())
	{
		color = 0.67 * ((unsigned long)normalColor) + 0.33 * ((unsigned long)0xffffffff);
	}
	else
	{
		color = normalColor;
	}*/
	aTextListItem::setColor((unsigned long)color);

	aTextListItem::render();
}


CFocusManager::CFocusManager()
{
	clear();
}

void CFocusManager::clear()
{
	speciesOfTheControlWhichHasTheFocus = CS_NONE;

	pDropListThatHasTheFocus = NULL;
	listOfDropListPointers.Clear();
}

void *CFocusManager::registerDropList(aDropList &DropList)
{
	listOfDropListPointers.Append(&DropList);
	return ((void *)&DropList);
}

void CFocusManager::unregisterDropList(aDropList &DropList)
{
	listOfDropListPointers.Delete(listOfDropListPointers.Find(&DropList));
}

void CFocusManager::update()
{
	if (userInput->isLeftClick())
	{
		/*focus may have changed*/
		if (NULL != pControlThatHasTheFocus())
		{
			if (pControlThatHasTheFocus()->pointInside(userInput->getMouseX(), userInput->getMouseY()))
			{
				/*the pointer was clicked on the control that already has the focus, so the focus
				is unaffected*/
				return;
			}
		}

		/*reset focus*/
		switch (speciesOfTheControlWhichHasTheFocus)
		{
		case CS_DROPLIST:
			pDropListThatHasTheFocus = NULL;
			break;
		}
		speciesOfTheControlWhichHasTheFocus = CS_NONE;

		/*check the droplists to see the pointer was clicked in one of them*/
		CListOfDropListPointers::EIterator DropListIter = listOfDropListPointers.Begin();
		while (!DropListIter.IsDone())
		{
			if ((*DropListIter)->pointInside(userInput->getMouseX(), userInput->getMouseY()))
			{
				pDropListThatHasTheFocus = (*DropListIter);
				speciesOfTheControlWhichHasTheFocus = CS_DROPLIST;
				break;
			}
			DropListIter++;
		}

		if (CS_NONE == speciesOfTheControlWhichHasTheFocus)
		{
			/*No droplist got the focus. Check the other control species.*/
		}

		/*make sure all drop lists that do not have focus are unexpanded*/
		DropListIter = listOfDropListPointers.Begin();
		while (!DropListIter.IsDone())
		{
			if ((CS_DROPLIST != speciesOfTheControlWhichHasTheFocus)
				|| ((*DropListIter) != pDropListThatHasTheFocus))
			{
				(*DropListIter)->IsExpanded(false);
			}
			DropListIter++;
		}
	}
}

bool CFocusManager::somebodyHasTheFocus()
{
	if (CS_NONE == speciesOfTheControlWhichHasTheFocus)
	{
		return false;
	}
	else
	{
		return true;
	}
}

aObject *CFocusManager::pControlThatHasTheFocus()
{
	if (CS_NONE != speciesOfTheControlWhichHasTheFocus)
	{
		switch(speciesOfTheControlWhichHasTheFocus) 
		{
		case CS_DROPLIST:
			return pDropListThatHasTheFocus;
			break;
		}
		assert(false);
	}
	return NULL;
}


//////////////////////////////////////////////



//*************************************************************************************************
// end of file ( MPParameterScreen.cpp )
