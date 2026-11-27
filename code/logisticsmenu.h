// SPDX-License-Identifier: GPL-3.0-only
#ifndef LOGISTICSMENU_H
#define LOGISTICSMENU_H

// Menu opened by the top right button of the logistics and multiplayer
// screens: Options, Encyclopedia, Back to main menu (asks first), Close.

#include "logisticsdialog.h"

class OptionsScreenWrapper;
class Mechlopedia;

// Two-button question; Esc or a click outside cancels instead of choosing
// the left button.
class LogisticsLeaveDialog : public LogisticsDialog
{
public:
	enum { CANCEL = 1000 };

	void init();
	void ask( int textID, int leftButton, int rightButton );
	virtual int handleMessage( unsigned long, unsigned long who );
	virtual void update();
};

class LogisticsMenu : public LogisticsScreen
{
public:
	enum Result
	{
		OPEN,           // still showing
		CLOSED,         // back to the screen underneath
		LEAVE,          // to the title menu
		SAVE_AND_LEAVE  // campaign: save, then to the title menu
	};

	LogisticsMenu();
	virtual ~LogisticsMenu();

	void init();
	// campaign: the leave question offers to save; multiplayer: it warns
	// that the room is left
	void open( bool campaign, bool multiplayer );
	virtual void update();
	virtual void render();
	virtual int handleMessage( unsigned long, unsigned long who );

	Result result() const { return state; }
	void close() { state = CLOSED; }
	// Options and the encyclopedia cover the whole screen
	bool coversScreen() const { return bOptions || bEncyclopedia; }

private:
	Result state;
	bool bCampaign;
	bool bMultiplayer;
	bool bOptions;
	bool bEncyclopedia;
	bool bAsking;
	GUI_RECT panel;
	OptionsScreenWrapper* options;
	Mechlopedia* encyclopedia;
	LogisticsLeaveDialog dialog;
};

#endif
