#ifndef CHATWINDOW_H
#define CHATWINDOW_H
//===========================================================================//
//ChatWindow.h			: Interface for the ChatWindow component.
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef LOGISTICSSCREEN_H
#include"logisticsscreen.h"
#endif

//*************************************************************************************************

/**************************************************************************************************
CLASS DESCRIPTION
ChatWindow:
**************************************************************************************************/
#ifndef ALISTBOX_H
#include"alistbox.h"
#endif

class ChatMessageItem : public aListItem
{
public:

	ChatMessageItem();
	void		 setPlayerColor(long color);
	void		 setTextColor( long color );
	void		 setPlayerName( const char* name, long messageWidth );
	int			 setText( const char* text ); // returns number of lines
	long		 getLineCount() { return lineCount; }

private:
	aText		 name;
	aText		 playerText;
	aRect		 playerRect;
	long		 lineCount;
};

class ChatWidget : public LogisticsScreen  // the one that obscures....
{
public:

	ChatWidget();
	virtual ~ChatWidget();

	void init();

private:

	friend class ChatWindow;
	aListBox		listBox;

	ChatMessageItem	listItems[128];
	long			curItem;


};

class ChatWindow: public LogisticsScreen
{
	public:

		ChatWindow();
		virtual ~ChatWindow();

		static void init();
        // static
		static void static_destroy();
		static ChatWindow* instance() { return s_instance; }

		int initInstance();

		virtual void update();
		virtual void render( int xOffset, int yOffset );
		int handleMessage( unsigned long, unsigned long );

		virtual bool pointInside( long xPos, long yPos );
		bool		 isExpanded();


	private:

		static ChatWindow*	s_instance;

		aListBox		listBox;

        int audience = -2;
        bool audienceOpen = false;
        EString recipientName;
        aFont audienceFont;

        GUI_RECT audienceRect(int row = 0);
        const char* audienceLabel(int target, char* label);
        int getAudiences(int* targets);
        bool validRecipient() const;
		ChatWidget		chatWidget;

		ChatMessageItem	listItems[4];
		long			curItem;
		long			maxItems;


		static void refillListBox( aListBox& listBox, char** chatTexts, long* playerIDs,  ChatMessageItem* pItems, 
			long& curItem, long itemCount, long maxCount );





};






//*************************************************************************************************
#endif  // end of file ( ChatWindow.h )
