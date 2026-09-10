#include "NativeControls.h"


#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <climits>
#include <new>
#include <string_view>

#include "Lawn/Board.h"
#include "Lawn/Challenge.h"
#include "Lawn/CursorObject.h"
#include "Lawn/Cutscene.h"
#include "Lawn/LawnMower.h"
#include "Lawn/System/Music.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/GameButton.h"
#include "Lawn/Widget/SeedChooserScreen.h"
#include "Lawn/Widget/TitleScreen.h"
#include "LawnApp.h"
#include "PvzpLib/PvzpDebug.h"
#include "PvzpLib/PvzpParticle.h"
#include "SexyAppFramework/Common.h"
#include "SexyAppFramework/graphics/Graphics.h"
#include "SexyAppFramework/misc/MTRand.h"
#include "SexyAppFramework/widget/WidgetManager.h"


namespace PvzpNative {
	bool gWorldReplaced = false;
	bool gEventFrameOpen = false;
	std::uint64_t gBoardEpoch = 0;
	std::uint64_t gPendingCompletedRounds = 0;
	std::uint32_t gMorphPlaceholder = 0;
	std::uint32_t gMorphSuccessor = 0;
	bool gGameSpeedCaptured = false;
	int gOriginalFrameTime = 10;
	double gOriginalUpdateMultiplier = 1.0;
	bool gFastForward = false;
	std::int32_t gFastForwardPerformance = 0;
	bool gFastForwardSuppressWindow = false;
	bool gAcceleratedUpdateBatch = false;
	std::uint32_t gSeedChooserFastForwardRemaining = 0;
	bool gAdvancedPause = false;
	bool gAdvancedPauseRefreshCursor = true;
	bool gAdvancedPauseDrawMask = false;
	std::uint32_t gAdvancedPauseMaskRgba = 0x00000060U;
	bool gDirectSunProduction = false;
	std::int32_t gPendingSurvivalStage = -1;
	bool gPendingReadyCooldowns = false;
	bool gPendingWorldOrigin = false;
	std::uint32_t gPendingDancerClock = 0;
	RandomState gRandomStreams[2];
	RandomState gSavedRandomStreams[2];
	std::uint32_t gRandomConstructionSeed = 4357;
	bool gRandomConstructionActive = false;
	std::uint32_t gWaveSpawnRandomSeed = 0;
	Sexy::MTRand gWaveSpawnRandom(4357);
	bool gWaveSpawnRandomEnabled = false;
	bool gWaveSpawnActive = false;
	bool gWaveRowPickActive = false;
	bool gBypassRandomOverride = false;
	ModifierState gModifiers;
	bool gEasyPlantingCaptured = false;
	bool gEasyPlantingOriginal = false;
	void BeginRandomConstruction(std::uint32_t seed, bool lockedOnly)
	{
		if (gRandomConstructionActive)
			return;
		if (lockedOnly && !gRandomStreams[0].locked && !gRandomStreams[1].locked)
			return;
		gRandomConstructionActive = true;
		gRandomConstructionSeed = seed == 0 ? 4357 : seed;
		for (int i = 0; i < 2; ++i)
		{
			gSavedRandomStreams[i] = gRandomStreams[i];
			gRandomStreams[i].seed = gRandomConstructionSeed;
			gRandomStreams[i].fixed = 0;
			gRandomStreams[i].locked = false;
		}
		Sexy::SRand(gRandomConstructionSeed);
	}

	void EndRandomConstruction()
	{
		if (!gRandomConstructionActive)
			return;
		Sexy::SRand(gRandomConstructionSeed);
		for (int i = 0; i < 2; ++i)
		{
			gRandomStreams[i] = gSavedRandomStreams[i];
			gRandomStreams[i].seed = gRandomConstructionSeed;
		}
		gRandomConstructionActive = false;
	}
	void RestoreGameSpeed()
	{
		if (!gGameSpeedCaptured)
			return;
		if (gLawnApp)
		{
			gLawnApp->mFrameTime = gOriginalFrameTime;
			gLawnApp->mUpdateMultiplier = gOriginalUpdateMultiplier;
		}
		gGameSpeedCaptured = false;
	}
	struct EventAttempt
	{
		std::uint32_t token = 0;
		bool suppressed = false;
	};

