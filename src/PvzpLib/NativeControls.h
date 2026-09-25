#pragma once
#include "Plugin.h"
#include "SexyAppFramework/misc/MTRand.h"
class LawnApp;
class Board;
class Plant;
class Projectile;
class Zombie;
namespace Sexy { class Graphics; }

namespace PvzpNative {
    PVZP_API void BuildCurrentZombieWaves(Board* board, std::uint32_t seed);
    enum class CommonDance : std::int32_t { Default = -1, None = 0, Fast = 1, Slow = 2 };
	struct RandomState
	{
		std::uint32_t seed = 4357;
		std::uint32_t fixed = 0;
		bool locked = false;

		void Reset(std::int32_t mode, std::uint32_t value)
		{
			if (mode == 0)
			{
				seed = value == 0 ? 4357 : value;
				fixed = 0;
				locked = false;
			}
			else
			{
				fixed = value & 0x7fffffffU;
				locked = true;
			}
		}
	};	struct ModifierState
	{
		bool seedRechargeIgnored = false;
		bool sunCostIgnored = false;
		bool fogRevealed = false;
		bool vaseContentsVisible = false;
		std::int32_t plantSpecialCountdownRule = 0;
		bool mushroomsAwake = false;
		bool cobFixedDelay = false;
        bool disableCobImpactDelay = false;
        CommonDance commonZombieDance = CommonDance::Default;
		bool cobRechargeShortened = false;
		bool cobDriftFixed = false;
		bool itemDropDisabled = false;
		bool naturalSunDropDisabled = false;
		std::int32_t jackExplosionRule = 0;
		std::int32_t pepperExplosionRule = 0;
		bool specialEventsDisabled = false;
		bool zombieSpawnStopped = false;
		bool zombiesDieAtHouse = false;
		bool plantingRestrictionsIgnored = false;
		bool profileReadonly = false;
		bool normalAutoCollectEnabled = false;
		std::int32_t kernelPultProjectileRule = 0;
		std::int32_t plantDamageRule = 0;
        std::int32_t zombieDamageRule = 0;
        bool upgradePlantingUnrestricted = false;
        bool seedRechargeShadingHidden = false;
        bool seedBankTopmost = false;
		std::int32_t maidCheat = 0;
		std::int32_t sunlightLimit = 9990;
		std::int32_t moneyLimit = 99999;
		bool cratersExpireImmediately = false;
		bool chilledEffectsDisabled = false;
		bool butterEffectsDisabled = false;
		bool impThrowDisabled = false;
		bool zombieWalkingStopped = false;
		bool iceTrailsDisabled = false;
		std::int32_t backgroundRunRule = 0; // 0=native platform behavior, 1=PT background, 2=desktop focus pause
		bool pauseDialogDisabled = false;
		bool hiddenPagesVisible = false;
		bool temporaryUnlock = false;
	};
    extern bool gWorldReplaced;
    extern bool gEventFrameOpen;
    extern PVZP_API std::uint64_t gBoardEpoch;
    extern PVZP_API std::uint64_t gPendingCompletedRounds;
    extern PVZP_API std::uint32_t gMorphPlaceholder;
    extern PVZP_API std::uint32_t gMorphSuccessor;
    extern PVZP_API bool gGameSpeedCaptured;
    extern PVZP_API double gOriginalUpdateMultiplier;
    extern PVZP_API bool gFastForward;
    extern PVZP_API std::int32_t gFastForwardPerformance;
    extern PVZP_API bool gFastForwardSuppressWindow;
    extern bool gAcceleratedUpdateBatch;
    extern PVZP_API std::uint32_t gSeedChooserFastForwardRemaining;
    extern PVZP_API bool gAdvancedPause;
    extern PVZP_API bool gAdvancedPauseRefreshCursor;
    extern PVZP_API bool gAdvancedPauseDrawMask;
    extern PVZP_API std::uint32_t gAdvancedPauseMaskRgba;
    extern PVZP_API bool gDirectSunProduction;
    extern PVZP_API std::int32_t gPendingSurvivalStage;
    extern PVZP_API bool gPendingReadyCooldowns;
    extern PVZP_API bool gPendingWorldOrigin;
    extern PVZP_API std::uint32_t gPendingDancerClock;
    extern PVZP_API RandomState gRandomStreams[2];
    extern RandomState gSavedRandomStreams[2];
    extern std::uint32_t gRandomConstructionSeed;
    extern bool gRandomConstructionActive;
    extern PVZP_API std::uint32_t gWaveSpawnRandomSeed;
    extern Sexy::MTRand gWaveSpawnRandom;
    extern PVZP_API bool gWaveSpawnRandomEnabled;
    extern PVZP_API bool gWaveSpawnActive;
    extern PVZP_API bool gWaveRowPickActive;
    extern bool gBypassRandomOverride;
    extern PVZP_API ModifierState gModifiers;
    extern PVZP_API bool gEasyPlantingCaptured;
    extern PVZP_API bool gEasyPlantingOriginal;

PVZP_API void BeginRandomConstruction(std::uint32_t seed, bool lockedOnly);
PVZP_API void EndRandomConstruction();
PVZP_API void RestoreGameSpeed();
PVZP_API void SetTemporaryUnlock(bool enabled);
PVZP_API void RestoreAll();
	struct RandomConstructionGuard
	{
		explicit RandomConstructionGuard(std::uint32_t seed)
		{
			BeginRandomConstruction(seed, false);
		}

