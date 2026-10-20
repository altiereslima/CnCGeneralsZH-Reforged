/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Diplomacy.cpp ///////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell - August 2002
// Desc: GUI callbacks for the diplomacy menu
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------

static NameKeyType staticTextPlayerID[MAX_SLOTS];
static NameKeyType staticTextSideID[MAX_SLOTS];
static NameKeyType staticTextTeamID[MAX_SLOTS];
static NameKeyType staticTextStatusID[MAX_SLOTS];
static NameKeyType buttonMuteID[MAX_SLOTS];
static NameKeyType buttonUnMuteID[MAX_SLOTS];
static NameKeyType radioButtonInGameID = NAMEKEY_INVALID;
static NameKeyType radioButtonBuddiesID = NAMEKEY_INVALID;
static GameWindow *radioButtonInGame = NULL;
static GameWindow *radioButtonBuddies = NULL;
static NameKeyType winInGameID = NAMEKEY_INVALID;
static NameKeyType winBuddiesID = NAMEKEY_INVALID;
static NameKeyType winSoloID = NAMEKEY_INVALID;
static GameWindow *winInGame = NULL;
static GameWindow *winBuddies = NULL;
static GameWindow *winSolo = NULL;
static GameWindow *staticTextPlayer[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *staticTextSide[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *staticTextTeam[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *staticTextStatus[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *buttonMute[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *buttonUnMute[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static Int slotNumInRow[MAX_SLOTS];

// The money column (fork, Tools/diplomacy_layout.py).  The amount each row would send is kept per
// slot, so it survives the screen being closed and opened again.
static NameKeyType buttonMoneyMinusID[MAX_SLOTS];
static NameKeyType buttonMoneyPlusID[MAX_SLOTS];
static NameKeyType buttonMoneySendID[MAX_SLOTS];
static GameWindow *buttonMoneyMinus[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *staticTextMoney[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *buttonMoneyPlus[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *buttonMoneySend[MAX_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static GameWindow *staticTextMoneyTitle = NULL;
static Int moneyToSend[MAX_SLOTS] = {0, 0, 0, 0, 0, 0, 0, 0};

//-------------------------------------------------------------------------------------------------

static WindowLayout *theLayout = NULL;
static GameWindow *theWindow = NULL;
static AnimateWindowManager *theAnimateWindowManager = NULL;
WindowMsgHandledType BuddyControlSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2);
void InitBuddyControls(Int type);
void updateBuddyInfo( void );
static void grabWindowPointers( void )
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		AsciiString temp;
		temp.format("Diplomacy.wnd:StaticTextPlayer%d", i);
		staticTextPlayerID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextSide%d", i);
		staticTextSideID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextTeam%d", i);
		staticTextTeamID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextStatus%d", i);
		staticTextStatusID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonMute%d", i);
		buttonMuteID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonUnMute%d", i);
		buttonUnMuteID[i] = NAMEKEY(temp);

		staticTextPlayer[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextPlayerID[i]);
		staticTextSide[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextSideID[i]);
		staticTextTeam[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextTeamID[i]);
		staticTextStatus[i] = TheWindowManager->winGetWindowFromId(theWindow, staticTextStatusID[i]);
		buttonMute[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonMuteID[i]);
		buttonUnMute[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonUnMuteID[i]);

		temp.format("Diplomacy.wnd:ButtonMoneyMinus%d", i);
		buttonMoneyMinusID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonMoneyPlus%d", i);
		buttonMoneyPlusID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:ButtonMoneySend%d", i);
		buttonMoneySendID[i] = NAMEKEY(temp);
		temp.format("Diplomacy.wnd:StaticTextMoney%d", i);
		buttonMoneyMinus[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonMoneyMinusID[i]);
		buttonMoneyPlus[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonMoneyPlusID[i]);
		buttonMoneySend[i] = TheWindowManager->winGetWindowFromId(theWindow, buttonMoneySendID[i]);
		staticTextMoney[i] = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY(temp));

		slotNumInRow[i] = -1;
	}
	staticTextMoneyTitle = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY("Diplomacy.wnd:StaticTextMoneyTitle"));
}

static void releaseWindowPointers( void )
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		staticTextPlayer[i] = NULL;
		staticTextSide[i] = NULL;
		staticTextTeam[i] = NULL;
		staticTextStatus[i] = NULL;
		buttonMute[i] = NULL;
		buttonUnMute[i] = NULL;
		buttonMoneyMinus[i] = NULL;
		staticTextMoney[i] = NULL;
		buttonMoneyPlus[i] = NULL;
		buttonMoneySend[i] = NULL;

		slotNumInRow[i] = -1;
	}
	staticTextMoneyTitle = NULL;
}

//-------------------------------------------------------------------------------------------------
// The money column.  The screen only picks the amount and asks; MSG_TRANSFER_MONEY is checked and
// carried out in GameLogic on every machine alike (GameLogicDispatch.cpp).

static Player *playerInSlot( Int slotNum )
{
	AsciiString playerName;
	playerName.format("player%d", slotNum);
	return ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
}

/// 100 a step, 1,000 with Ctrl, 10,000 with Shift, 100,000 with both
static Int moneyStep( void )
{
	const Bool ctrl = TheKeyboard->isCtrl();
	const Bool shift = TheKeyboard->isShift();
	if (ctrl && shift)
		return 100000;
	if (shift)
		return 10000;
	if (ctrl)
		return 1000;
	return 100;
}

static void showMoneyToSend( Int row )
{
	if (staticTextMoney[row] == NULL || slotNumInRow[row] < 0)
		return;
	UnicodeString text;
	text.format(TheGameText->fetch("GUI:MoneyTransferAmount"), moneyToSend[slotNumInRow[row]]);
	GadgetStaticTextSetText(staticTextMoney[row], text);
}

/// direction +1 or -1, one step of moneyStep, kept between nothing and all the local player holds
static void adjustMoneyToSend( Int row, Int direction )
{
	Int &amount = moneyToSend[slotNumInRow[row]];
	const Int held = (Int)ThePlayerList->getLocalPlayer()->getMoney()->countMoney();
	amount += direction * moneyStep();
	if (amount > held)
		amount = held;
	if (amount < 0)
		amount = 0;
	showMoneyToSend(row);
}

static void sendMoney( Int row )
{
	Int &amount = moneyToSend[slotNumInRow[row]];
	if (amount <= 0)
		return;
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_TRANSFER_MONEY);
	msg->appendIntegerArgument(playerInSlot(slotNumInRow[row])->getPlayerIndex());
	msg->appendIntegerArgument(amount);
	amount = 0;
	showMoneyToSend(row);
}

/// the row whose money controls are under the mouse, or -1
static Int moneyRowAt( Int x, Int y )
{
	for (Int row = 0; row < MAX_SLOTS; ++row)
	{
		GameWindow *controls[] = { buttonMoneyMinus[row], staticTextMoney[row], buttonMoneyPlus[row], buttonMoneySend[row] };
		for (Int c = 0; c < 4; ++c)
			if (controls[c] && !controls[c]->winIsHidden() && controls[c]->winPointInWindow(x, y))
				return row;
	}
	return -1;
}


//-------------------------------------------------------------------------------------------------

static void updateFunc( WindowLayout *layout, void *param )
{
	if (theAnimateWindowManager && TheGlobalData->m_animateWindows)
	{
		Bool wasFinished = theAnimateWindowManager->isFinished();
		theAnimateWindowManager->update();
		if (theAnimateWindowManager->isFinished() && !wasFinished && theAnimateWindowManager->isReversed())
			theWindow->winHide( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static BriefingList theBriefingList;

//-------------------------------------------------------------------------------------------------
BriefingList* GetBriefingTextList(void)
{
	return &theBriefingList;
}

//-------------------------------------------------------------------------------------------------
void UpdateDiplomacyBriefingText(AsciiString newText, Bool clear)
{
	GameWindow *listboxSolo = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY("Diplomacy.wnd:ListboxSolo"));

	if (clear)
	{
		theBriefingList.clear();
		if (listboxSolo)
			GadgetListBoxReset(listboxSolo);
	}

	if (newText.isEmpty())
		return;

	if (std::find(theBriefingList.begin(), theBriefingList.end(), newText) != theBriefingList.end())
		return;

	theBriefingList.push_back(newText);
	if (!listboxSolo)
		return;

	UnicodeString translated = TheGameText->fetch(newText);

	Int numEntries = GadgetListBoxGetNumEntries(listboxSolo);
	GadgetListBoxAddEntryText(listboxSolo, translated, TheInGameUI->getMessageColor(numEntries%2), -1);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShowDiplomacy( Bool immediate )
{
	if (!TheInGameUI->getInputEnabled() || TheGameLogic->isIntroMoviePlaying() || 
			TheGameLogic->isLoadingMap())
		return;
	

	if (TheInGameUI->isQuitMenuVisible())
		return;

	if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
		return;

	if (theWindow)
	{
		theWindow->winHide(FALSE);
		theWindow->winEnable(TRUE);
	}
	else
	{
		theLayout = TheWindowManager->winCreateLayout( "Diplomacy.wnd" );
		theWindow = theLayout->getFirstWindow();
		theLayout->setUpdate(updateFunc);
		theAnimateWindowManager = NEW AnimateWindowManager;
		radioButtonInGameID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:RadioButtonInGame");
		radioButtonBuddiesID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:RadioButtonBuddies");
		radioButtonInGame = TheWindowManager->winGetWindowFromId(NULL, radioButtonInGameID);
		radioButtonBuddies = TheWindowManager->winGetWindowFromId(NULL, radioButtonBuddiesID);
		winInGameID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:InGameParent");
		winBuddiesID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:BuddiesParent");
		winSoloID = TheNameKeyGenerator->nameToKey("Diplomacy.wnd:SoloParent");
		winInGame = TheWindowManager->winGetWindowFromId(NULL, winInGameID);
		winBuddies = TheWindowManager->winGetWindowFromId(NULL, winBuddiesID);
		winSolo = TheWindowManager->winGetWindowFromId(NULL, winSoloID);

		if (!TheRecorder->isMultiplayer())
		{
			GameWindow *listboxSolo = TheWindowManager->winGetWindowFromId(theWindow, NAMEKEY("Diplomacy.wnd:ListboxSolo"));
			if (listboxSolo)
			{
				for (BriefingList::iterator it = theBriefingList.begin(); it != theBriefingList.end(); ++it)
				{
					UnicodeString translated = TheGameText->fetch(*it);
					Int numEntries = GadgetListBoxGetNumEntries(listboxSolo);
					GadgetListBoxAddEntryText(listboxSolo, translated, TheInGameUI->getMessageColor(numEntries%2), -1);
				}
			}
		}
	}
	theLayout->hide(FALSE);

	radioButtonInGame->winHide(TRUE);
	radioButtonBuddies->winHide(TRUE);
	GadgetRadioSetSelection(radioButtonInGame, FALSE);
	if (TheRecorder->isMultiplayer())
	{
		winInGame->winHide(FALSE);
		winBuddies->winHide(TRUE);
		winSolo->winHide(TRUE);
	}
	else
	{
		winInGame->winHide(TRUE);
		winBuddies->winHide(TRUE);
		winSolo->winHide(FALSE);
	}

	theAnimateWindowManager->reset();
	if (!immediate && TheGlobalData->m_animateWindows)
		theAnimateWindowManager->registerGameWindow( theWindow, WIN_ANIMATION_SLIDE_TOP, TRUE, 200 );

	TheInGameUI->registerWindowLayout(theLayout);
	grabWindowPointers();
	PopulateInGameDiplomacyPopup();

	if(TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() != 0)
	{
		radioButtonInGame->winHide(FALSE);
		radioButtonBuddies->winHide(FALSE);
		InitBuddyControls(1);
		PopulateOldBuddyMessages();
		updateBuddyInfo();
	}
	
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ResetDiplomacy( void )
{
	if(theLayout)
	{
		TheInGameUI->unregisterWindowLayout(theLayout);
		theLayout->destroyWindows();
		theLayout->deleteInstance();
		InitBuddyControls(-1);
	}
	theLayout = NULL;
	theWindow = NULL;
	if (theAnimateWindowManager)
		delete theAnimateWindowManager;
	theAnimateWindowManager = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void HideDiplomacy( Bool immediate )
{
	releaseWindowPointers();
	if (theWindow)
	{
		if (immediate || !TheGlobalData->m_animateWindows)
		{
			theWindow->winHide(TRUE);
			theWindow->winEnable(FALSE);
		}
		else
		{
			if (theAnimateWindowManager->isFinished())
				theAnimateWindowManager->reverseAnimateWindow();
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ToggleDiplomacy( Bool immediate )
{
	// If we bring this up, let's hide the quit menu
	HideQuitMenu();

	if (theWindow)
	{
		Bool show = theWindow->winIsHidden();
		if (show)
			ShowDiplomacy( immediate );
		else
			HideDiplomacy( immediate );
	}
	else
	{
		ShowDiplomacy( immediate );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType DiplomacyInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
//			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					HideDiplomacy();
					return MSG_HANDLED;
					//return MSG_IGNORED;
				}  // end escape

			}  // end switch( key )

			return MSG_HANDLED;

		}  // end char

		// --------------------------------------------------------------------------------------------
		// the wheel over a row's money controls moves its amount; every control there lets it through
		case GWM_WHEEL_UP:
		case GWM_WHEEL_DOWN:
		{
			Int row = moneyRowAt(LOLONGTOSHORT(mData1), HILONGTOSHORT(mData1));
			if (row < 0)
				return MSG_IGNORED;
			adjustMoneyToSend(row, msg == GWM_WHEEL_UP ? 1 : -1);
			return MSG_HANDLED;
		}

	}

	return MSG_IGNORED;

}  // end DiplomacyInput

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType DiplomacySystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	if(BuddyControlSystem(window, msg, mData1, mData2) == MSG_HANDLED)
	{
		return MSG_HANDLED;
	}
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GGM_FOCUS_CHANGE:
		{
//			Bool focus = (Bool) mData1;
			//if (focus)
				//TheWindowManager->winSetGrabWindow( chatTextEntry );
			break;
		} // end focus change

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're given the opportunity to take the keyboard focus we must say we don't want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = FALSE;

			return MSG_HANDLED;
		}//case GWM_INPUT_FOCUS:

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			NameKeyType controlID = (NameKeyType)control->winGetWindowId();
			static NameKeyType buttonHideID = NAMEKEY( "Diplomacy.wnd:ButtonHide" );
			if (controlID == buttonHideID)
			{
				HideDiplomacy( FALSE );
			}
			else if( controlID == radioButtonInGameID)
			{
				winInGame->winHide(FALSE);
				winBuddies->winHide(TRUE);
			}
			else if( controlID == radioButtonBuddiesID)
			{
				winInGame->winHide(TRUE);
				winBuddies->winHide(FALSE);
			}

			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (controlID == buttonMuteID[i] && slotNumInRow[i] >= 0)
				{
					TheGameInfo->getSlot(slotNumInRow[i])->mute(TRUE);
					PopulateInGameDiplomacyPopup();
					break;
				}
				if (controlID == buttonUnMuteID[i] && slotNumInRow[i] >= 0)
				{
					TheGameInfo->getSlot(slotNumInRow[i])->mute(FALSE);
					PopulateInGameDiplomacyPopup();
					break;
				}
				if (slotNumInRow[i] < 0)
					continue;
				if (controlID == buttonMoneyMinusID[i])
				{
					adjustMoneyToSend(i, -1);
					break;
				}
				if (controlID == buttonMoneyPlusID[i])
				{
					adjustMoneyToSend(i, 1);
					break;
				}
				if (controlID == buttonMoneySendID[i])
				{
					sendMoney(i);
					break;
				}
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end DiplomacySystem

void PopulateInGameDiplomacyPopup( void )
{
	if (!TheGameInfo)
		return;

	// the money column is for a player still in the match, and never in a replay, where the
	// transfers already recorded play back by themselves
	const Bool mayGive = !TheGameLogic->isInReplayGame() && ThePlayerList->getLocalPlayer()->isPlayerActive();
	if (staticTextMoneyTitle)
		staticTextMoneyTitle->winHide(!mayGive);

	Int rowNum = 0;
	for (Int slotNum=0; slotNum<MAX_SLOTS; ++slotNum)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(slotNum);
		if (slot && slot->isOccupied())
		{
			Bool isInGame = false;
			// Note - for skirmish, TheNetwork == NULL.  jba.
			if (TheNetwork &&	TheNetwork->isPlayerConnected(slotNum)) {
				isInGame = true;
			} else if ((TheNetwork == NULL) && slot->isHuman()) {
				// this is a skirmish game and it is the human player.
				isInGame = true;
			}
			if (slot->isAI())
				isInGame = true;
			AsciiString playerName;
			playerName.format("player%d", slotNum);
			Player *player = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
			Bool isAlive = !TheVictoryConditions->hasSinglePlayerBeenDefeated(player);
			Bool isObserver = player->isPlayerObserver();

			if (slot->isHuman() && TheGameInfo->getLocalSlotNum() != slotNum && isInGame)
			{
				// show mute button
				if (buttonMute[rowNum])
				{
					buttonMute[rowNum]->winHide(slot->isMuted());
				}
				if (buttonUnMute[rowNum])
				{
					buttonUnMute[rowNum]->winHide(!slot->isMuted());
				}
			}
			else
			{
				// can't mute self, AI players, or MIA humans
				if (buttonMute[rowNum])
					buttonMute[rowNum]->winHide(TRUE);
				if (buttonUnMute[rowNum])
					buttonUnMute[rowNum]->winHide(TRUE);
			}

			Color playerColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
			Color backColor = GameMakeColor(0, 0, 0, 255);
			Color aliveColor = GameMakeColor(0, 255, 0, 255);
			Color deadColor = GameMakeColor(255, 0, 0, 255);
			Color observerInGameColor = GameMakeColor(255, 255, 255, 255);
			Color goneColor = GameMakeColor(196, 0, 0, 255);
			Color observerGoneColor = GameMakeColor(196, 196, 196, 255);

			if (staticTextPlayer[rowNum])
			{
				staticTextPlayer[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				GadgetStaticTextSetText(staticTextPlayer[rowNum], slot->getName());
			}
			if (staticTextSide[rowNum])
			{
				staticTextSide[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				GadgetStaticTextSetText(staticTextSide[rowNum], slot->getApparentPlayerTemplateDisplayName() );
			}
			if (staticTextTeam[rowNum])
			{
				staticTextTeam[rowNum]->winSetEnabledTextColors( playerColor, backColor );
				AsciiString teamStr;
				teamStr.format("Team:%d", slot->getTeamNumber() + 1);
				if (slot->isAI() && slot->getTeamNumber() == -1)
					teamStr = "Team:AI";
				GadgetStaticTextSetText(staticTextTeam[rowNum], TheGameText->fetch(teamStr) );
			}
			if (staticTextStatus[rowNum])
			{
				staticTextStatus[rowNum]->winHide(FALSE);
				if (isInGame)
				{
					if (isAlive)
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( aliveColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerAlive"));
					}
					else
					{
						if (isObserver)
						{
							staticTextStatus[rowNum]->winSetEnabledTextColors( observerInGameColor, backColor );
							GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerObserver"));
						}
						else
						{
							staticTextStatus[rowNum]->winSetEnabledTextColors( deadColor, backColor );
							GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerDead"));
						}
					}
				}
				else
				{
					// not in game
					if (isObserver)
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( observerGoneColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerObserverGone"));
					}
					else
					{
						staticTextStatus[rowNum]->winSetEnabledTextColors( goneColor, backColor );
						GadgetStaticTextSetText(staticTextStatus[rowNum], TheGameText->fetch("GUI:PlayerGone"));
					}
				}
			}

			// the same test MSG_TRANSFER_MONEY makes in GameLogicDispatch.cpp: allies both ways, still playing
			Player *local = ThePlayerList->getLocalPlayer();
			const Bool canGiveTo = mayGive && TheGameInfo->getLocalSlotNum() != slotNum
														 && player->isPlayableSide() && player->isPlayerActive()
														 && local->getRelationship(player->getDefaultTeam()) == ALLIES
														 && player->getRelationship(local->getDefaultTeam()) == ALLIES;
			GameWindow *money[] = { buttonMoneyMinus[rowNum], staticTextMoney[rowNum], buttonMoneyPlus[rowNum], buttonMoneySend[rowNum] };
			for (Int c = 0; c < 4; ++c)
				if (money[c])
					money[c]->winHide(!canGiveTo);

			slotNumInRow[rowNum] = slotNum;
			showMoneyToSend(rowNum);
			++rowNum;
		}
	}

	while (rowNum < MAX_SLOTS)
	{
		slotNumInRow[rowNum] = -1;
		if (staticTextPlayer[rowNum])
			staticTextPlayer[rowNum]->winHide(TRUE);
		if (staticTextSide[rowNum])
			staticTextSide[rowNum]->winHide(TRUE);
		if (staticTextTeam[rowNum])
			staticTextTeam[rowNum]->winHide(TRUE);
		if (staticTextStatus[rowNum])
			staticTextStatus[rowNum]->winHide(TRUE);
		if (buttonMute[rowNum])
			buttonMute[rowNum]->winHide(TRUE);
		if (buttonUnMute[rowNum])
			buttonUnMute[rowNum]->winHide(TRUE);
		GameWindow *money[] = { buttonMoneyMinus[rowNum], staticTextMoney[rowNum], buttonMoneyPlus[rowNum], buttonMoneySend[rowNum] };
		for (Int c = 0; c < 4; ++c)
			if (money[c])
				money[c]->winHide(TRUE);

		++rowNum;
	}
}