	EventAttempt BeginPlantEffect(
		std::uint32_t interest, std::int32_t sourceKind, void* actor, Plant* plant,
		std::int32_t effectKind, std::int32_t requested)
	{
		if (!PvzpPlugin::BattleDispatchEnabled() || !(gLawnApp->mPlugin.battleInterest & interest))
			return {};
		PvzpPlugin::CallbackScope scope;
		const std::uint64_t packed = gLawnApp->mPlugin.battleCallbacks.beginPlantEffect(sourceKind, actor, plant, effectKind, requested);
		return {static_cast<std::uint32_t>(packed), (packed >> 32) != 0};
	}

	void FinishPlantEffect(const EventAttempt& attempt, std::int32_t outcome, std::int32_t applied = 0)
	{
		if (attempt.token)
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.finishPlantEffect(attempt.token, outcome, applied);
		}
	}

	void ApplyDirectPlantDamage(
		std::uint32_t interest, std::int32_t sourceKind, void* actor, Plant* plant, std::int32_t damage)
	{
		const EventAttempt attempt = BeginPlantEffect(interest, sourceKind, actor, plant, 0, damage);
		if (attempt.suppressed)
		{
			FinishPlantEffect(attempt, 0);
			return;
		}
		const std::int32_t before = plant->mPlantHealth;
		if (gModifiers.plantDamageRule == 1)
		{
			FinishPlantEffect(attempt, 1);
			return;
		}
		if (gModifiers.plantDamageRule == 2)
			plant->mPlantHealth = 0;
		else
			plant->mPlantHealth -= damage;
		FinishPlantEffect(attempt, plant->mPlantHealth <= 0 ? 3 : 2, before - plant->mPlantHealth);
	}
	void BoardCreated()
	{
		++gBoardEpoch;
		gWorldReplaced = true;
	}

	void BoardDestroying()
	{
		PvzpPlugin::BoardDestroying();
		if (gLawnApp && gLawnApp->mBoardResult == BoardResult::BOARDRESULT_WON)
			++gPendingCompletedRounds;
		++gBoardEpoch;
		gWorldReplaced = true;
	}

	void RoundCompleted()
	{
		++gPendingCompletedRounds;
	}

	bool BeforeUpdate()
	{
		if (gFastForward && (!gLawnApp || gLawnApp->mGameScene != GameScenes::SCENE_PLAYING
			|| !gLawnApp->mBoard || gLawnApp->mBoard->mPaused
			|| (gLawnApp->mWidgetManager && gLawnApp->mWidgetManager->mBaseModalWidget)))
		{
			gFastForward = false;
			gFastForwardPerformance = 0;
			gFastForwardSuppressWindow = false;
			return false;
		}
		if (PvzpPlugin::InCallback())
			return true;
		if (gPendingWorldOrigin && gLawnApp && gLawnApp->mBoard
			&& gLawnApp->mGameScene == GameScenes::SCENE_PLAYING)
		{
			gLawnApp->mBoard->mMainCounter = 0;
			gLawnApp->mAppCounter = static_cast<std::int32_t>(gPendingDancerClock);
			gPendingWorldOrigin = false;
		}
		const std::uint64_t completedRounds = gPendingCompletedRounds;
		gPendingCompletedRounds = 0;
		const bool replaced = gWorldReplaced;
		gWorldReplaced = false;
		const std::int32_t result = PvzpPlugin::Update(replaced, completedRounds);
		if (gLawnApp->mShutdown)
			return false;
		if (result == 0)
		{
			if (gSeedChooserFastForwardRemaining && gLawnApp && gLawnApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
				--gSeedChooserFastForwardRemaining;
			else if (gLawnApp && gLawnApp->mGameScene != GameScenes::SCENE_LEVEL_INTRO)
				gSeedChooserFastForwardRemaining = 0;
			if (gAdvancedPause)
			{
				if (gAdvancedPauseRefreshCursor && gLawnApp && gLawnApp->mBoard)
				{
					gLawnApp->mBoard->UpdateCursor();
					gLawnApp->mBoard->mCursorPreview->Update();
				}
				return false;
			}
			return true;
		}
		if (result == 1)
			return false;

		return false;
	}

	void DrawAdvancedPauseMask(Sexy::Graphics* graphics)
	{
		if (!gAdvancedPause || !gAdvancedPauseDrawMask || !graphics)
			return;
		graphics->SetColor(Sexy::Color(
			static_cast<int>((gAdvancedPauseMaskRgba >> 24) & 0xffU),
			static_cast<int>((gAdvancedPauseMaskRgba >> 16) & 0xffU),
			static_cast<int>((gAdvancedPauseMaskRgba >> 8) & 0xffU),
			static_cast<int>(gAdvancedPauseMaskRgba & 0xffU)));
		graphics->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
	}

	void BeginLogicFrame()
	{
		gEventFrameOpen = false;
		gMorphPlaceholder = 0;
		gMorphSuccessor = 0;
		if (PvzpPlugin::BattleDispatchEnabled() && gLawnApp && gLawnApp->mBoard)
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.beginLogicFrame(gBoardEpoch, static_cast<std::int32_t>(gLawnApp->mBoard->mMainCounter));
			gEventFrameOpen = true;
		}
	}

	void EndLogicFrame()
	{
		if (!PvzpPlugin::BattleDispatchEnabled() || !gLawnApp)
		{
			gEventFrameOpen = false;
			return;
		}
		std::int32_t status = 0;
		if (gLawnApp->mBoard && gLawnApp->mBoard->mLevelComplete)
			status = 1;
		else if (gLawnApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
			status = 2;
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.endLogicFrame(status);
		}
		gEventFrameOpen = false;
	}

	void Shutdown()
	{
		PvzpPlugin::Shutdown();
	}