		~RandomConstructionGuard()
		{
			EndRandomConstruction();
		}
	};

}
namespace PvzpNative
{
	void BoardCreated();
	void BoardDestroying();
	void RoundCompleted();
	bool BeforeUpdate();
	void DrawAdvancedPauseMask(Sexy::Graphics* graphics);
	void BeginLogicFrame();
	void EndLogicFrame();
	void Shutdown();

	bool SeedRechargeIgnored();
	bool SunCostIgnored();
	bool FogRevealed();
	bool VaseContentsVisible();
	int KernelPultProjectileRule();
	bool InstantIceAndAshEffects();
    int PlantSpecialCountdownRule();
	bool MushroomsAwake();
	bool CobFixedDelay();
	bool CobRechargeShortened();
	bool CobDriftFixed();
	bool ItemDropDisabled();
	bool NaturalSunDropDisabled();
	bool JackExplosionsDisabled();
    int JackExplosionRule();
	bool PepperExplosionsDisabled();
    int PepperExplosionRule();
	bool SpecialEventsDisabled();
	bool ZombieSpawnStopped();
	bool ZombiesDieAtHouse();
	bool PlantingRestrictionsIgnored();
	bool ProfileReadonly();
	bool NormalAutoCollectEnabled();
	int PlantDamageRule();
    int ZombieDamageRule();
	int MaidCheat();

	void ApplyBite(Zombie* zombie, Plant* plant, int damage);
	void ApplyBasketball(Projectile* projectile, Plant* plant, int damage);
	void ApplyGargantuarSpikeDamage(Zombie* zombie, Plant* plant);
	void ApplyZombieSquish(Zombie* zombie, Plant* plant, int source);
	void ApplyBungeeLift(Zombie* zombie, Plant* plant);
	void ApplyJackPlantExplosion(Zombie* zombie, Board* board, int x, int y, int radius);
	void EmitHomeEntry(Zombie* zombie);
	void EmitGargantuarSpawned(Zombie* zombie);
	void EmitImpThrown(Zombie* parent, Zombie* imp);
	void EmitGargantuarAshHit(Zombie* zombie);
	void NoteImitaterMorph(Plant* placeholder, Plant* successor);
	int UpdateCount(int nativeCount);
	bool ContinueUpdateBatch(int updateIndex);
	int FastForwardPerformance();
	bool WindowUpdateSuppressed();
	void NoteBattleSeed(unsigned long seed);
	bool OverrideRandom(Sexy::MTRand* random, unsigned long* value);
	void BeginSurvivalWaveInit();
	void EndSurvivalWaveInit();
	void BeginWaveSpawn(Board* board);
	void EndWaveSpawn();
	void BeginRowPick();
	void EndRowPick();
	bool CreditProducedSun(Board* board, int coinType);
}
