#define CHATWINDOW_CPP
//===========================================================================//
//ChatWindow.cpp		: Implementation of the ChatWindow component.
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#include"chatwindow.h"
#include <algorithm>
#include"mclib.h"
#include"multplyr.h"
#include "../resource.h"

ChatWindow* ChatWindow::s_instance = NULL;

#define MP_CHAT_HELPTOGGLE	300
#define MP_CHAT_EXPAND		301
#define MP_CHAT_TEAMONLY	302

namespace {
constexpr int ChatEveryone = -2;
constexpr int ChatTeam = -1;
constexpr int AudienceWidth = 215;
constexpr int AudienceHeight = 17;

bool contains(const GUI_RECT& rect, long x, long y)
{
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
}

ChatWindow::ChatWindow(  )
{
	curItem = 0;
	helpTextArrayID = 0;
}

//-------------------------------------------------------------------------------------------------

ChatWindow::~ChatWindow()
{
//	listBox.destroy( );
}

//-------------------------------------------------------------------------------------------------
void ChatWindow::static_destroy()
{
	if (s_instance)
	{
		delete s_instance;
		s_instance = NULL;
	}
}

//-------------------------------------------------------------------------------------------------

void ChatWindow::init()
{
	s_instance = new ChatWindow;
	s_instance->initInstance();
}

//-------------------------------------------------------------------------------------------------

int ChatWindow::initInstance()
{
	FitIniFile file;
	FullPathFileName path;
	path.init( artPath, "mcl_mp_chatwidget", ".fit" );
	if ( NO_ERR != file.open( path ) )
	{
		char buffer2[512];
		sprintf( buffer2, "couldn't open file %s", (const char*)path );
		Assert( 0, 0, buffer2 );
		return false;	

	}

	LogisticsScreen::init( file, "Static", "Text", "Rect", "Button", "Edit" );		

    auto* helpButton = getButton(MP_CHAT_HELPTOGGLE);
    auto* expandButton = getButton(MP_CHAT_EXPAND);
    const long chatRight = expandButton->globalRight();
    const long toolbarTop = edits[0].globalBottom() - AudienceHeight;
    constexpr long toolbarGap = 1;
    long buttonRight = chatRight;
    for (auto* button : {expandButton, helpButton})
    {
        const long width = button->width() * AudienceHeight / button->height();
        button->moveTo(buttonRight - width, toolbarTop);
        button->resizeIcon(width, AudienceHeight);
        buttonRight = button->globalLeft() - toolbarGap;
    }

    auto& messageArea = rects[1];
    messageArea.resize(chatRight - messageArea.globalLeft(), messageArea.height());
    rects[2].resize(messageArea.width() - 4, rects[2].height());
    rects[3].moveTo(helpButton->globalLeft(), toolbarTop);
    rects[3].resize(helpButton->width(), AudienceHeight);
    rects[4].moveTo(expandButton->globalLeft(), toolbarTop);
    rects[4].resize(expandButton->width(), AudienceHeight);
    rects[5].setHelpID(0);
    textObjects[0].resize(chatRight - textObjects[0].globalLeft(), textObjects[0].height());
    listBox.init(messageArea.left(), messageArea.top(), messageArea.width(), messageArea.height());
    listBox.setSpaceBetweenItems(0);

	chatWidget.init();

	for ( int i = 0; i < buttonCount; i++ )
	{
		buttons[i].setMessageOnRelease();
		buttons[i].setPressFX( LOG_VIDEOBUTTONS );
		buttons[i].setDisabledFX( LOG_WRONGBUTTON );
		buttons[i].setHighlightFX( LOG_DIGITALHIGHLIGHT );
	}	

	ChatMessageItem item;
	float itemHeight = item.height();

	maxItems = listBox.height()/itemHeight;

    getButton(MP_CHAT_TEAMONLY)->showGUIWindow(false);
    audienceFont.init(IDS_CHAT_FONT);
    edits[0].resize(audienceRect().left - edits[0].globalLeft() - 6, edits[0].height());
    edits[0].limitEntry(128);
	edits[0].setSelectedColor( 0xffffffff );

	return true;
}

GUI_RECT ChatWindow::audienceRect(int row)
{
    const long right = getButton(MP_CHAT_HELPTOGGLE)->globalLeft() - 2;
    const long bottom = edits[0].globalBottom() - row * AudienceHeight;
    return {right - AudienceWidth, bottom - AudienceHeight, right, bottom};
}

int ChatWindow::getAudiences(int* targets)
{
    int count = 0;
    targets[count++] = ChatEveryone;
    // Team chat only means something with a teammate (team against team).
    const auto* local = MPlayer->getPlayerInfo(MPlayer->commanderID);
    bool teammate = false;
    for (const auto& player : MPlayer->playerInfo)
        if (local && local->team >= 0 && player.player && !player.leftSession && player.commanderID >= 0 &&
            player.commanderID != MPlayer->commanderID && player.team == local->team)
            teammate = true;
    if (teammate)
        targets[count++] = ChatTeam;
    for (const auto& player : MPlayer->playerInfo)
        if (player.player && !player.leftSession && player.commanderID >= 0 &&
            player.commanderID != MPlayer->commanderID)
            targets[count++] = player.commanderID;
    return count;
}

bool ChatWindow::validRecipient() const
{
    const auto* player = MPlayer->getPlayerInfo(audience);
    return player && player->player && !player->leftSession &&
        strcmp(player->name, recipientName) == 0;
}

const char* ChatWindow::audienceLabel(int target, char* label)
{
    if (target == ChatEveryone || target == ChatTeam)
        cLoadString(target == ChatEveryone ? IDS_MP_CHAT_ALL : IDS_MP_CHAT_TEAM, label, 127);
    else
    {
        char format[128];
        cLoadString(IDS_MP_CHAT_WHISPER_TO, format, sizeof(format) - 1);
        snprintf(label, 128, format, target + 1, MPlayer->playerInfo[target].name);
    }
    return label;
}

bool ChatWindow::pointInside( long xPos, long yPos )
{
    if (audienceOpen || (!getButton(MP_CHAT_HELPTOGGLE)->isPressed() && contains(audienceRect(), xPos, yPos)))
        return true;
	if ( getButton( MP_CHAT_EXPAND )->isPressed() )
		return chatWidget.inside(xPos, yPos) || rects[3].pointInside(xPos, yPos) ||
            rects[4].pointInside(xPos, yPos);

	return LogisticsScreen::inside( xPos, yPos );
}


bool ChatWindow::isExpanded()
{
	return getButton( MP_CHAT_EXPAND )->isPressed();
}
int ChatWindow::handleMessage( unsigned long, unsigned long who )
{
	if ( who == MP_CHAT_HELPTOGGLE )
	{
		if ( getButton( MP_CHAT_HELPTOGGLE )->isPressed() 
			&& getButton( MP_CHAT_EXPAND )->isPressed() )
		{
			getButton( MP_CHAT_EXPAND )->press( 0 );
		}
	}
	else if ( who == MP_CHAT_EXPAND )
	{
		if ( getButton( MP_CHAT_EXPAND )->isPressed() )
			getButton( MP_CHAT_HELPTOGGLE )->press( 0 );
	}
	return 1;
}


void ChatWindow::render( int xOffset, int yOffset )
{
	if ( getButton( MP_CHAT_EXPAND )->isPressed() )
	{
		chatWidget.listBox.move( xOffset, yOffset );
		chatWidget.listBox.render();
		chatWidget.listBox.move( -xOffset, -yOffset );
		textObjects[0].showGUIWindow( 0 );

	}
	else if ( !getButton( MP_CHAT_HELPTOGGLE )->isPressed() )
	{
		listBox.move( xOffset, yOffset );
		listBox.render();
		listBox.move( -xOffset, -yOffset );
		textObjects[0].showGUIWindow( 0 );
	}
	else
		textObjects[0].showGUIWindow( 1 );

	LogisticsScreen::render( xOffset, yOffset );

	
    if (!getButton(MP_CHAT_HELPTOGGLE)->isPressed())
    {
        const auto audienceBounds = audienceRect();
        const GUI_RECT input = {edits[0].globalLeft() + xOffset, edits[0].globalTop() + yOffset,
                                audienceBounds.right + xOffset, edits[0].globalBottom() + yOffset};
        drawRect(input, 0xff000000);
        if (getButton(MP_CHAT_EXPAND)->isPressed())
        {
            chatWidget.render(xOffset, yOffset);
            // The expanded panel's art covers the lower-right toolbar.
            for (auto* button : {getButton(MP_CHAT_HELPTOGGLE), getButton(MP_CHAT_EXPAND)})
            {
                button->move(xOffset, yOffset);
                button->render();
                button->move(-xOffset, -yOffset);
            }
        }
        edits[0].move(xOffset, yOffset);
        edits[0].render();
        edits[0].move(-xOffset, -yOffset);
    }

    textObjects[0].render();
    if (getButton(MP_CHAT_HELPTOGGLE)->isPressed())
        return;

    const auto renderAudience = [&](int target, int row) {
        auto rect = audienceRect(row);
        rect.left += xOffset;
        rect.right += xOffset;
        rect.top += yOffset;
        rect.bottom += yOffset;
        drawRect(rect, target == audience ? 0xff23495a : 0xff151b20);
        char label[132];
        if (!row && target >= 0 && !validRecipient())
            cLoadString(IDS_MP_CHAT_RECIPIENT_MISSING, label, sizeof(label) - 1);
        else
            audienceLabel(target, label);

        const int labelWidth = AudienceWidth - 20;
        if (audienceFont.width(label) > static_cast<unsigned>(labelWidth))
        {
            // Cut at UTF-8 boundaries and leave room for the ellipsis.
            size_t end = strlen(label);
            do {
                do {
                    --end;
                } while (end && (static_cast<unsigned char>(label[end]) & 0xc0) == 0x80);
                strcpy(label + end, "...");
            } while (end && audienceFont.width(label) > static_cast<unsigned>(labelWidth));
        }
        audienceFont.render(label, rect.left + 4, rect.top, labelWidth, AudienceHeight, 0xffffffff, false, 3);
        if (!row)
        {
            // The menu opens upwards: point up while closed, down while open.
            const long cx = rect.right - 8;
            const long cy = rect.top + AudienceHeight / 2;
            for (long k = 0; k < 4; ++k)
            {
                const long y = audienceOpen ? cy + 1 - k : cy - 2 + k;
                drawRect({cx - k, y, cx + k + 1, y + 1}, 0xffffffff);
            }
        }
    };

    renderAudience(audience, 0);
    if (audienceOpen)
    {
        int targets[MAX_MC_PLAYERS + 2];
        const int count = getAudiences(targets);
        for (int i = 0; i < count; ++i)
            renderAudience(targets[i], count - i);
    }

}

void ChatWindow::update()
{
    int targets[MAX_MC_PLAYERS + 2];
    const int targetCount = getAudiences(targets);
    // The last teammate left or changed team.
    if (audience == ChatTeam && std::find(targets, targets + targetCount, ChatTeam) == targets + targetCount)
        audience = ChatEveryone;

    if (userInput->isLeftClick() && !getButton(MP_CHAT_HELPTOGGLE)->isPressed())
    {
        const long x = userInput->getMouseX();
        const long y = userInput->getMouseY();
        if (contains(audienceRect(), x, y))
        {
            audienceOpen = !audienceOpen;
            return;
        }
        if (audienceOpen)
        {
            audienceOpen = false;
            for (int i = 0; i < targetCount; ++i)
                if (contains(audienceRect(targetCount - i), x, y))
                {
                    audience = targets[i];
                    if (audience >= 0)
                        recipientName = MPlayer->playerInfo[audience].name;
                    edits[0].setFocus(true);
                    break;
                }
            return;
        }
    }

	bool bEnterPressed = gos_GetKeyStatus(KEY_RETURN) == KEY_PRESSED;
    if (audienceOpen && (bEnterPressed || gos_GetKeyStatus(KEY_ESCAPE) == KEY_PRESSED))
    {
        audienceOpen = false;
        return;
    }
	bool bFocused = edits[0].hasFocus();

	if ( getButton( MP_CHAT_EXPAND )->isPressed() )
	{
		chatWidget.update();
		chatWidget.listBox.update();
	}
	else
		listBox.update();


	
	edits[0].showGUIWindow( true );
	if ( getButton( MP_CHAT_HELPTOGGLE )->isPressed() )
	{
		edits[0].showGUIWindow( false );
		rects[2].setColor( rects[2].getColor( ) & 0x00ffffff );
	}
	else
		rects[2].setColor( rects[2].getColor( ) | 0xff000000 );
	

	char* chatTexts[MAX_STORED_CHATS];
	long playerIDs[MAX_STORED_CHATS];

	long count = MAX_STORED_CHATS;
	MPlayer->getChatMessages( chatTexts, playerIDs, count );
	
	refillListBox( listBox, chatTexts, playerIDs, listItems, curItem, count, maxItems );
	refillListBox( chatWidget.listBox, chatTexts, playerIDs, chatWidget.listItems, chatWidget.curItem, count, 128 );


	// now do the same for the chat widget....

	LogisticsScreen::update();

	if ( bEnterPressed )
	{
		if ( bFocused )
		{
			EString text;
			edits[0].getEntry( text );
            if (text.Length())
            {
                if (audience >= 0)
                {
                    if (!validRecipient() || !MPlayer->sendWhisper(audience, text))
                    {
                        char unavailable[128];
                        cLoadString(IDS_MP_CHAT_RECIPIENT_LEFT, unavailable, sizeof(unavailable) - 1);
                        MPlayer->addLocalChat(MPlayer->commanderID, unavailable, true, false);
                        edits[0].setFocus(true);
                        audienceOpen = true;
                        return;
                    }
                }
                else
                {
                    const auto* local = MPlayer->getPlayerInfo(MPlayer->commanderID);
                    const int team = audience == ChatTeam ? local->team : -1;
                    MPlayer->sendChat(0, team, (char*)(const char*)text);
                }
            }
			edits[0].setEntry( "" );
			edits[0].setFocus( false );
		}
		else
		{
			// repress chat button if appropriate
			if ( getButton( MP_CHAT_HELPTOGGLE )->isPressed() )
				getButton( MP_CHAT_HELPTOGGLE )->press( 0 );
			edits[0].setFocus( true );
		}
	}


}

void ChatWindow::refillListBox( aListBox& listBox, char** chatTexts, long* playerIDs, ChatMessageItem* pItems, long& curItem, long itemCount, long maxCount )
{
	// Leave space for the list scrollbar and its margins.
	const long messageWidth = listBox.width() - 22;
	long linesToAdd = 0;
	for ( int i = 0; i < itemCount && i < maxCount; i++ )
	{
		int item = (curItem+i)%maxCount;
		int playerID = playerIDs[i] & 0x00ffffff;
		MC2Player* player = &MPlayer->playerInfo[0];
		for ( int j = 0; j < MAX_MC_PLAYERS; j++ )
		{
			if ( MPlayer->playerInfo[j].commanderID == playerID )
			{
				player = &MPlayer->playerInfo[j];
				break;
			}
		}
		pItems[item].setPlayerColor( MPlayer->colors[ player->baseColor[0] ]); 
		if ( (playerIDs[i] & 0x01000000) )
			pItems[item].setPlayerName( "", messageWidth );
		else
			pItems[item].setPlayerName( player->name, messageWidth );
		int lineCount = pItems[item].setText( chatTexts[i] );
		linesToAdd += lineCount;
		pItems[item].setTextColor( (playerIDs[i] & 0x10000000)  ? 0xff41c700 : 0xffffffff );
		pItems[item].moveTo( 0, 0 );

	}

	int curLinesInListBox = 0;
	for (int i = 0; i < listBox.GetItemCount(); i++ )
	{
		ChatMessageItem* pItem = (ChatMessageItem*)listBox.GetItem( i );
		curLinesInListBox += pItem->getLineCount();
	}
	if ( curLinesInListBox + linesToAdd > maxCount )
	{
		for ( int i = 0; i < maxCount; i++ )
		{
			ChatMessageItem* pItem = (ChatMessageItem*)listBox.GetItem( 0 );
			if ( pItem )
			{
				curLinesInListBox -= pItem->getLineCount();

				listBox.RemoveItem( listBox.GetItem( 0 ), 0 );
			}
			if ( curLinesInListBox + linesToAdd <= maxCount )
				break;
		}
	}

	for (int i = 0; i < itemCount && i < maxCount; i++ )
	{
		int item = (curItem)%maxCount;
		listBox.AddItem( &pItems[item] );
		if ( maxCount > 2 )
			listBox.SelectItem( listBox.GetItemCount() - 1 );
		curItem++;
	}
}

//-------------------------------------------------------------------------------------------------
ChatWidget::ChatWidget()
{
	curItem = 0;
}

ChatWidget::~ChatWidget()
{
//	listBox.destroy();
}

void ChatWidget::init( )
{

	FitIniFile file;
	FullPathFileName path;
	path.init( artPath, "mcl_mp_chatwidget_up", ".fit" );
	if ( NO_ERR != file.open( path ) )
	{
		char buffer2[512];
		sprintf( buffer2, "couldn't open file %s", (const char*)path );
		Assert( 0, 0, buffer2 );
		return;

	}


	LogisticsScreen::init( file, "Static", "Text", "Rect", "Button", "Edit" );
	listBox.init( rects[2].left(), rects[2].top(), rects[2].width(), rects[2].height() );
	listBox.setSpaceBetweenItems( 0 );


}

//-------------------------------------------------------------------------------------------------

ChatMessageItem::ChatMessageItem()

{
	name.font.init( IDS_CHAT_FONT );
	playerText.font.init( IDS_CHAT_FONT );
	playerText.setColor( 0xffffffff );
	name.setColor( 0xffffffff );
	name.move( 1, 1 );
	playerText.move( 0, 1 );
	playerRect.move( 0, 1 );


	resize(140, name.font.height() );

	addChild( &playerRect );
	addChild( &name );
	addChild( &playerText );
	lineCount = 1;
}

void ChatMessageItem::setPlayerName( const char* pName, long messageWidth )
{
	name.setText( pName );
	name.moveTo( globalX()+1, globalY()+1 );
	long tmpWidth = name.font.width( pName );
	playerText.moveTo( globalX() + tmpWidth+3, globalY() +1 );

	playerRect.resize( tmpWidth+2, playerText.font.height() - 2 );
	playerText.resize( messageWidth - playerRect.width() - 2, playerText.font.height()  );
	lineCount = 1;


}

int ChatMessageItem::setText( const char* pText )
{
	playerText.setText( pText );
	lineCount = 1;

	unsigned long height = playerText.font.height( pText, playerText.width() );
	int retVal = 1;
	if ( height > playerText.font.height() * 2 )
	{
		height = playerText.font.height() * 2;
		retVal = 2;
		lineCount = 2;
	}
	playerText.resize( playerText.width(), height );
	resize( width(), height);

	return retVal;
}

void ChatMessageItem::setPlayerColor( long newColor )
{
	playerRect.setColor( newColor );

	if ( ((newColor & 0xff) + ( (newColor & 0xff00)>>8 ) + ( (newColor & 0xff0000)>>16 ))/3 < 85 )
		name.setColor( 0xffffffff );
	else
		name.setColor( 0xff000000 );
}

void ChatMessageItem::setTextColor( long newColor )
{
	playerText.setColor( newColor );
}



//*************************************************************************************************
// end of file ( ChatWindow.cpp )