#define PVZP_DEFINE_RULE_QUERY(name, member) bool name() { return gModifiers.member; }
	PVZP_DEFINE_RULE_QUERY(SeedRechargeIgnored, seedRechargeIgnored)
	PVZP_DEFINE_RULE_QUERY(SunCostIgnored, sunCostIgnored)
	PVZP_DEFINE_RULE_QUERY(FogRevealed, fogRevealed)
	PVZP_DEFINE_RULE_QUERY(VaseContentsVisible, vaseContentsVisible)
	PVZP_DEFINE_RULE_QUERY(InstantIceAndAshEffects, instantIceAndAshEffects)
	PVZP_DEFINE_RULE_QUERY(MushroomsAwake, mushroomsAwake)
	PVZP_DEFINE_RULE_QUERY(CobFixedDelay, cobFixedDelay)
	PVZP_DEFINE_RULE_QUERY(CobRechargeShortened, cobRechargeShortened)
	PVZP_DEFINE_RULE_QUERY(CobDriftFixed, cobDriftFixed)
	PVZP_DEFINE_RULE_QUERY(ItemDropDisabled, itemDropDisabled)
	PVZP_DEFINE_RULE_QUERY(NaturalSunDropDisabled, naturalSunDropDisabled)
	PVZP_DEFINE_RULE_QUERY(JackExplosionsDisabled, jackExplosionsDisabled)
	PVZP_DEFINE_RULE_QUERY(PepperExplosionsDisabled, pepperExplosionsDisabled)
	PVZP_DEFINE_RULE_QUERY(SpecialEventsDisabled, specialEventsDisabled)
	PVZP_DEFINE_RULE_QUERY(ZombieSpawnStopped, zombieSpawnStopped)
	PVZP_DEFINE_RULE_QUERY(ZombiesDieAtHouse, zombiesDieAtHouse)
	PVZP_DEFINE_RULE_QUERY(PlantingRestrictionsIgnored, plantingRestrictionsIgnored)
	PVZP_DEFINE_RULE_QUERY(ProfileReadonly, profileReadonly)
	PVZP_DEFINE_RULE_QUERY(NormalAutoCollectEnabled, normalAutoCollectEnabled)
