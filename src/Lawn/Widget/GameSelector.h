/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __GAMESELECTOR_H__
#define __GAMESELECTOR_H__

#include "../../ConstEnums.h"
#include "widget/Widget.h"
#include "widget/ButtonListener.h"
#include "GameButton.h"
#include <memory>

class LawnApp;
class ToolTipWidget;
namespace Sexy
{
	class DialogButton;
}

using namespace Sexy;

enum SelectorAnimState
{
	SELECTOR_OPEN,
	SELECTOR_NEW_USER,
	SELECTOR_SHOW_SIGN,
	SELECTOR_IDLE
};

class GameSelector : public Widget, public ButtonListener
{
private:
	enum
	{
		GameSelector_Adventure = 100,
		GameSelector_Minigame,
		GameSelector_Puzzle,
		GameSelector_Options,
		GameSelector_Help,
		GameSelector_Quit,
		GameSelector_ChangeUser,
		GameSelector_Store,
		GameSelector_Almanac,
		GameSelector_ZenGarden,
		GameSelector_Survival
	};

public:
	LawnApp*                    mApp;
	// Non-owning: owned by the widget container (RemoveAllWidgets(true)); do not delete or wrap in unique_ptr.
	NewLawnButton*              mAdventureButton;
	NewLawnButton*              mMinigameButton;
	NewLawnButton*              mPuzzleButton;
	NewLawnButton*              mOptionsButton;
	NewLawnButton*              mQuitButton;
	NewLawnButton*              mHelpButton;
	NewLawnButton*              mStoreButton;
	NewLawnButton*              mAlmanacButton;
	NewLawnButton*              mZenGardenButton;
	NewLawnButton*              mSurvivalButton;
	NewLawnButton*              mChangeUserButton;
	Widget*                     mOverlayWidget;
	bool                        mStartingGame;
	int                         mStartingGameCounter;
	bool                        mMinigamesLocked;
	bool                        mPuzzleLocked;
	bool                        mSurvivalLocked;
	bool                        mShowStartButton;
	ParticleSystemID            mTrophyParticleID;
	ReanimationID               mSelectorReanimID;
	ReanimationID               mCloudReanimID[6];
	int                         mCloudCounter[6];
	ReanimationID               mFlowerReanimID[3];
	ReanimationID               mLeafReanimID;
	ReanimationID               mHandReanimID;
	int                         mLeafCounter;
	SelectorAnimState           mSelectorState;
	int                         mLevel;
	bool                        mLoading;
	std::unique_ptr<ToolTipWidget>      mToolTip;
	bool                        mHasTrophy;
	bool                        mUnlockSelectorCheat;

public:
	GameSelector(LawnApp* theApp);
	~GameSelector() override;

	void                        SyncProfile(bool theShowLoading);
	void                        Draw(Graphics* g) override;
	void                        DrawOverlay(Graphics* g) override;
	void                        Update() override;
	void                        ButtonMouseEnter(int theId) override;
	void                        ButtonPress(int theId) override;
	void                        ButtonDepress(int theId) override;
	void                        ButtonDownTick(int) override{}
	void                        ButtonMouseLeave(int) override{}
	void                        ButtonMouseMove(int, int, int) override{}
	void                        KeyDown(KeyCode theKey) override;
	void                        KeyChar(char theChar) override;
	void                        MouseDown(int x, int y, int theClickCount) override;
	void                        TrackButton(DialogButton* theButton, const char* theTrackName, float theOffsetX, float theOffsetY);
	void                        SyncButtons();
	void                        AddTrophySparkle();
	void                        ClickedAdventure();
	void                        UpdateTooltip();
	bool             ShouldDoZenTuturialBeforeAdventure();
	void                        AddPreviewProfiles();
};

class GameSelectorOverlay : public Widget
{
public:
	GameSelector*               mParent;

public:
	GameSelectorOverlay(GameSelector* theGameSelector);
	~GameSelectorOverlay() override { }

	void         Draw(Graphics* g) override;
};

#endif
