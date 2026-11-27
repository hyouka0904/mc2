// SPDX-License-Identifier: GPL-3.0-only
#include "logisticsmenu.h"

#include "mclib.h"
#include "inifile.h"
#include "abutton.h"
#include "optionsscreenwrapper.h"
#include "mechlopedia.h"
#include "gamesound.h"
#include "../resource.h"

// Buttons taken from the main menu art (mcl_mm.fit)
#define MENU_CLOSE			94	// "Return to game"
#define MENU_OPTIONS		95
#define MENU_ENCYCLOPEDIA	96
#define MENU_MAIN			97	// "Exit"

static const long kMenuButtons[] = { MENU_OPTIONS, MENU_ENCYCLOPEDIA, MENU_MAIN, MENU_CLOSE };
static const int kMenuButtonCount = sizeof(kMenuButtons) / sizeof(kMenuButtons[0]);
static const long kButtonStep = 42;		// spacing of the main menu buttons
static const long kPanelPadding = 16;
static const DWORD kDimColor = 0x7f000000;	// the screen underneath stays visible

static bool openFit( FitIniFile& file, const char* name )
{
	FullPathFileName path;
	path.init( artPath, name, ".fit" );
	if ( NO_ERR != file.open( path ) )
	{
		char error[256];
		snprintf( error, sizeof(error), "couldn't open file %s", (const char*)path );
		Assert( 0, 0, error );
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------

void LogisticsLeaveDialog::init()
{
	FitIniFile file;
	if ( !openFit( file, "mcl_dialog" ) )
		return;
	LogisticsScreen::init( file, "Static", "Text", "Rect", "Button", "Edit" );
	file.seekBlock( "EnterAnim" );
	enterAnim.init( &file, "" );
	file.seekBlock( "LeaveAnim" );
	exitAnim.init( &file, "" );
	for ( int i = 0; i < buttonCount; i++ )
		buttons[i].setMessageOnRelease();
}

void LogisticsLeaveDialog::ask( int textID, int leftButton, int rightButton )
{
	textObjects[0].setText( textID );
	buttons[0].setText( leftButton );	// NO
	buttons[1].setText( rightButton );	// YES
	begin();
}

int LogisticsLeaveDialog::handleMessage( unsigned long, unsigned long who )
{
	if ( status != RUNNING )
		return 0;
	status = who;
	exitAnim.begin();
	enterAnim.end();
	return 0;
}

void LogisticsLeaveDialog::update()
{
	enterAnim.update();
	exitAnim.update();
	if ( exitAnim.isDone() )
		bDone = true;

	userInput->mouseOn();
	userInput->setMouseCursor( mState_NORMAL );

	if ( !enterAnim.isDone() || status != RUNNING )
		return;

	LogisticsScreen::update();
	if ( status != RUNNING )
		return;

	if ( gos_GetKeyStatus( KEY_ESCAPE ) == KEY_RELEASED
		|| ( userInput->leftMouseReleased()
			&& !inside( userInput->getMouseX(), userInput->getMouseY() )
			&& !inside( userInput->getMouseDragX(), userInput->getMouseDragY() ) ) )
		handleMessage( 0, CANCEL );
	else if ( gos_GetKeyStatus( KEY_RETURN ) == KEY_RELEASED )
		handleMessage( 0, YES );
}

//-------------------------------------------------------------------------------------------------

LogisticsMenu::LogisticsMenu()
{
	state = CLOSED;
	bCampaign = bMultiplayer = bOptions = bEncyclopedia = bAsking = false;
	options = NULL;
	encyclopedia = NULL;
	panel.left = panel.top = panel.right = panel.bottom = 0;
}

LogisticsMenu::~LogisticsMenu()
{
	delete options;
	delete encyclopedia;
}

void LogisticsMenu::init()
{
	FitIniFile file;
	if ( !openFit( file, "mcl_mm" ) )
		return;
	// only the buttons: no background art, the screen underneath shows
	LogisticsScreen::init( file, NULL, NULL, NULL, "Button", NULL, NULL );

	long top = 300 - ( kMenuButtonCount * kButtonStep ) / 2;
	for ( int i = 0; i < buttonCount; i++ )
	{
		buttons[i].showGUIWindow( false );
		buttons[i].setMessageOnRelease();
		buttons[i].setPressFX( LOG_VIDEOBUTTONS );
		buttons[i].setHighlightFX( LOG_DIGITALHIGHLIGHT );
	}
	for ( int i = 0; i < kMenuButtonCount; i++ )
	{
		aButton* b = getButton( kMenuButtons[i] );
		if ( !b )
			continue;
		b->showGUIWindow( true );
		b->move( 0.f, (float)( top + i * kButtonStep - b->top() ) );
		if ( i == 0 )
		{
			panel.left = b->left() - kPanelPadding;
			panel.right = b->right() + kPanelPadding;
			panel.top = b->top() - kPanelPadding;
		}
		panel.bottom = b->bottom() + kPanelPadding;
	}
	getButton( MENU_MAIN )->setText( IDS_LOGISTICS_MENU_MAIN );
	getButton( MENU_CLOSE )->setText( IDS_LOGISTICS_MENU_CLOSE );
	// the main menu help ("Exit the game" ...) does not apply here
	getButton( MENU_MAIN )->setHelpHeader( 0 );
	getButton( MENU_MAIN )->setHelpID( 0 );
	getButton( MENU_CLOSE )->setHelpHeader( 0 );
	getButton( MENU_CLOSE )->setHelpID( 0 );

	dialog.init();
}

void LogisticsMenu::open( bool campaign, bool multiplayer )
{
	bCampaign = campaign;
	bMultiplayer = multiplayer;
	bOptions = bEncyclopedia = bAsking = false;
	state = OPEN;
	status = RUNNING;
	for ( int i = 0; i < kMenuButtonCount; i++ )
		if ( aButton* b = getButton( kMenuButtons[i] ) )
			b->press( false );
	soundSystem->playDigitalSample( LOG_MAINMENUBUTTON );
	gos_KeyboardFlush();
}

int LogisticsMenu::handleMessage( unsigned long, unsigned long who )
{
	if ( state != OPEN || bAsking || bOptions || bEncyclopedia )
		return 0;

	switch ( who )
	{
	case MENU_OPTIONS:
		if ( !options )
		{
			options = new OptionsScreenWrapper;
			options->init();
		}
		options->begin();
		bOptions = true;
		break;
	case MENU_ENCYCLOPEDIA:
		if ( !encyclopedia )
		{
			encyclopedia = new Mechlopedia;
			encyclopedia->init();
		}
		encyclopedia->begin();
		bEncyclopedia = true;
		break;
	case MENU_MAIN:
		if ( bCampaign )
			dialog.ask( IDS_PROMPT_SAVE_BEFORE_MAIN, IDS_DIALOG_DONT_SAVE, IDS_DIALOG_SAVE );
		else if ( bMultiplayer )
			dialog.ask( IDS_PROMPT_LEAVE_ROOM, IDS_DIALOG_NO, IDS_DIALOG_YES );
		else
		{
			state = LEAVE;	// nothing to save or leave
			break;
		}
		bAsking = true;
		break;
	case MENU_CLOSE:
		state = CLOSED;
		break;
	}
	if ( aButton* b = getButton( who ) )
		b->press( false );
	return 1;
}

void LogisticsMenu::update()
{
	if ( state != OPEN )
		return;

	if ( bOptions )
	{
		if ( options->update() == OptionsScreenWrapper::opt_DONE )
		{
			options->end();
			bOptions = false;
		}
		return;
	}
	if ( bEncyclopedia )
	{
		encyclopedia->update();
		if ( encyclopedia->getStatus() == NEXT )
			bEncyclopedia = false;
		return;
	}
	if ( bAsking )
	{
		dialog.update();
		if ( !dialog.isDone() )
			return;
		dialog.end();
		bAsking = false;
		const long answer = dialog.getStatus();
		if ( answer == YES )
			state = bCampaign ? SAVE_AND_LEAVE : LEAVE;
		else if ( answer == NO && bCampaign )
			state = LEAVE;		// "Don't save"
		return;					// cancelled: back to the menu
	}

	userInput->mouseOn();
	userInput->setMouseCursor( mState_LOGISTICS );
	LogisticsScreen::update();
	if ( state != OPEN )
		return;

	if ( gos_GetKeyStatus( KEY_ESCAPE ) == KEY_RELEASED
		|| ( userInput->leftMouseReleased()
			&& !inside( userInput->getMouseX(), userInput->getMouseY() )
			&& !inside( userInput->getMouseDragX(), userInput->getMouseDragY() )
			&& !( userInput->getMouseX() >= panel.left && userInput->getMouseX() <= panel.right
				&& userInput->getMouseY() >= panel.top && userInput->getMouseY() <= panel.bottom ) ) )
		state = CLOSED;
}

void LogisticsMenu::render()
{
	if ( state != OPEN )
		return;
	if ( bOptions )
	{
		options->render();
		return;
	}
	if ( bEncyclopedia )
	{
		encyclopedia->render();
		return;
	}

	GUI_RECT screen = { 0, 0, Environment.screenWidth, Environment.screenHeight };
	drawRect( screen, kDimColor );
	drawRect( panel, 0xec000000 );
	drawEmptyRect( panel, 0xff0066c6, 0xff0066c6 );	// orange (BGR)

	LogisticsScreen::render( 0, 0 );

	if ( bAsking )
		dialog.render();
}