#undef PVZP_DEFINE_RULE_QUERY

	int KernelPultProjectileRule() { return gModifiers.kernelPultProjectileRule; }
	int PlantDamageRule() { return gModifiers.plantDamageRule; }
	int MaidCheat() { return gModifiers.maidCheat; }

	int UpdateCount(int nativeCount)
	{
		gAcceleratedUpdateBatch = false;
		if (PvzpPlugin::InCallback())
			return nativeCount;
		if (gSeedChooserFastForwardRemaining && gLawnApp && gLawnApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
		{
			gAcceleratedUpdateBatch = true;
			return static_cast<int>(std::min<std::uint32_t>(
				gSeedChooserFastForwardRemaining, static_cast<std::uint32_t>(INT_MAX)));
		}
		if (gFastForward)
		{
			gAcceleratedUpdateBatch = true;
			return INT_MAX;
		}
		return nativeCount;
	}

	bool ContinueUpdateBatch(int updateIndex)
	{
		if (updateIndex == 0 || !gAcceleratedUpdateBatch)
			return true;
		return gFastForward || (gSeedChooserFastForwardRemaining && gLawnApp
			&& gLawnApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO);
	}

	int FastForwardPerformance()
	{
		return gFastForward ? gFastForwardPerformance : 0;
	}

	bool WindowUpdateSuppressed()
	{
		return (gFastForward && gFastForwardSuppressWindow)
			|| (gSeedChooserFastForwardRemaining && gLawnApp
				&& gLawnApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO);
	}


	void NoteBattleSeed(unsigned long seed)
	{
		gRandomStreams[0].seed = static_cast<std::uint32_t>(seed == 0 ? 4357 : seed);
	}

	bool OverrideRandom(Sexy::MTRand* random, unsigned long* value)
	{
		if (gBypassRandomOverride)
			return false;
		if (gWaveSpawnActive && gWaveRowPickActive && Sexy::IsBattleRandom(random))
		{
			gBypassRandomOverride = true;
			*value = gWaveSpawnRandom.NextNoAssert() & 0x7fffffffU;
			gBypassRandomOverride = false;
			return true;
		}
		RandomState& state = gRandomStreams[Sexy::IsBattleRandom(random) ? 0 : 1];
		if (!state.locked)
			return false;
		*value = state.fixed;
		return true;
	}

	void BeginSurvivalWaveInit()
	{
		BeginRandomConstruction(gRandomStreams[0].seed, true);
	}

	void EndSurvivalWaveInit()
	{
		EndRandomConstruction();
	}

	void BeginWaveSpawn(Board* board)
	{
		if (!gWaveSpawnRandomEnabled || !board)
			return;
		gWaveSpawnRandom.SRand(gWaveSpawnRandomSeed + static_cast<std::uint32_t>(board->mCurrentWave));
		gWaveSpawnActive = true;
	}

	void EndWaveSpawn()
	{
		gWaveRowPickActive = false;
		gWaveSpawnActive = false;
	}

	void BeginRowPick()
	{
		gWaveRowPickActive = gWaveSpawnActive;
	}

	void EndRowPick()
	{
		gWaveRowPickActive = false;
	}

	bool CreditProducedSun(Board* board, int coinType)
	{
		if (!gDirectSunProduction)
			return false;
		int value = 0;
		switch (static_cast<CoinType>(coinType))
		{
		case CoinType::COIN_SUN: value = 25; break;
		case CoinType::COIN_SMALLSUN: value = 15; break;
		case CoinType::COIN_LARGESUN: value = 50; break;
		default: return false;
		}
		board->AddSunMoney(value);
		return true;
	}

	void ApplyBite(Zombie* zombie, Plant* plant, int damage)
	{
		ApplyDirectPlantDamage(1U << 1, 1, zombie, plant, damage);
	}

	void ApplyBasketball(Projectile* projectile, Plant* plant, int damage)
	{
		ApplyDirectPlantDamage(1U << 3, 3, projectile, plant, damage);
	}

	void ApplyGargantuarSpikeDamage(Zombie* zombie, Plant* plant)
	{
		const EventAttempt attempt = BeginPlantEffect(1U << 2, 2, zombie, plant, 0, 50);
		if (attempt.suppressed)
		{
			FinishPlantEffect(attempt, 0);
			return;
		}
		const int before = plant->mPlantHealth;
		plant->SpikeRockTakeDamage();
		if (plant->mDead || plant->mPlantHealth <= 0)
			FinishPlantEffect(attempt, 3);
		else if (plant->mPlantHealth != before)
			FinishPlantEffect(attempt, 2, before - plant->mPlantHealth);
		else
			FinishPlantEffect(attempt, gModifiers.plantDamageRule == 1 ? 1 : 6);
	}

	void ApplyGargantuarSquish(Zombie* zombie, Plant* plant)
	{
		const EventAttempt attempt = BeginPlantEffect(1U << 2, 2, zombie, plant, 2, 0);
		if (attempt.suppressed)
		{
			FinishPlantEffect(attempt, 0);
			return;
		}
		if (gModifiers.plantDamageRule == 1)
		{
			FinishPlantEffect(attempt, 1);
			return;
		}
		const bool wasDead = plant->mDead;
		const bool wasSquished = plant->mSquished;
		const PlantState oldState = plant->mState;
		++plant->mBoard->mPlantsEaten;
		plant->Squish();
		if (!wasDead && plant->mDead)
			FinishPlantEffect(attempt, 3);
		else if (!wasSquished && plant->mSquished)
			FinishPlantEffect(attempt, 4);
		else if (oldState != plant->mState)
			FinishPlantEffect(attempt, 5);
		else
			FinishPlantEffect(attempt, gModifiers.plantDamageRule == 1 ? 1 : 6);
	}

	void ApplyJackPlantExplosion(Zombie* zombie, Board* board, int x, int y, int radius)
	{
		for (Plant* plant : board->mPlants)
		{
			if (plant->mDead || !GetCircleRectOverlap(x, y, radius, plant->GetPlantRect()))
				continue;
			const EventAttempt attempt = BeginPlantEffect(1U << 0, 0, zombie, plant, 1, 0);
			if (attempt.suppressed)
			{
				FinishPlantEffect(attempt, 0);
				continue;
			}
			if (gModifiers.plantDamageRule == 1)
			{
				FinishPlantEffect(attempt, 1);
				continue;
			}
			++board->mPlantsEaten;
			plant->Die();
			FinishPlantEffect(attempt, plant->mDead ? 3 : 6);
		}
	}

	void EmitHomeEntry(Zombie* zombie)
	{
		if (PvzpPlugin::BattleDispatchEnabled() && (gLawnApp->mPlugin.battleInterest & (1U << 4)))
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.emitHomeEntry(zombie);
		}
	}

	void EmitGargantuarSpawned(Zombie* zombie)
	{
		if (PvzpPlugin::BattleDispatchEnabled() && gEventFrameOpen && zombie && (gLawnApp->mPlugin.battleInterest & (1U << 5)))
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.emitGargantuarSpawned(zombie);
		}
	}

	void EmitImpThrown(Zombie* parent, Zombie* imp)
	{
		if (PvzpPlugin::BattleDispatchEnabled() && gEventFrameOpen && parent && imp && (gLawnApp->mPlugin.battleInterest & (1U << 5)))
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.emitImpThrown(parent, imp);
		}
	}

	void EmitGargantuarAshHit(Zombie* zombie)
	{
		if (PvzpPlugin::BattleDispatchEnabled() && gEventFrameOpen && zombie && (gLawnApp->mPlugin.battleInterest & (1U << 5)))
		{
			PvzpPlugin::CallbackScope scope;
			gLawnApp->mPlugin.battleCallbacks.emitGargantuarAshHit(zombie);
		}
	}

	void NoteImitaterMorph(Plant* placeholder, Plant* successor)
	{
		if (!placeholder || !successor || !placeholder->mBoard)
			return;
		gMorphPlaceholder = placeholder->mBoard->mPlants.DataArrayGetID(placeholder);
		gMorphSuccessor = placeholder->mBoard->mPlants.DataArrayGetID(successor);
	}
}

namespace PvzpNative {
void RestoreAll()
{
	RestoreGameSpeed();
	if (gEasyPlantingCaptured && gLawnApp)
		gLawnApp->mEasyPlantingCheat = gEasyPlantingOriginal;
	gEasyPlantingCaptured = false;
	gModifiers = {};
	gFastForward = false;
	gFastForwardPerformance = 0;
	gFastForwardSuppressWindow = false;
	gSeedChooserFastForwardRemaining = 0;
	gAdvancedPause = false;
	gAdvancedPauseDrawMask = false;
	gAdvancedPauseMaskRgba = 0x00000060U;
	gAdvancedPauseRefreshCursor = true;
	gDirectSunProduction = false;
	gPendingSurvivalStage = -1;
	gPendingReadyCooldowns = false;
	gPendingWorldOrigin = false;
	gPendingDancerClock = 0;
	gPendingCompletedRounds = 0;
	gWaveSpawnRandomEnabled = false;
	gWaveSpawnActive = false;
	gWaveRowPickActive = false;
	gBypassRandomOverride = false;
	gRandomConstructionActive = false;
	for (RandomState& state : gRandomStreams)
		state.Reset(0, 4357);
}

}
