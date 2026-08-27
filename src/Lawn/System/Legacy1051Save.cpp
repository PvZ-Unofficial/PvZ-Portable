/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "Legacy1051Save.h"

#include "Music.h"
#include "../Board.h"
#include "../Challenge.h"
#include "../Coin.h"
#include "../CursorObject.h"
#include "../GridItem.h"
#include "../LawnMower.h"
#include "../MessageWidget.h"
#include "../Plant.h"
#include "../Projectile.h"
#include "../SeedPacket.h"
#include "../Zombie.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../../PvzpLib/Attachment.h"
#include "../../PvzpLib/DataArray.h"
#include "../../PvzpLib/EffectSystem.h"
#include "../../PvzpLib/PvzpParticle.h"
#include "../../PvzpLib/PvzpDebug.h"
#include "../../PvzpLib/Reanimator.h"
#include "../../PvzpLib/Trail.h"
#include "../../SexyAppFramework/misc/Buffer.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace Sexy;

namespace
{
constexpr uint32_t LEGACY_MAGIC = 0xFEEDDEADU;
constexpr uint32_t LEGACY_VERSION = 2U;
constexpr uint32_t LEGACY_1051_BUILD_DATE = 0xB7EFF6A8U;

constexpr size_t LEGACY_BOARD_TAIL_SIZE = 0x564CU;
constexpr uint32_t LEGACY_NEXT_KEY = 1001U;

constexpr uint32_t ZOMBIE_SLOT_SIZE = 348U;
constexpr uint32_t PLANT_SLOT_SIZE = 332U;
constexpr uint32_t PROJECTILE_SLOT_SIZE = 148U;
constexpr uint32_t COIN_SLOT_SIZE = 216U;
constexpr uint32_t MOWER_SLOT_SIZE = 72U;
constexpr uint32_t GRID_ITEM_SLOT_SIZE = 236U;
constexpr uint32_t PARTICLE_SYSTEM_SLOT_SIZE = 44U;
constexpr uint32_t PARTICLE_EMITTER_SLOT_SIZE = 176U;
constexpr uint32_t PARTICLE_SLOT_SIZE = 160U;
constexpr uint32_t REANIMATION_SLOT_SIZE = 160U;
constexpr uint32_t TRAIL_SLOT_SIZE = 236U;
constexpr uint32_t ATTACHMENT_SLOT_SIZE = 780U;

class ByteView
{
public:
	const unsigned char* mData = nullptr;
	size_t mSize = 0;

	bool ReadU8(size_t theOffset, uint8_t& theValue) const
	{
		if (theOffset >= mSize)
			return false;
		theValue = mData[theOffset];
		return true;
	}

	bool ReadU32(size_t theOffset, uint32_t& theValue) const
	{
		if (theOffset > mSize || mSize - theOffset < 4U)
			return false;
		theValue = static_cast<uint32_t>(mData[theOffset]) |
			(static_cast<uint32_t>(mData[theOffset + 1U]) << 8U) |
			(static_cast<uint32_t>(mData[theOffset + 2U]) << 16U) |
			(static_cast<uint32_t>(mData[theOffset + 3U]) << 24U);
		return true;
	}

	bool ReadI32(size_t theOffset, int32_t& theValue) const
	{
		uint32_t aValue = 0;
		if (!ReadU32(theOffset, aValue))
			return false;
		theValue = static_cast<int32_t>(aValue);
		return true;
	}

	bool ReadI64(size_t theOffset, int64_t& theValue) const
	{
		uint32_t aLow = 0;
		uint32_t aHigh = 0;
		if (!ReadU32(theOffset, aLow) || !ReadU32(theOffset + 4U, aHigh))
			return false;
		uint64_t aValue = static_cast<uint64_t>(aLow) | (static_cast<uint64_t>(aHigh) << 32U);
		theValue = static_cast<int64_t>(aValue);
		return true;
	}

	bool ReadFloat(size_t theOffset, float& theValue) const
	{
		uint32_t aBits = 0;
		if (!ReadU32(theOffset, aBits))
			return false;
		static_assert(sizeof(aBits) == sizeof(theValue));
		std::memcpy(&theValue, &aBits, sizeof(theValue));
		return true;
	}

	bool Copy(size_t theOffset, void* theDest, size_t theSize) const
	{
		if (theOffset > mSize || theSize > mSize - theOffset)
			return false;
		std::memcpy(theDest, mData + theOffset, theSize);
		return true;
	}

	ByteView Slice(size_t theOffset, size_t theSize) const
	{
		if (theOffset > mSize || theSize > mSize - theOffset)
			return {};
		return { mData + theOffset, theSize };
	}
};

class LegacyReader
{
public:
	ByteView mBytes;
	size_t mPosition = 0;

	bool ReadU32(uint32_t& theValue)
	{
		if (!mBytes.ReadU32(mPosition, theValue))
			return false;
		mPosition += 4U;
		return true;
	}

	bool ReadI32(int32_t& theValue)
	{
		uint32_t aValue = 0;
		if (!ReadU32(aValue))
			return false;
		theValue = static_cast<int32_t>(aValue);
		return true;
	}

	bool ReadSyncBlock(size_t theExpectedSize, ByteView& theBlock)
	{
		uint32_t aSize = 0;
		if (!ReadU32(aSize) || aSize != theExpectedSize)
			return false;
		if (mPosition > mBytes.mSize || aSize > mBytes.mSize - mPosition)
			return false;
		theBlock = mBytes.Slice(mPosition, aSize);
		mPosition += aSize;
		return true;
	}

	bool AtEnd() const
	{
		return mPosition == mBytes.mSize;
	}
};

template <typename T>
bool ReadI32Field(const ByteView& theBlock, size_t theOffset, T& theDest)
{
	int32_t aValue = 0;
	if (!theBlock.ReadI32(theOffset, aValue))
		return false;
	theDest = static_cast<T>(aValue);
	return true;
}

template <typename T>
bool ReadU32Field(const ByteView& theBlock, size_t theOffset, T& theDest)
{
	uint32_t aValue = 0;
	if (!theBlock.ReadU32(theOffset, aValue))
		return false;
	theDest = static_cast<T>(aValue);
	return true;
}

bool ReadFloatField(const ByteView& theBlock, size_t theOffset, float& theDest)
{
	return theBlock.ReadFloat(theOffset, theDest);
}

bool ReadBoolField(const ByteView& theBlock, size_t theOffset, bool& theDest)
{
	uint8_t aValue = 0;
	if (!theBlock.ReadU8(theOffset, aValue) || aValue > 1U)
		return false;
	theDest = aValue != 0U;
	return true;
}

template <typename T>
bool ReadI32Array(const ByteView& theBlock, size_t theOffset, T* theDest, size_t theCount)
{
	for (size_t i = 0; i < theCount; i++)
		if (!ReadI32Field(theBlock, theOffset + i * 4U, theDest[i]))
			return false;
	return true;
}

template <typename T>
bool ReadU32Array(const ByteView& theBlock, size_t theOffset, T* theDest, size_t theCount)
{
	for (size_t i = 0; i < theCount; i++)
		if (!ReadU32Field(theBlock, theOffset + i * 4U, theDest[i]))
			return false;
	return true;
}

bool ReadFloatArray(const ByteView& theBlock, size_t theOffset, float* theDest, size_t theCount)
{
	for (size_t i = 0; i < theCount; i++)
		if (!ReadFloatField(theBlock, theOffset + i * 4U, theDest[i]))
			return false;
	return true;
}

bool ReadBoolArray(const ByteView& theBlock, size_t theOffset, bool* theDest, size_t theCount)
{
	for (size_t i = 0; i < theCount; i++)
		if (!ReadBoolField(theBlock, theOffset + i, theDest[i]))
			return false;
	return true;
}

bool ReadRect(const ByteView& theBlock, size_t theOffset, Rect& theRect)
{
	return ReadI32Field(theBlock, theOffset, theRect.mX) &&
		ReadI32Field(theBlock, theOffset + 4U, theRect.mY) &&
		ReadI32Field(theBlock, theOffset + 8U, theRect.mWidth) &&
		ReadI32Field(theBlock, theOffset + 12U, theRect.mHeight);
}

bool ReadVector2(const ByteView& theBlock, size_t theOffset, SexyVector2& theVector)
{
	return ReadFloatField(theBlock, theOffset, theVector.x) &&
		ReadFloatField(theBlock, theOffset + 4U, theVector.y);
}

bool ReadColor(const ByteView& theBlock, size_t theOffset, Color& theColor)
{
	return ReadI32Field(theBlock, theOffset, theColor.mRed) &&
		ReadI32Field(theBlock, theOffset + 4U, theColor.mGreen) &&
		ReadI32Field(theBlock, theOffset + 8U, theColor.mBlue) &&
		ReadI32Field(theBlock, theOffset + 12U, theColor.mAlpha);
}

bool ReadMatrix(const ByteView& theBlock, size_t theOffset, SexyTransform2D& theMatrix)
{
	for (size_t aRow = 0; aRow < 3U; aRow++)
		for (size_t aColumn = 0; aColumn < 3U; aColumn++)
			if (!ReadFloatField(theBlock, theOffset + (aRow * 3U + aColumn) * 4U, theMatrix.m[aRow][aColumn]))
				return false;
	return true;
}

struct LegacyArrayData
{
	uint32_t mFreeListHead = 0;
	uint32_t mMaxUsedCount = 0;
	uint32_t mSize = 0;
	uint32_t mSlotSize = 0;
	uint32_t mIdOffset = 0;
	ByteView mBlock;
	std::vector<uint32_t> mIds;

	bool IsActive(size_t theIndex) const
	{
		return theIndex < mIds.size() && (mIds[theIndex] & DATA_ARRAY_KEY_MASK) != 0U;
	}

	bool HasActiveId(uint32_t theId) const
	{
		uint32_t anIndex = theId & DATA_ARRAY_INDEX_MASK;
		return anIndex < mIds.size() && mIds[anIndex] == theId && IsActive(anIndex);
	}

	ByteView Slot(size_t theIndex) const
	{
		return mBlock.Slice(theIndex * mSlotSize, mSlotSize);
	}
};

bool ReadArray(LegacyReader& theReader, uint32_t theMaxSize, uint32_t theSlotSize, uint32_t theIdOffset, LegacyArrayData& theArray)
{
	theArray.mSlotSize = theSlotSize;
	theArray.mIdOffset = theIdOffset;
	if (!theReader.ReadU32(theArray.mFreeListHead) ||
		!theReader.ReadU32(theArray.mMaxUsedCount) ||
		!theReader.ReadU32(theArray.mSize))
		return false;
	if (theArray.mMaxUsedCount > theMaxSize || theArray.mSize > theArray.mMaxUsedCount)
		return false;
	if (theArray.mMaxUsedCount > std::numeric_limits<uint32_t>::max() / theSlotSize)
		return false;
	if (!theReader.ReadSyncBlock(theArray.mMaxUsedCount * theSlotSize, theArray.mBlock))
		return false;

	theArray.mIds.resize(theArray.mMaxUsedCount);
	uint32_t anActiveCount = 0;
	for (uint32_t i = 0; i < theArray.mMaxUsedCount; i++)
	{
		if (!theArray.mBlock.ReadU32(static_cast<size_t>(i) * theSlotSize + theIdOffset, theArray.mIds[i]))
			return false;
		if ((theArray.mIds[i] & DATA_ARRAY_KEY_MASK) != 0U)
		{
			if ((theArray.mIds[i] & DATA_ARRAY_INDEX_MASK) != i)
				return false;
			anActiveCount++;
		}
		else if (theArray.mIds[i] > theArray.mMaxUsedCount)
		{
			return false;
		}
	}
	if (anActiveCount != theArray.mSize || theArray.mFreeListHead > theArray.mMaxUsedCount)
		return false;

	std::vector<bool> aFreeSlots(theArray.mMaxUsedCount, false);
	uint32_t aFreeIndex = theArray.mFreeListHead;
	while (aFreeIndex < theArray.mMaxUsedCount)
	{
		if (theArray.IsActive(aFreeIndex) || aFreeSlots[aFreeIndex])
			return false;
		aFreeSlots[aFreeIndex] = true;
		aFreeIndex = theArray.mIds[aFreeIndex];
		if (aFreeIndex > theArray.mMaxUsedCount)
			return false;
	}
	for (uint32_t i = 0; i < theArray.mMaxUsedCount; i++)
		if (theArray.IsActive(i) == aFreeSlots[i])
			return false;
	return true;
}

struct LegacyEmitterExtra
{
	int32_t mDefinitionIndex = -1;
	int32_t mImageId = -1;
	std::vector<uint32_t> mParticleIds;
};

struct LegacyParticleSystemExtra
{
	int32_t mDefinitionIndex = -1;
	std::vector<uint32_t> mEmitterIds;
	std::vector<LegacyEmitterExtra> mEmitters;
};

struct LegacyReanimationExtra
{
	int32_t mDefinitionIndex = -1;
	ByteView mTrackBlock;
	std::vector<int32_t> mImageIds;
};

struct LegacySaveData
{
	ByteView mBoard;
	LegacyArrayData mZombies;
	LegacyArrayData mPlants;
	LegacyArrayData mProjectiles;
	LegacyArrayData mCoins;
	LegacyArrayData mMowers;
	LegacyArrayData mGridItems;
	LegacyArrayData mParticleSystems;
	LegacyArrayData mEmitters;
	LegacyArrayData mParticles;
	LegacyArrayData mReanimations;
	LegacyArrayData mTrails;
	LegacyArrayData mAttachments;
	std::vector<LegacyParticleSystemExtra> mParticleSystemExtras;
	std::vector<LegacyReanimationExtra> mReanimationExtras;
	std::vector<int32_t> mTrailDefinitionIndexes;
	ByteView mCursor;
	ByteView mCursorPreview;
	ByteView mAdvice;
	ByteView mSeedBank;
	ByteView mChallenge;
	ByteView mMusic;
};

bool IsValidImageId(int32_t theImageId)
{
	return theImageId >= 0 && theImageId <= static_cast<int32_t>(ResourceId::RESOURCE_ID_MAX);
}

bool ReadIdList(LegacyReader& theReader, const LegacyArrayData& theTarget, std::vector<uint32_t>& theIds)
{
	int32_t aCount = 0;
	if (!theReader.ReadI32(aCount) || aCount < 0 || static_cast<uint32_t>(aCount) > theTarget.mSize)
		return false;
	theIds.reserve(aCount);
	std::vector<bool> aSeen(theTarget.mMaxUsedCount, false);
	for (int32_t i = 0; i < aCount; i++)
	{
		ByteView anIdBlock;
		uint32_t anId = 0;
		if (!theReader.ReadSyncBlock(4U, anIdBlock) || !anIdBlock.ReadU32(0U, anId) || !theTarget.HasActiveId(anId))
			return false;
		uint32_t anIndex = anId & DATA_ARRAY_INDEX_MASK;
		if (aSeen[anIndex])
			return false;
		aSeen[anIndex] = true;
		theIds.push_back(anId);
	}
	return true;
}

bool ParseLegacySave(LegacyReader& theReader, LegacySaveData& theSave)
{
	ByteView aHeader;
	uint32_t aMagic = 0;
	uint32_t aVersion = 0;
	uint32_t aBuildDate = 0;
	if (!theReader.ReadSyncBlock(12U, aHeader) ||
		!aHeader.ReadU32(0U, aMagic) ||
		!aHeader.ReadU32(4U, aVersion) ||
		!aHeader.ReadU32(8U, aBuildDate) ||
		aMagic != LEGACY_MAGIC || aVersion != LEGACY_VERSION || aBuildDate != LEGACY_1051_BUILD_DATE)
		return false;

	if (!theReader.ReadSyncBlock(LEGACY_BOARD_TAIL_SIZE, theSave.mBoard) ||
		!ReadArray(theReader, 1024U, ZOMBIE_SLOT_SIZE, 344U, theSave.mZombies) ||
		!ReadArray(theReader, 1024U, PLANT_SLOT_SIZE, 328U, theSave.mPlants) ||
		!ReadArray(theReader, 1024U, PROJECTILE_SLOT_SIZE, 144U, theSave.mProjectiles) ||
		!ReadArray(theReader, 1024U, COIN_SLOT_SIZE, 208U, theSave.mCoins) ||
		!ReadArray(theReader, 32U, MOWER_SLOT_SIZE, 68U, theSave.mMowers) ||
		!ReadArray(theReader, 128U, GRID_ITEM_SLOT_SIZE, 232U, theSave.mGridItems) ||
		!ReadArray(theReader, 1024U, PARTICLE_SYSTEM_SLOT_SIZE, 40U, theSave.mParticleSystems) ||
		!ReadArray(theReader, 1024U, PARTICLE_EMITTER_SLOT_SIZE, 172U, theSave.mEmitters) ||
		!ReadArray(theReader, 1024U, PARTICLE_SLOT_SIZE, 156U, theSave.mParticles) ||
		!ReadArray(theReader, 1024U, REANIMATION_SLOT_SIZE, 156U, theSave.mReanimations) ||
		!ReadArray(theReader, 1024U, TRAIL_SLOT_SIZE, 232U, theSave.mTrails) ||
		!ReadArray(theReader, 1024U, ATTACHMENT_SLOT_SIZE, 776U, theSave.mAttachments))
		return false;

	std::vector<bool> anEmitterOwners(theSave.mEmitters.mMaxUsedCount, false);
	std::vector<bool> aParticleOwners(theSave.mParticles.mMaxUsedCount, false);
	for (uint32_t i = 0; i < theSave.mParticleSystems.mMaxUsedCount; i++)
	{
		if (!theSave.mParticleSystems.IsActive(i))
			continue;
		LegacyParticleSystemExtra aSystem;
		if (!theReader.ReadI32(aSystem.mDefinitionIndex))
			return false;
		PvzpParticleDefinition* aDefinition = nullptr;
		if (aSystem.mDefinitionIndex == static_cast<int32_t>(ParticleEffect::PARTICLE_NONE))
		{
			aDefinition = nullptr;
		}
		else if (aSystem.mDefinitionIndex >= 0 && aSystem.mDefinitionIndex < gParticleDefCount)
		{
			aDefinition = &gParticleDefArray[aSystem.mDefinitionIndex];
		}
		else
		{
			return false;
		}
		if (!ReadIdList(theReader, theSave.mEmitters, aSystem.mEmitterIds))
			return false;
		if (!aDefinition && !aSystem.mEmitterIds.empty())
			return false;

		for (uint32_t anEmitterId : aSystem.mEmitterIds)
		{
			uint32_t anEmitterIndex = anEmitterId & DATA_ARRAY_INDEX_MASK;
			if (anEmitterOwners[anEmitterIndex])
				return false;
			anEmitterOwners[anEmitterIndex] = true;
			LegacyEmitterExtra anEmitter;
			if (!theReader.ReadI32(anEmitter.mDefinitionIndex) ||
				!aDefinition || anEmitter.mDefinitionIndex < 0 || anEmitter.mDefinitionIndex >= aDefinition->mEmitterDefCount ||
				!theReader.ReadI32(anEmitter.mImageId) || !IsValidImageId(anEmitter.mImageId) ||
				!ReadIdList(theReader, theSave.mParticles, anEmitter.mParticleIds))
				return false;
			for (uint32_t aParticleId : anEmitter.mParticleIds)
			{
				uint32_t aParticleIndex = aParticleId & DATA_ARRAY_INDEX_MASK;
				if (aParticleOwners[aParticleIndex])
					return false;
				aParticleOwners[aParticleIndex] = true;
			}
			aSystem.mEmitters.push_back(std::move(anEmitter));
		}
		theSave.mParticleSystemExtras.push_back(std::move(aSystem));
	}
	for (uint32_t i = 0; i < theSave.mEmitters.mMaxUsedCount; i++)
		if (theSave.mEmitters.IsActive(i) != anEmitterOwners[i])
			return false;
	for (uint32_t i = 0; i < theSave.mParticles.mMaxUsedCount; i++)
		if (theSave.mParticles.IsActive(i) != aParticleOwners[i])
			return false;

	for (uint32_t i = 0; i < theSave.mReanimations.mMaxUsedCount; i++)
	{
		if (!theSave.mReanimations.IsActive(i))
			continue;
		LegacyReanimationExtra aReanimation;
		if (!theReader.ReadI32(aReanimation.mDefinitionIndex) ||
			aReanimation.mDefinitionIndex < 0 ||
			aReanimation.mDefinitionIndex >= static_cast<int32_t>(ReanimationType::NUM_REANIMS))
			return false;
		ReanimatorEnsureDefinitionLoaded(static_cast<ReanimationType>(aReanimation.mDefinitionIndex), true);
		ReanimatorDefinition& aDefinition = gReanimatorDefArray[aReanimation.mDefinitionIndex];
		if (aDefinition.mTracks.count < 0 ||
			static_cast<size_t>(aDefinition.mTracks.count) > std::numeric_limits<uint32_t>::max() / 96U)
			return false;
		if (aDefinition.mTracks.count > 0 &&
			!theReader.ReadSyncBlock(static_cast<size_t>(aDefinition.mTracks.count) * 96U, aReanimation.mTrackBlock))
			return false;
		aReanimation.mImageIds.resize(aDefinition.mTracks.count);
		for (int aTrackIndex = 0; aTrackIndex < aDefinition.mTracks.count; aTrackIndex++)
			if (!theReader.ReadI32(aReanimation.mImageIds[aTrackIndex]) || !IsValidImageId(aReanimation.mImageIds[aTrackIndex]))
				return false;
		theSave.mReanimationExtras.push_back(std::move(aReanimation));
	}

	for (uint32_t i = 0; i < theSave.mTrails.mMaxUsedCount; i++)
	{
		if (!theSave.mTrails.IsActive(i))
			continue;
		int32_t aDefinitionIndex = 0;
		if (!theReader.ReadI32(aDefinitionIndex) ||
			aDefinitionIndex < 0 || aDefinitionIndex >= gTrailDefCount)
			return false;
		theSave.mTrailDefinitionIndexes.push_back(aDefinitionIndex);
	}

	if (!theReader.ReadSyncBlock(76U, theSave.mCursor) ||
		!theReader.ReadSyncBlock(44U, theSave.mCursorPreview) ||
		!theReader.ReadSyncBlock(796U, theSave.mAdvice) ||
		!theReader.ReadSyncBlock(848U, theSave.mSeedBank) ||
		!theReader.ReadSyncBlock(188U, theSave.mChallenge) ||
		!theReader.ReadSyncBlock(76U, theSave.mMusic))
		return false;

	uint32_t aTailMagic = 0;
	return theReader.ReadU32(aTailMagic) && aTailMagic == LEGACY_MAGIC && theReader.AtEnd();
}

bool ReadGameObject(const ByteView& theBlock, GameObject& theObject)
{
	return ReadI32Field(theBlock, 8U, theObject.mX) &&
		ReadI32Field(theBlock, 12U, theObject.mY) &&
		ReadI32Field(theBlock, 16U, theObject.mWidth) &&
		ReadI32Field(theBlock, 20U, theObject.mHeight) &&
		ReadBoolField(theBlock, 24U, theObject.mVisible) &&
		ReadI32Field(theBlock, 28U, theObject.mRow) &&
		ReadI32Field(theBlock, 32U, theObject.mRenderOrder);
}

bool ReadMagnetItem(const ByteView& theBlock, size_t theOffset, MagnetItem& theItem)
{
	return ReadFloatField(theBlock, theOffset, theItem.mPosX) &&
		ReadFloatField(theBlock, theOffset + 4U, theItem.mPosY) &&
		ReadFloatField(theBlock, theOffset + 8U, theItem.mDestOffsetX) &&
		ReadFloatField(theBlock, theOffset + 12U, theItem.mDestOffsetY) &&
		ReadI32Field(theBlock, theOffset + 16U, theItem.mItemType);
}

bool ReadZombie(const ByteView& theBlock, Zombie& theZombie)
{
	if (!ReadGameObject(theBlock, theZombie) ||
		!ReadI32Field(theBlock, 36U, theZombie.mZombieType) ||
		!ReadI32Field(theBlock, 40U, theZombie.mZombiePhase) ||
		!ReadFloatField(theBlock, 44U, theZombie.mPosX) ||
		!ReadFloatField(theBlock, 48U, theZombie.mPosY) ||
		!ReadFloatField(theBlock, 52U, theZombie.mVelX) ||
		!ReadI32Field(theBlock, 56U, theZombie.mAnimCounter) ||
		!ReadI32Field(theBlock, 60U, theZombie.mGroanCounter) ||
		!ReadI32Field(theBlock, 64U, theZombie.mAnimTicksPerFrame) ||
		!ReadI32Field(theBlock, 68U, theZombie.mAnimFrames) ||
		!ReadI32Field(theBlock, 72U, theZombie.mFrame) ||
		!ReadI32Field(theBlock, 76U, theZombie.mPrevFrame) ||
		!ReadBoolField(theBlock, 80U, theZombie.mVariant) ||
		!ReadBoolField(theBlock, 81U, theZombie.mIsEating) ||
		!ReadI32Field(theBlock, 84U, theZombie.mJustGotShotCounter) ||
		!ReadI32Field(theBlock, 88U, theZombie.mShieldJustGotShotCounter) ||
		!ReadI32Field(theBlock, 92U, theZombie.mShieldRecoilCounter) ||
		!ReadI32Field(theBlock, 96U, theZombie.mZombieAge) ||
		!ReadI32Field(theBlock, 100U, theZombie.mZombieHeight) ||
		!ReadI32Field(theBlock, 104U, theZombie.mPhaseCounter) ||
		!ReadI32Field(theBlock, 108U, theZombie.mFromWave) ||
		!ReadBoolField(theBlock, 112U, theZombie.mDroppedLoot) ||
		!ReadI32Field(theBlock, 116U, theZombie.mZombieFade) ||
		!ReadBoolField(theBlock, 120U, theZombie.mFlatTires) ||
		!ReadI32Field(theBlock, 124U, theZombie.mUseLadderCol) ||
		!ReadI32Field(theBlock, 128U, theZombie.mTargetCol) ||
		!ReadFloatField(theBlock, 132U, theZombie.mAltitude) ||
		!ReadBoolField(theBlock, 136U, theZombie.mHitUmbrella) ||
		!ReadRect(theBlock, 140U, theZombie.mZombieRect) ||
		!ReadRect(theBlock, 156U, theZombie.mZombieAttackRect) ||
		!ReadI32Field(theBlock, 172U, theZombie.mChilledCounter) ||
		!ReadI32Field(theBlock, 176U, theZombie.mButteredCounter) ||
		!ReadI32Field(theBlock, 180U, theZombie.mIceTrapCounter) ||
		!ReadBoolField(theBlock, 184U, theZombie.mMindControlled) ||
		!ReadBoolField(theBlock, 185U, theZombie.mBlowingAway) ||
		!ReadBoolField(theBlock, 186U, theZombie.mHasHead) ||
		!ReadBoolField(theBlock, 187U, theZombie.mHasArm) ||
		!ReadBoolField(theBlock, 188U, theZombie.mHasObject) ||
		!ReadBoolField(theBlock, 189U, theZombie.mInPool) ||
		!ReadBoolField(theBlock, 190U, theZombie.mOnHighGround) ||
		!ReadBoolField(theBlock, 191U, theZombie.mYuckyFace) ||
		!ReadI32Field(theBlock, 192U, theZombie.mYuckyFaceCounter) ||
		!ReadI32Field(theBlock, 196U, theZombie.mHelmType) ||
		!ReadI32Field(theBlock, 200U, theZombie.mBodyHealth) ||
		!ReadI32Field(theBlock, 204U, theZombie.mBodyMaxHealth) ||
		!ReadI32Field(theBlock, 208U, theZombie.mHelmHealth) ||
		!ReadI32Field(theBlock, 212U, theZombie.mHelmMaxHealth) ||
		!ReadI32Field(theBlock, 216U, theZombie.mShieldType) ||
		!ReadI32Field(theBlock, 220U, theZombie.mShieldHealth) ||
		!ReadI32Field(theBlock, 224U, theZombie.mShieldMaxHealth) ||
		!ReadI32Field(theBlock, 228U, theZombie.mFlyingHealth) ||
		!ReadI32Field(theBlock, 232U, theZombie.mFlyingMaxHealth) ||
		!ReadBoolField(theBlock, 236U, theZombie.mDead) ||
		!ReadU32Field(theBlock, 240U, theZombie.mRelatedZombieID) ||
		!ReadU32Array(theBlock, 244U, &theZombie.mFollowerZombieID[0], MAX_ZOMBIE_FOLLOWERS) ||
		!ReadBoolField(theBlock, 260U, theZombie.mPlayingSong) ||
		!ReadI32Field(theBlock, 264U, theZombie.mParticleOffsetX) ||
		!ReadI32Field(theBlock, 268U, theZombie.mParticleOffsetY) ||
		!ReadI32Field(theBlock, 272U, theZombie.mAttachmentID) ||
		!ReadI32Field(theBlock, 276U, theZombie.mSummonCounter) ||
		!ReadU32Field(theBlock, 280U, theZombie.mBodyReanimID) ||
		!ReadFloatField(theBlock, 284U, theZombie.mScaleZombie) ||
		!ReadFloatField(theBlock, 288U, theZombie.mVelZ) ||
		!ReadFloatField(theBlock, 292U, theZombie.mOriginalAnimRate) ||
		!ReadU32Field(theBlock, 296U, theZombie.mTargetPlantID) ||
		!ReadI32Field(theBlock, 300U, theZombie.mBossMode) ||
		!ReadI32Field(theBlock, 304U, theZombie.mTargetRow) ||
		!ReadI32Field(theBlock, 308U, theZombie.mBossBungeeCounter) ||
		!ReadI32Field(theBlock, 312U, theZombie.mBossStompCounter) ||
		!ReadI32Field(theBlock, 316U, theZombie.mBossHeadCounter) ||
		!ReadU32Field(theBlock, 320U, theZombie.mBossFireBallReanimID) ||
		!ReadU32Field(theBlock, 324U, theZombie.mSpecialHeadReanimID) ||
		!ReadI32Field(theBlock, 328U, theZombie.mFireballRow) ||
		!ReadBoolField(theBlock, 332U, theZombie.mIsFireBall) ||
		!ReadU32Field(theBlock, 336U, theZombie.mMoweredReanimID) ||
		!ReadI32Field(theBlock, 340U, theZombie.mLastPortalX))
		return false;
	return true;
}

bool ReadPlant(const ByteView& theBlock, Plant& thePlant)
{
	if (!ReadGameObject(theBlock, thePlant) ||
		!ReadI32Field(theBlock, 36U, thePlant.mSeedType) ||
		!ReadI32Field(theBlock, 40U, thePlant.mPlantCol) ||
		!ReadI32Field(theBlock, 44U, thePlant.mAnimCounter) ||
		!ReadI32Field(theBlock, 48U, thePlant.mFrame) ||
		!ReadI32Field(theBlock, 52U, thePlant.mFrameLength) ||
		!ReadI32Field(theBlock, 56U, thePlant.mNumFrames) ||
		!ReadI32Field(theBlock, 60U, thePlant.mState) ||
		!ReadI32Field(theBlock, 64U, thePlant.mPlantHealth) ||
		!ReadI32Field(theBlock, 68U, thePlant.mPlantMaxHealth) ||
		!ReadI32Field(theBlock, 72U, thePlant.mSubclass) ||
		!ReadI32Field(theBlock, 76U, thePlant.mDisappearCountdown) ||
		!ReadI32Field(theBlock, 80U, thePlant.mDoSpecialCountdown) ||
		!ReadI32Field(theBlock, 84U, thePlant.mStateCountdown) ||
		!ReadI32Field(theBlock, 88U, thePlant.mLaunchCounter) ||
		!ReadI32Field(theBlock, 92U, thePlant.mLaunchRate) ||
		!ReadRect(theBlock, 96U, thePlant.mPlantRect) ||
		!ReadRect(theBlock, 112U, thePlant.mPlantAttackRect) ||
		!ReadI32Field(theBlock, 128U, thePlant.mTargetX) ||
		!ReadI32Field(theBlock, 132U, thePlant.mTargetY) ||
		!ReadI32Field(theBlock, 136U, thePlant.mStartRow) ||
		!ReadU32Field(theBlock, 140U, thePlant.mParticleID) ||
		!ReadI32Field(theBlock, 144U, thePlant.mShootingCounter) ||
		!ReadU32Field(theBlock, 148U, thePlant.mBodyReanimID) ||
		!ReadU32Field(theBlock, 152U, thePlant.mHeadReanimID) ||
		!ReadU32Field(theBlock, 156U, thePlant.mHeadReanimID2) ||
		!ReadU32Field(theBlock, 160U, thePlant.mHeadReanimID3) ||
		!ReadU32Field(theBlock, 164U, thePlant.mBlinkReanimID) ||
		!ReadU32Field(theBlock, 168U, thePlant.mLightReanimID) ||
		!ReadU32Field(theBlock, 172U, thePlant.mSleepingReanimID) ||
		!ReadI32Field(theBlock, 176U, thePlant.mBlinkCountdown) ||
		!ReadI32Field(theBlock, 180U, thePlant.mRecentlyEatenCountdown) ||
		!ReadI32Field(theBlock, 184U, thePlant.mEatenFlashCountdown) ||
		!ReadI32Field(theBlock, 188U, thePlant.mBeghouledFlashCountdown) ||
		!ReadFloatField(theBlock, 192U, thePlant.mShakeOffsetX) ||
		!ReadFloatField(theBlock, 196U, thePlant.mShakeOffsetY))
		return false;
	for (int i = 0; i < MAX_MAGNET_ITEMS; i++)
		if (!ReadMagnetItem(theBlock, 200U + static_cast<size_t>(i) * 20U, thePlant.mMagnetItems[i]))
			return false;
	return ReadU32Field(theBlock, 300U, thePlant.mTargetZombieID) &&
		ReadI32Field(theBlock, 304U, thePlant.mWakeUpCounter) &&
		ReadI32Field(theBlock, 308U, thePlant.mOnBungeeState) &&
		ReadI32Field(theBlock, 312U, thePlant.mImitaterType) &&
		ReadI32Field(theBlock, 316U, thePlant.mPottedPlantIndex) &&
		ReadBoolField(theBlock, 320U, thePlant.mAnimPing) &&
		ReadBoolField(theBlock, 321U, thePlant.mDead) &&
		ReadBoolField(theBlock, 322U, thePlant.mSquished) &&
		ReadBoolField(theBlock, 323U, thePlant.mIsAsleep) &&
		ReadBoolField(theBlock, 324U, thePlant.mIsOnBoard) &&
		ReadBoolField(theBlock, 325U, thePlant.mHighlighted);
}

bool ReadProjectile(const ByteView& theBlock, Projectile& theProjectile)
{
	return ReadGameObject(theBlock, theProjectile) &&
		ReadI32Field(theBlock, 36U, theProjectile.mFrame) &&
		ReadI32Field(theBlock, 40U, theProjectile.mNumFrames) &&
		ReadI32Field(theBlock, 44U, theProjectile.mAnimCounter) &&
		ReadFloatField(theBlock, 48U, theProjectile.mPosX) &&
		ReadFloatField(theBlock, 52U, theProjectile.mPosY) &&
		ReadFloatField(theBlock, 56U, theProjectile.mPosZ) &&
		ReadFloatField(theBlock, 60U, theProjectile.mVelX) &&
		ReadFloatField(theBlock, 64U, theProjectile.mVelY) &&
		ReadFloatField(theBlock, 68U, theProjectile.mVelZ) &&
		ReadFloatField(theBlock, 72U, theProjectile.mAccZ) &&
		ReadFloatField(theBlock, 76U, theProjectile.mShadowY) &&
		ReadBoolField(theBlock, 80U, theProjectile.mDead) &&
		ReadI32Field(theBlock, 84U, theProjectile.mAnimTicksPerFrame) &&
		ReadI32Field(theBlock, 88U, theProjectile.mMotionType) &&
		ReadI32Field(theBlock, 92U, theProjectile.mProjectileType) &&
		ReadI32Field(theBlock, 96U, theProjectile.mProjectileAge) &&
		ReadI32Field(theBlock, 100U, theProjectile.mClickBackoffCounter) &&
		ReadFloatField(theBlock, 104U, theProjectile.mRotation) &&
		ReadFloatField(theBlock, 108U, theProjectile.mRotationSpeed) &&
		ReadBoolField(theBlock, 112U, theProjectile.mOnHighGround) &&
		ReadI32Field(theBlock, 116U, theProjectile.mDamageRangeFlags) &&
		ReadI32Field(theBlock, 120U, theProjectile.mHitTorchwoodGridX) &&
		ReadI32Field(theBlock, 124U, theProjectile.mAttachmentID) &&
		ReadFloatField(theBlock, 128U, theProjectile.mCobTargetX) &&
		ReadI32Field(theBlock, 132U, theProjectile.mCobTargetRow) &&
		ReadU32Field(theBlock, 136U, theProjectile.mTargetZombieID) &&
		ReadI32Field(theBlock, 140U, theProjectile.mLastPortalX);
}

bool ReadPottedPlant(const ByteView& theBlock, size_t theOffset, PottedPlant& thePlant)
{
	return ReadI32Field(theBlock, theOffset, thePlant.mSeedType) &&
		ReadI32Field(theBlock, theOffset + 4U, thePlant.mWhichZenGarden) &&
		ReadI32Field(theBlock, theOffset + 8U, thePlant.mX) &&
		ReadI32Field(theBlock, theOffset + 12U, thePlant.mY) &&
		ReadI32Field(theBlock, theOffset + 16U, thePlant.mFacing) &&
		theBlock.ReadI64(theOffset + 24U, thePlant.mLastWateredTime) &&
		ReadI32Field(theBlock, theOffset + 32U, thePlant.mDrawVariation) &&
		ReadI32Field(theBlock, theOffset + 36U, thePlant.mPlantAge) &&
		ReadI32Field(theBlock, theOffset + 40U, thePlant.mTimesFed) &&
		ReadI32Field(theBlock, theOffset + 44U, thePlant.mFeedingsPerGrow) &&
		ReadI32Field(theBlock, theOffset + 48U, thePlant.mPlantNeed) &&
		theBlock.ReadI64(theOffset + 56U, thePlant.mLastNeedFulfilledTime) &&
		theBlock.ReadI64(theOffset + 64U, thePlant.mLastFertilizedTime) &&
		theBlock.ReadI64(theOffset + 72U, thePlant.mLastChocolateTime) &&
		theBlock.ReadI64(theOffset + 80U, thePlant.mFutureAttribute[0]);
}

bool ReadCoin(const ByteView& theBlock, Coin& theCoin)
{
	return ReadGameObject(theBlock, theCoin) &&
		ReadFloatField(theBlock, 36U, theCoin.mPosX) &&
		ReadFloatField(theBlock, 40U, theCoin.mPosY) &&
		ReadFloatField(theBlock, 44U, theCoin.mVelX) &&
		ReadFloatField(theBlock, 48U, theCoin.mVelY) &&
		ReadFloatField(theBlock, 52U, theCoin.mScale) &&
		ReadBoolField(theBlock, 56U, theCoin.mDead) &&
		ReadI32Field(theBlock, 60U, theCoin.mFadeCount) &&
		ReadFloatField(theBlock, 64U, theCoin.mCollectX) &&
		ReadFloatField(theBlock, 68U, theCoin.mCollectY) &&
		ReadI32Field(theBlock, 72U, theCoin.mGroundY) &&
		ReadI32Field(theBlock, 76U, theCoin.mCoinAge) &&
		ReadBoolField(theBlock, 80U, theCoin.mIsBeingCollected) &&
		ReadI32Field(theBlock, 84U, theCoin.mDisappearCounter) &&
		ReadI32Field(theBlock, 88U, theCoin.mType) &&
		ReadI32Field(theBlock, 92U, theCoin.mCoinMotion) &&
		ReadI32Field(theBlock, 96U, theCoin.mAttachmentID) &&
		ReadFloatField(theBlock, 100U, theCoin.mCollectionDistance) &&
		ReadI32Field(theBlock, 104U, theCoin.mUsableSeedType) &&
		ReadPottedPlant(theBlock, 112U, theCoin.mPottedPlantSpec) &&
		ReadBoolField(theBlock, 200U, theCoin.mNeedsBouncyArrow) &&
		ReadBoolField(theBlock, 201U, theCoin.mHasBouncyArrow) &&
		ReadBoolField(theBlock, 202U, theCoin.mHitGround) &&
		ReadI32Field(theBlock, 204U, theCoin.mTimesDropped);
}

bool ReadLawnMower(const ByteView& theBlock, LawnMower& theMower)
{
	return ReadFloatField(theBlock, 8U, theMower.mPosX) &&
		ReadFloatField(theBlock, 12U, theMower.mPosY) &&
		ReadI32Field(theBlock, 16U, theMower.mRenderOrder) &&
		ReadI32Field(theBlock, 20U, theMower.mRow) &&
		ReadI32Field(theBlock, 24U, theMower.mAnimTicksPerFrame) &&
		ReadU32Field(theBlock, 28U, theMower.mReanimID) &&
		ReadI32Field(theBlock, 32U, theMower.mChompCounter) &&
		ReadI32Field(theBlock, 36U, theMower.mRollingInCounter) &&
		ReadI32Field(theBlock, 40U, theMower.mSquishedCounter) &&
		ReadI32Field(theBlock, 44U, theMower.mMowerState) &&
		ReadBoolField(theBlock, 48U, theMower.mDead) &&
		ReadBoolField(theBlock, 49U, theMower.mVisible) &&
		ReadI32Field(theBlock, 52U, theMower.mMowerType) &&
		ReadFloatField(theBlock, 56U, theMower.mAltitude) &&
		ReadI32Field(theBlock, 60U, theMower.mMowerHeight) &&
		ReadI32Field(theBlock, 64U, theMower.mLastPortalX);
}

bool ReadGridItem(const ByteView& theBlock, GridItem& theItem)
{
	if (!ReadI32Field(theBlock, 8U, theItem.mGridItemType) ||
		!ReadI32Field(theBlock, 12U, theItem.mGridItemState) ||
		!ReadI32Field(theBlock, 16U, theItem.mGridX) ||
		!ReadI32Field(theBlock, 20U, theItem.mGridY) ||
		!ReadI32Field(theBlock, 24U, theItem.mGridItemCounter) ||
		!ReadI32Field(theBlock, 28U, theItem.mRenderOrder) ||
		!ReadBoolField(theBlock, 32U, theItem.mDead) ||
		!ReadFloatField(theBlock, 36U, theItem.mPosX) ||
		!ReadFloatField(theBlock, 40U, theItem.mPosY) ||
		!ReadFloatField(theBlock, 44U, theItem.mGoalX) ||
		!ReadFloatField(theBlock, 48U, theItem.mGoalY) ||
		!ReadU32Field(theBlock, 52U, theItem.mGridItemReanimID) ||
		!ReadU32Field(theBlock, 56U, theItem.mGridItemParticleID) ||
		!ReadI32Field(theBlock, 60U, theItem.mZombieType) ||
		!ReadI32Field(theBlock, 64U, theItem.mSeedType) ||
		!ReadI32Field(theBlock, 68U, theItem.mScaryPotType) ||
		!ReadBoolField(theBlock, 72U, theItem.mHighlighted) ||
		!ReadI32Field(theBlock, 76U, theItem.mTransparentCounter) ||
		!ReadI32Field(theBlock, 80U, theItem.mSunCount))
		return false;
	for (int i = 0; i < NUM_MOTION_TRAIL_FRAMES; i++)
	{
		size_t anOffset = 84U + static_cast<size_t>(i) * 12U;
		if (!ReadFloatField(theBlock, anOffset, theItem.mMotionTrailFrames[i].mPosX) ||
			!ReadFloatField(theBlock, anOffset + 4U, theItem.mMotionTrailFrames[i].mPosY) ||
			!ReadFloatField(theBlock, anOffset + 8U, theItem.mMotionTrailFrames[i].mAnimTime))
			return false;
	}
	return ReadI32Field(theBlock, 228U, theItem.mMotionTrailCount);
}

bool ReadParticleSystem(const ByteView& theBlock, PvzpParticleSystem& theSystem)
{
	return ReadI32Field(theBlock, 0U, theSystem.mEffectType) &&
		ReadBoolField(theBlock, 28U, theSystem.mDead) &&
		ReadBoolField(theBlock, 29U, theSystem.mIsAttachment) &&
		ReadI32Field(theBlock, 32U, theSystem.mRenderOrder) &&
		ReadBoolField(theBlock, 36U, theSystem.mDontUpdate);
}

bool ReadParticleEmitter(const ByteView& theBlock, PvzpParticleEmitter& theEmitter)
{
	return ReadFloatField(theBlock, 24U, theEmitter.mSpawnAccum) &&
		ReadVector2(theBlock, 28U, theEmitter.mSystemCenter) &&
		ReadI32Field(theBlock, 36U, theEmitter.mParticlesSpawned) &&
		ReadI32Field(theBlock, 40U, theEmitter.mSystemAge) &&
		ReadI32Field(theBlock, 44U, theEmitter.mSystemDuration) &&
		ReadFloatField(theBlock, 48U, theEmitter.mSystemTimeValue) &&
		ReadFloatField(theBlock, 52U, theEmitter.mSystemLastTimeValue) &&
		ReadBoolField(theBlock, 56U, theEmitter.mDead) &&
		ReadColor(theBlock, 60U, theEmitter.mColorOverride) &&
		ReadBoolField(theBlock, 76U, theEmitter.mExtraAdditiveDrawOverride) &&
		ReadFloatField(theBlock, 80U, theEmitter.mScaleOverride) &&
		ReadU32Field(theBlock, 88U, theEmitter.mCrossFadeEmitterID) &&
		ReadI32Field(theBlock, 92U, theEmitter.mEmitterCrossFadeCountDown) &&
		ReadI32Field(theBlock, 96U, theEmitter.mFrameOverride) &&
		ReadFloatArray(theBlock, 100U, &theEmitter.mTrackInterp[0], ParticleSystemTracks::NUM_SYSTEM_TRACKS) &&
		ReadFloatArray(theBlock, 140U, &theEmitter.mSystemFieldInterp[0][0], MAX_PARTICLE_FIELDS * 2U);
}

bool ReadParticle(const ByteView& theBlock, PvzpParticle& theParticle)
{
	return ReadI32Field(theBlock, 4U, theParticle.mParticleDuration) &&
		ReadI32Field(theBlock, 8U, theParticle.mParticleAge) &&
		ReadFloatField(theBlock, 12U, theParticle.mParticleTimeValue) &&
		ReadFloatField(theBlock, 16U, theParticle.mParticleLastTimeValue) &&
		ReadFloatField(theBlock, 20U, theParticle.mAnimationTimeValue) &&
		ReadVector2(theBlock, 24U, theParticle.mVelocity) &&
		ReadVector2(theBlock, 32U, theParticle.mPosition) &&
		ReadI32Field(theBlock, 40U, theParticle.mImageFrame) &&
		ReadFloatField(theBlock, 44U, theParticle.mSpinPosition) &&
		ReadFloatField(theBlock, 48U, theParticle.mSpinVelocity) &&
		ReadU32Field(theBlock, 52U, theParticle.mCrossFadeParticleID) &&
		ReadI32Field(theBlock, 56U, theParticle.mCrossFadeDuration) &&
		ReadFloatArray(theBlock, 60U, &theParticle.mParticleInterp[0], ParticleTracks::NUM_PARTICLE_TRACKS) &&
		ReadFloatArray(theBlock, 124U, &theParticle.mParticleFieldInterp[0][0], MAX_PARTICLE_FIELDS * 2U);
}

bool ReadReanimation(const ByteView& theBlock, Reanimation& theReanimation)
{
	return ReadI32Field(theBlock, 0U, theReanimation.mReanimationType) &&
		ReadFloatField(theBlock, 4U, theReanimation.mAnimTime) &&
		ReadFloatField(theBlock, 8U, theReanimation.mAnimRate) &&
		ReadI32Field(theBlock, 16U, theReanimation.mLoopType) &&
		ReadBoolField(theBlock, 20U, theReanimation.mDead) &&
		ReadI32Field(theBlock, 24U, theReanimation.mFrameStart) &&
		ReadI32Field(theBlock, 28U, theReanimation.mFrameCount) &&
		ReadI32Field(theBlock, 32U, theReanimation.mFrameBasePose) &&
		ReadMatrix(theBlock, 36U, theReanimation.mOverlayMatrix) &&
		ReadColor(theBlock, 72U, theReanimation.mColorOverride) &&
		ReadI32Field(theBlock, 92U, theReanimation.mLoopCount) &&
		ReadBoolField(theBlock, 100U, theReanimation.mIsAttachment) &&
		ReadI32Field(theBlock, 104U, theReanimation.mRenderOrder) &&
		ReadColor(theBlock, 108U, theReanimation.mExtraAdditiveColor) &&
		ReadBoolField(theBlock, 124U, theReanimation.mEnableExtraAdditiveDraw) &&
		ReadColor(theBlock, 128U, theReanimation.mExtraOverlayColor) &&
		ReadBoolField(theBlock, 144U, theReanimation.mEnableExtraOverlayDraw) &&
		ReadFloatField(theBlock, 148U, theReanimation.mLastFrameTime) &&
		ReadI32Field(theBlock, 152U, theReanimation.mFilterEffect);
}

bool ReadTrail(const ByteView& theBlock, Trail& theTrail)
{
	for (int i = 0; i < 20; i++)
		if (!ReadVector2(theBlock, static_cast<size_t>(i) * 8U, theTrail.mTrailPoints[i].aPos))
			return false;
	return ReadI32Field(theBlock, 160U, theTrail.mNumTrailPoints) &&
		ReadBoolField(theBlock, 164U, theTrail.mDead) &&
		ReadI32Field(theBlock, 168U, theTrail.mRenderOrder) &&
		ReadI32Field(theBlock, 172U, theTrail.mTrailAge) &&
		ReadI32Field(theBlock, 176U, theTrail.mTrailDuration) &&
		ReadFloatArray(theBlock, 188U, &theTrail.mTrailInterp[0], 4U) &&
		ReadVector2(theBlock, 204U, theTrail.mTrailCenter) &&
		ReadBoolField(theBlock, 212U, theTrail.mIsAttachment) &&
		ReadColor(theBlock, 216U, theTrail.mColorOverride);
}

bool ReadAttachment(const ByteView& theBlock, Attachment& theAttachment)
{
	for (int i = 0; i < MAX_EFFECTS_PER_ATTACHMENT; i++)
	{
		size_t anOffset = static_cast<size_t>(i) * 48U;
		AttachEffect& anEffect = theAttachment.mEffectArray[i];
		if (!ReadU32Field(theBlock, anOffset, anEffect.mEffectID) ||
			!ReadI32Field(theBlock, anOffset + 4U, anEffect.mEffectType) ||
			!ReadMatrix(theBlock, anOffset + 8U, anEffect.mOffset) ||
			!ReadBoolField(theBlock, anOffset + 44U, anEffect.mDontDrawIfParentHidden) ||
			!ReadBoolField(theBlock, anOffset + 45U, anEffect.mDontPropogateColor))
			return false;
	}
	return ReadI32Field(theBlock, 768U, theAttachment.mNumEffects) &&
		theAttachment.mNumEffects >= 0 && theAttachment.mNumEffects <= MAX_EFFECTS_PER_ATTACHMENT &&
		ReadBoolField(theBlock, 772U, theAttachment.mDead);
}

bool ReadBoard(const ByteView& theBlock, Board& theBoard)
{
	static_assert(MAX_GRID_SIZE_X == 9);
	static_assert(MAX_GRID_SIZE_Y == 6);
	static_assert(MAX_ZOMBIE_WAVES == 100);
	static_assert(MAX_ZOMBIES_IN_WAVE == 50);
	static_assert(NUM_ADVICE_TYPES == 66);
	if (!ReadBoolField(theBlock, 0U, theBoard.mPaused) ||
		!ReadI32Array(theBlock, 4U, &theBoard.mGridSquareType[0][0], MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y) ||
		!ReadI32Array(theBlock, 220U, &theBoard.mGridCelLook[0][0], MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y) ||
		!ReadI32Array(theBlock, 436U, &theBoard.mGridCelOffset[0][0][0], MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y * 2U) ||
		!ReadI32Array(theBlock, 868U, &theBoard.mGridCelFog[0][0], MAX_GRID_SIZE_X * (MAX_GRID_SIZE_Y + 1U)) ||
		!ReadBoolField(theBlock, 1120U, theBoard.mEnableGraveStones) ||
		!ReadI32Field(theBlock, 1124U, theBoard.mSpecialGraveStoneX) ||
		!ReadI32Field(theBlock, 1128U, theBoard.mSpecialGraveStoneY) ||
		!ReadFloatField(theBlock, 1132U, theBoard.mFogOffset) ||
		!ReadI32Field(theBlock, 1136U, theBoard.mFogBlownCountDown) ||
		!ReadI32Array(theBlock, 1140U, &theBoard.mPlantRow[0], MAX_GRID_SIZE_Y) ||
		!ReadI32Array(theBlock, 1164U, &theBoard.mWaveRowGotLawnMowered[0], MAX_GRID_SIZE_Y) ||
		!ReadI32Field(theBlock, 1188U, theBoard.mBonusLawnMowersRemaining) ||
		!ReadI32Array(theBlock, 1192U, &theBoard.mIceMinX[0], MAX_GRID_SIZE_Y) ||
		!ReadI32Array(theBlock, 1216U, &theBoard.mIceTimer[0], MAX_GRID_SIZE_Y) ||
		!ReadU32Array(theBlock, 1240U, &theBoard.mIceParticleID[0], MAX_GRID_SIZE_Y))
		return false;
	for (int i = 0; i < MAX_GRID_SIZE_Y; i++)
	{
		size_t anOffset = 1264U + static_cast<size_t>(i) * 16U;
		if (!ReadI32Field(theBlock, anOffset, theBoard.mRowPickingArray[i].mItem) ||
			!ReadFloatField(theBlock, anOffset + 4U, theBoard.mRowPickingArray[i].mWeight) ||
			!ReadFloatField(theBlock, anOffset + 8U, theBoard.mRowPickingArray[i].mLastPicked) ||
			!ReadFloatField(theBlock, anOffset + 12U, theBoard.mRowPickingArray[i].mSecondLastPicked))
			return false;
	}
	if (!ReadI32Array(theBlock, 1360U, &theBoard.mZombiesInWave[0][0], MAX_ZOMBIE_WAVES * MAX_ZOMBIES_IN_WAVE) ||
		!ReadBoolArray(theBlock, 21360U, &theBoard.mZombieAllowed[0], 100U) ||
		!ReadI32Field(theBlock, 21460U, theBoard.mSunCountDown) ||
		!ReadI32Field(theBlock, 21464U, theBoard.mNumSunsFallen) ||
		!ReadI32Field(theBlock, 21468U, theBoard.mShakeCounter) ||
		!ReadI32Field(theBlock, 21472U, theBoard.mShakeAmountX) ||
		!ReadI32Field(theBlock, 21476U, theBoard.mShakeAmountY) ||
		!ReadI32Field(theBlock, 21480U, theBoard.mBackground) ||
		!ReadI32Field(theBlock, 21484U, theBoard.mLevel) ||
		!ReadI32Field(theBlock, 21488U, theBoard.mSodPosition) ||
		!ReadI32Field(theBlock, 21492U, theBoard.mPrevMouseX) ||
		!ReadI32Field(theBlock, 21496U, theBoard.mPrevMouseY) ||
		!ReadI32Field(theBlock, 21500U, theBoard.mSunMoney) ||
		!ReadI32Field(theBlock, 21504U, theBoard.mNumWaves) ||
		!ReadU32Field(theBlock, 21508U, theBoard.mMainCounter) ||
		!ReadU32Field(theBlock, 21512U, theBoard.mEffectCounter) ||
		!ReadU32Field(theBlock, 21516U, theBoard.mDrawCount) ||
		!ReadI32Field(theBlock, 21520U, theBoard.mRiseFromGraveCounter) ||
		!ReadI32Field(theBlock, 21524U, theBoard.mOutOfMoneyCounter) ||
		!ReadI32Field(theBlock, 21528U, theBoard.mCurrentWave) ||
		!ReadI32Field(theBlock, 21532U, theBoard.mTotalSpawnedWaves) ||
		!ReadI32Field(theBlock, 21536U, theBoard.mTutorialState) ||
		!ReadU32Field(theBlock, 21540U, theBoard.mTutorialParticleID) ||
		!ReadI32Field(theBlock, 21544U, theBoard.mTutorialTimer) ||
		!ReadI32Field(theBlock, 21548U, theBoard.mLastBungeeWave) ||
		!ReadI32Field(theBlock, 21552U, theBoard.mZombieHealthToNextWave) ||
		!ReadI32Field(theBlock, 21556U, theBoard.mZombieHealthWaveStart) ||
		!ReadI32Field(theBlock, 21560U, theBoard.mZombieCountDown) ||
		!ReadI32Field(theBlock, 21564U, theBoard.mZombieCountDownStart) ||
		!ReadI32Field(theBlock, 21568U, theBoard.mHugeWaveCountDown) ||
		!ReadBoolArray(theBlock, 21572U, &theBoard.mHelpDisplayed[0], NUM_ADVICE_TYPES) ||
		!ReadI32Field(theBlock, 21640U, theBoard.mHelpIndex) ||
		!ReadBoolField(theBlock, 21644U, theBoard.mFinalBossKilled) ||
		!ReadBoolField(theBlock, 21645U, theBoard.mShowShovel) ||
		!ReadI32Field(theBlock, 21648U, theBoard.mCoinBankFadeCount) ||
		!ReadI32Field(theBlock, 21652U, theBoard.mDebugTextMode) ||
		!ReadBoolField(theBlock, 21656U, theBoard.mLevelComplete) ||
		!ReadI32Field(theBlock, 21660U, theBoard.mBoardFadeOutCounter) ||
		!ReadI32Field(theBlock, 21664U, theBoard.mNextSurvivalStageCounter) ||
		!ReadI32Field(theBlock, 21668U, theBoard.mScoreNextMowerCounter) ||
		!ReadBoolField(theBlock, 21672U, theBoard.mLevelAwardSpawned) ||
		!ReadI32Field(theBlock, 21676U, theBoard.mProgressMeterWidth) ||
		!ReadI32Field(theBlock, 21680U, theBoard.mFlagRaiseCounter) ||
		!ReadI32Field(theBlock, 21684U, theBoard.mIceTrapCounter) ||
		!ReadI32Field(theBlock, 21688U, theBoard.mBoardRandSeed) ||
		!ReadU32Field(theBlock, 21692U, theBoard.mPoolSparklyParticleID) ||
		!ReadU32Array(theBlock, 21696U, &theBoard.mFwooshID[0][0], MAX_GRID_SIZE_Y * 12U) ||
		!ReadI32Field(theBlock, 21984U, theBoard.mFwooshCountDown) ||
		!ReadI32Field(theBlock, 21988U, theBoard.mTimeStopCounter) ||
		!ReadBoolField(theBlock, 21992U, theBoard.mDroppedFirstCoin) ||
		!ReadI32Field(theBlock, 21996U, theBoard.mFinalWaveSoundCounter) ||
		!ReadI32Field(theBlock, 22000U, theBoard.mCobCannonCursorDelayCounter) ||
		!ReadI32Field(theBlock, 22004U, theBoard.mCobCannonMouseX) ||
		!ReadI32Field(theBlock, 22008U, theBoard.mCobCannonMouseY) ||
		!ReadBoolField(theBlock, 22012U, theBoard.mKilledYeti) ||
		!ReadBoolField(theBlock, 22013U, theBoard.mMustacheMode) ||
		!ReadBoolField(theBlock, 22014U, theBoard.mSuperMowerMode) ||
		!ReadBoolField(theBlock, 22015U, theBoard.mFutureMode) ||
		!ReadBoolField(theBlock, 22016U, theBoard.mPinataMode) ||
		!ReadBoolField(theBlock, 22017U, theBoard.mDanceMode) ||
		!ReadBoolField(theBlock, 22018U, theBoard.mDaisyMode) ||
		!ReadBoolField(theBlock, 22019U, theBoard.mSukhbirMode) ||
		!ReadI32Field(theBlock, 22020U, theBoard.mPrevBoardResult) ||
		!ReadI32Field(theBlock, 22024U, theBoard.mTriggeredLawnMowers) ||
		!ReadU32Field(theBlock, 22028U, theBoard.mPlayTimeActiveLevel) ||
		!ReadU32Field(theBlock, 22032U, theBoard.mPlayTimeInactiveLevel) ||
		!ReadI32Field(theBlock, 22036U, theBoard.mMaxSunPlants))
		return false;

	uint32_t aStartDrawTime = 0;
	uint32_t anIntervalDrawTime = 0;
	int32_t aGameId = 0;
	if (!theBlock.ReadU32(22040U, aStartDrawTime) ||
		!theBlock.ReadU32(22044U, anIntervalDrawTime) ||
		!ReadU32Field(theBlock, 22048U, theBoard.mIntervalDrawCountStart) ||
		!ReadFloatField(theBlock, 22052U, theBoard.mMinFPS) ||
		!ReadI32Field(theBlock, 22056U, theBoard.mPreloadTime) ||
		!theBlock.ReadI32(22060U, aGameId) ||
		!ReadU32Field(theBlock, 22064U, theBoard.mGravesCleared) ||
		!ReadU32Field(theBlock, 22068U, theBoard.mPlantsEaten) ||
		!ReadU32Field(theBlock, 22072U, theBoard.mPlantsShoveled) ||
		!ReadU32Field(theBlock, 22076U, theBoard.mCoinsCollected) ||
		!ReadU32Field(theBlock, 22080U, theBoard.mDiamondsCollected) ||
		!ReadU32Field(theBlock, 22084U, theBoard.mPottedPlantsCollected) ||
		!ReadU32Field(theBlock, 22088U, theBoard.mChocolateCollected))
		return false;
	theBoard.mStartDrawTime = aStartDrawTime;
	theBoard.mIntervalDrawTime = anIntervalDrawTime;
	theBoard.mGameID = static_cast<intptr_t>(aGameId);
	return true;
}

bool ReadCursor(const ByteView& theBlock, CursorObject& theCursor)
{
	return ReadGameObject(theBlock, theCursor) &&
		ReadI32Field(theBlock, 36U, theCursor.mSeedBankIndex) &&
		ReadI32Field(theBlock, 40U, theCursor.mType) &&
		ReadI32Field(theBlock, 44U, theCursor.mImitaterType) &&
		ReadI32Field(theBlock, 48U, theCursor.mCursorType) &&
		ReadU32Field(theBlock, 52U, theCursor.mCoinID) &&
		ReadU32Field(theBlock, 56U, theCursor.mGlovePlantID) &&
		ReadU32Field(theBlock, 60U, theCursor.mDuplicatorPlantID) &&
		ReadU32Field(theBlock, 64U, theCursor.mCobCannonPlantID) &&
		ReadI32Field(theBlock, 68U, theCursor.mHammerDownCounter) &&
		ReadU32Field(theBlock, 72U, theCursor.mReanimCursorID);
}

bool ReadCursorPreview(const ByteView& theBlock, CursorPreview& thePreview)
{
	return ReadGameObject(theBlock, thePreview) &&
		ReadI32Field(theBlock, 36U, thePreview.mGridX) &&
		ReadI32Field(theBlock, 40U, thePreview.mGridY);
}

bool ReadMessageWidget(const ByteView& theBlock, MessageWidget& theWidget)
{
	return theBlock.Copy(4U, &theWidget.mLabel[0], sizeof(theWidget.mLabel)) &&
		ReadI32Field(theBlock, 132U, theWidget.mDisplayTime) &&
		ReadI32Field(theBlock, 136U, theWidget.mDuration) &&
		ReadI32Field(theBlock, 140U, theWidget.mMessageStyle) &&
		ReadU32Array(theBlock, 144U, &theWidget.mTextReanimID[0], MAX_MESSAGE_LENGTH) &&
		ReadI32Field(theBlock, 656U, theWidget.mReanimType) &&
		ReadI32Field(theBlock, 660U, theWidget.mSlideOffTime) &&
		theBlock.Copy(664U, &theWidget.mLabelNext[0], sizeof(theWidget.mLabelNext)) &&
		ReadI32Field(theBlock, 792U, theWidget.mMessageStyleNext);
}

bool ReadSeedPacket(const ByteView& theBlock, size_t theOffset, SeedPacket& thePacket)
{
	ByteView aPacket = theBlock.Slice(theOffset, 80U);
	return aPacket.mData != nullptr && ReadGameObject(aPacket, thePacket) &&
		ReadI32Field(aPacket, 36U, thePacket.mRefreshCounter) &&
		ReadI32Field(aPacket, 40U, thePacket.mRefreshTime) &&
		ReadI32Field(aPacket, 44U, thePacket.mIndex) &&
		ReadI32Field(aPacket, 48U, thePacket.mOffsetX) &&
		ReadI32Field(aPacket, 52U, thePacket.mPacketType) &&
		ReadI32Field(aPacket, 56U, thePacket.mImitaterType) &&
		ReadI32Field(aPacket, 60U, thePacket.mSlotMachineCountDown) &&
		ReadI32Field(aPacket, 64U, thePacket.mSlotMachiningNextSeed) &&
		ReadFloatField(aPacket, 68U, thePacket.mSlotMachiningPosition) &&
		ReadBoolField(aPacket, 72U, thePacket.mActive) &&
		ReadBoolField(aPacket, 73U, thePacket.mRefreshing) &&
		ReadI32Field(aPacket, 76U, thePacket.mTimesUsed);
}

bool ReadSeedBank(const ByteView& theBlock, SeedBank& theSeedBank)
{
	if (!ReadGameObject(theBlock, theSeedBank) || !ReadI32Field(theBlock, 36U, theSeedBank.mNumPackets))
		return false;
	for (int i = 0; i < SEEDBANK_MAX; i++)
		if (!ReadSeedPacket(theBlock, 40U + static_cast<size_t>(i) * 80U, theSeedBank.mSeedPackets[i]))
			return false;
	return ReadI32Field(theBlock, 840U, theSeedBank.mCutSceneDarken) &&
		ReadI32Field(theBlock, 844U, theSeedBank.mConveyorBeltCounter);
}

bool ReadChallenge(const ByteView& theBlock, Challenge& theChallenge)
{
	if (!ReadI32Field(theBlock, 8U, theChallenge.mBeghouledMouseCapture) ||
		!ReadI32Field(theBlock, 12U, theChallenge.mBeghouledMouseDownX) ||
		!ReadI32Field(theBlock, 16U, theChallenge.mBeghouledMouseDownY))
		return false;
	for (size_t i = 0; i < 9U * 6U; i++)
	{
		uint8_t aValue = 0;
		if (!theBlock.ReadU8(20U + i, aValue) || aValue > 1U)
			return false;
		(&theChallenge.mBeghouledEated[0][0])[i] = aValue;
	}
	for (size_t i = 0; i < NUM_BEGHOULED_UPGRADES; i++)
	{
		uint8_t aValue = 0;
		if (!theBlock.ReadU8(74U + i, aValue) || aValue > 1U)
			return false;
		theChallenge.mBeghouledPurcasedUpgrade[i] = aValue;
	}
	return ReadI32Field(theBlock, 80U, theChallenge.mBeghouledMatchesThisMove) &&
		ReadI32Field(theBlock, 84U, theChallenge.mChallengeState) &&
		ReadI32Field(theBlock, 88U, theChallenge.mChallengeStateCounter) &&
		ReadI32Field(theBlock, 92U, theChallenge.mConveyorBeltCounter) &&
		ReadI32Field(theBlock, 96U, theChallenge.mChallengeScore) &&
		ReadI32Field(theBlock, 100U, theChallenge.mShowBowlingLine) &&
		ReadI32Field(theBlock, 104U, theChallenge.mLastConveyorSeedType) &&
		ReadI32Field(theBlock, 108U, theChallenge.mSurvivalStage) &&
		ReadI32Field(theBlock, 112U, theChallenge.mSlotMachineRollCount) &&
		ReadU32Field(theBlock, 116U, theChallenge.mReanimChallenge) &&
		ReadU32Array(theBlock, 120U, &theChallenge.mReanimClouds[0], 6U) &&
		ReadI32Array(theBlock, 144U, &theChallenge.mCloudsCounter[0], 6U) &&
		ReadI32Field(theBlock, 168U, theChallenge.mChallengeGridX) &&
		ReadI32Field(theBlock, 172U, theChallenge.mChallengeGridY) &&
		ReadI32Field(theBlock, 176U, theChallenge.mScaryPotterPots) &&
		ReadI32Field(theBlock, 180U, theChallenge.mRainCounter) &&
		ReadI32Field(theBlock, 184U, theChallenge.mTreeOfWisdomTalkIndex);
}

bool ReadMusic(const ByteView& theBlock, Music& theMusic)
{
	return ReadI32Field(theBlock, 8U, theMusic.mCurMusicTune) &&
		ReadI32Field(theBlock, 12U, theMusic.mCurMusicFileMain) &&
		ReadI32Field(theBlock, 16U, theMusic.mCurMusicFileDrums) &&
		ReadI32Field(theBlock, 20U, theMusic.mCurMusicFileHihats) &&
		ReadI32Field(theBlock, 24U, theMusic.mBurstOverride) &&
		ReadFloatField(theBlock, 28U, theMusic.mBaseBPM) &&
		ReadFloatField(theBlock, 32U, theMusic.mBaseModSpeed) &&
		ReadI32Field(theBlock, 36U, theMusic.mMusicBurstState) &&
		ReadI32Field(theBlock, 40U, theMusic.mBurstStateCounter) &&
		ReadI32Field(theBlock, 44U, theMusic.mMusicDrumsState) &&
		ReadI32Field(theBlock, 48U, theMusic.mQueuedDrumTrackPackedOrder) &&
		ReadI32Field(theBlock, 52U, theMusic.mDrumsStateCounter) &&
		ReadI32Field(theBlock, 56U, theMusic.mPauseOffset) &&
		ReadI32Field(theBlock, 60U, theMusic.mPauseOffsetDrums) &&
		ReadBoolField(theBlock, 64U, theMusic.mPaused) &&
		ReadI32Field(theBlock, 68U, theMusic.mFadeOutCounter) &&
		ReadI32Field(theBlock, 72U, theMusic.mFadeOutDuration);
}

template <typename T, typename TReader>
bool ApplyArray(DataArray<T>& theTarget, const LegacyArrayData& theSource, TReader theReadItem)
{
	if (theTarget.mMaxUsedCount != 0U || theTarget.mSize != 0U || theSource.mMaxUsedCount > theTarget.mMaxSize)
		return false;
	for (uint32_t i = 0; i < theSource.mMaxUsedCount; i++)
	{
		theTarget.DataArrayResetItemAt(i);
		theTarget.DataArrayGetIDAt(i) = theSource.mIds[i];
	}
	theTarget.mFreeListHead = theSource.mFreeListHead;
	theTarget.mMaxUsedCount = theSource.mMaxUsedCount;
	theTarget.mSize = theSource.mSize;
	theTarget.mNextKey = LEGACY_NEXT_KEY;
	for (uint32_t i = 0; i < theSource.mMaxUsedCount; i++)
	{
		if (theSource.IsActive(i) && !theReadItem(theSource.Slot(i), theTarget.DataArrayGetItemAt(i)))
			return false;
	}
	return true;
}

Image* ResolveImage(int32_t theImageId)
{
	if (theImageId == static_cast<int32_t>(ResourceId::RESOURCE_ID_MAX))
		return nullptr;
	return GetImageById(static_cast<ResourceId>(theImageId));
}

bool ReadTrackInstance(const ByteView& theBlock, ReanimatorTrackInstance& theTrack)
{
	if (!ReadI32Field(theBlock, 0U, theTrack.mBlendCounter) ||
		!ReadI32Field(theBlock, 4U, theTrack.mBlendTime) ||
		!ReadFloatField(theBlock, 8U, theTrack.mBlendTransform.mTransX) ||
		!ReadFloatField(theBlock, 12U, theTrack.mBlendTransform.mTransY) ||
		!ReadFloatField(theBlock, 16U, theTrack.mBlendTransform.mSkewX) ||
		!ReadFloatField(theBlock, 20U, theTrack.mBlendTransform.mSkewY) ||
		!ReadFloatField(theBlock, 24U, theTrack.mBlendTransform.mScaleX) ||
		!ReadFloatField(theBlock, 28U, theTrack.mBlendTransform.mScaleY) ||
		!ReadFloatField(theBlock, 32U, theTrack.mBlendTransform.mFrame) ||
		!ReadFloatField(theBlock, 36U, theTrack.mBlendTransform.mAlpha) ||
		!ReadFloatField(theBlock, 52U, theTrack.mShakeOverride) ||
		!ReadFloatField(theBlock, 56U, theTrack.mShakeX) ||
		!ReadFloatField(theBlock, 60U, theTrack.mShakeY) ||
		!ReadI32Field(theBlock, 64U, theTrack.mAttachmentID) ||
		!ReadI32Field(theBlock, 72U, theTrack.mRenderGroup) ||
		!ReadColor(theBlock, 76U, theTrack.mTrackColor) ||
		!ReadBoolField(theBlock, 92U, theTrack.mIgnoreClipRect) ||
		!ReadBoolField(theBlock, 93U, theTrack.mTruncateDisappearingFrames) ||
		!ReadBoolField(theBlock, 94U, theTrack.mIgnoreColorOverride) ||
		!ReadBoolField(theBlock, 95U, theTrack.mIgnoreExtraAdditiveColor))
		return false;
	theTrack.mBlendTransform.mImage = nullptr;
	theTrack.mBlendTransform.mFont = nullptr;
	theTrack.mBlendTransform.mText = "";
	return true;
}

bool ApplyEffectExtras(Board* theBoard, const LegacySaveData& theSave)
{
	EffectSystem* anEffectSystem = theBoard->mApp->mEffectSystem;
	PvzpParticleHolder* aParticleHolder = anEffectSystem->mParticleHolder.get();
	size_t aSystemExtraIndex = 0;
	for (uint32_t i = 0; i < theSave.mParticleSystems.mMaxUsedCount; i++)
	{
		if (!theSave.mParticleSystems.IsActive(i))
			continue;
		if (aSystemExtraIndex >= theSave.mParticleSystemExtras.size())
			return false;
		PvzpParticleSystem& aSystem = aParticleHolder->mParticleSystems.DataArrayGetItemAt(i);
		const LegacyParticleSystemExtra& aSystemExtra = theSave.mParticleSystemExtras[aSystemExtraIndex++];
		aSystem.mParticleHolder = aParticleHolder;
		aSystem.mParticleDef = aSystemExtra.mDefinitionIndex == static_cast<int32_t>(ParticleEffect::PARTICLE_NONE)
			? nullptr : &gParticleDefArray[aSystemExtra.mDefinitionIndex];
		aSystem.mEmitterList.SetAllocator(&aParticleHolder->mEmitterListNodeAllocator);
		if (aSystemExtra.mEmitterIds.size() != aSystemExtra.mEmitters.size())
			return false;
		for (size_t j = 0; j < aSystemExtra.mEmitterIds.size(); j++)
		{
			uint32_t anEmitterId = aSystemExtra.mEmitterIds[j];
			PvzpParticleEmitter* anEmitter = aParticleHolder->mEmitters.DataArrayTryToGet(anEmitterId);
			if (!anEmitter || !aSystem.mParticleDef)
				return false;
			const LegacyEmitterExtra& anEmitterExtra = aSystemExtra.mEmitters[j];
			aSystem.mEmitterList.AddTail(static_cast<ParticleEmitterID>(anEmitterId));
			anEmitter->mParticleSystem = &aSystem;
			anEmitter->mEmitterDef = &aSystem.mParticleDef->mEmitterDefs[anEmitterExtra.mDefinitionIndex];
			anEmitter->mImageOverride = ResolveImage(anEmitterExtra.mImageId);
			anEmitter->mParticleList.SetAllocator(&aParticleHolder->mParticleListNodeAllocator);
			for (uint32_t aParticleId : anEmitterExtra.mParticleIds)
			{
				PvzpParticle* aParticle = aParticleHolder->mParticles.DataArrayTryToGet(aParticleId);
				if (!aParticle)
					return false;
				anEmitter->mParticleList.AddTail(static_cast<ParticleID>(aParticleId));
				aParticle->mParticleEmitter = anEmitter;
			}
		}
	}
	if (aSystemExtraIndex != theSave.mParticleSystemExtras.size())
		return false;

	ReanimationHolder* aReanimationHolder = anEffectSystem->mReanimationHolder.get();
	size_t aReanimationExtraIndex = 0;
	for (uint32_t i = 0; i < theSave.mReanimations.mMaxUsedCount; i++)
	{
		if (!theSave.mReanimations.IsActive(i))
			continue;
		if (aReanimationExtraIndex >= theSave.mReanimationExtras.size())
			return false;
		Reanimation& aReanimation = aReanimationHolder->mReanimations.DataArrayGetItemAt(i);
		const LegacyReanimationExtra& anExtra = theSave.mReanimationExtras[aReanimationExtraIndex++];
		aReanimation.mDefinition = &gReanimatorDefArray[anExtra.mDefinitionIndex];
		aReanimation.mReanimationHolder = aReanimationHolder;
		int aTrackCount = aReanimation.mDefinition->mTracks.count;
		if (static_cast<size_t>(aTrackCount) != anExtra.mImageIds.size())
			return false;
		if (aTrackCount > 0)
		{
			int aSize = aTrackCount * static_cast<int>(sizeof(ReanimatorTrackInstance));
			PvzpAllocator* anAllocator = FindGlobalAllocator(aSize);
			if (!anAllocator)
				return false;
			aReanimation.mTrackInstances = static_cast<ReanimatorTrackInstance*>(anAllocator->Calloc(aSize));
			if (!aReanimation.mTrackInstances)
				return false;
			for (int aTrackIndex = 0; aTrackIndex < aTrackCount; aTrackIndex++)
			{
				ByteView aTrackBlock = anExtra.mTrackBlock.Slice(static_cast<size_t>(aTrackIndex) * 96U, 96U);
				if (!ReadTrackInstance(aTrackBlock, aReanimation.mTrackInstances[aTrackIndex]))
					return false;
				aReanimation.mTrackInstances[aTrackIndex].mImageOverride = ResolveImage(anExtra.mImageIds[aTrackIndex]);
			}
		}
	}
	if (aReanimationExtraIndex != theSave.mReanimationExtras.size())
		return false;

	TrailHolder* aTrailHolder = anEffectSystem->mTrailHolder.get();
	size_t aTrailExtraIndex = 0;
	for (uint32_t i = 0; i < theSave.mTrails.mMaxUsedCount; i++)
	{
		if (!theSave.mTrails.IsActive(i))
			continue;
		if (aTrailExtraIndex >= theSave.mTrailDefinitionIndexes.size())
			return false;
		Trail& aTrail = aTrailHolder->mTrails.DataArrayGetItemAt(i);
		aTrail.mDefinition = &gTrailDefArray[theSave.mTrailDefinitionIndexes[aTrailExtraIndex++]];
		aTrail.mTrailHolder = aTrailHolder;
	}
	return aTrailExtraIndex == theSave.mTrailDefinitionIndexes.size();
}

bool ApplyLegacySave(Board* theBoard, const LegacySaveData& theSave)
{
	if (!theBoard || !theBoard->mApp || !theBoard->mApp->mEffectSystem ||
		!theBoard->mCursorObject || !theBoard->mCursorPreview || !theBoard->mAdvice ||
		!theBoard->mSeedBank || !theBoard->mChallenge || !theBoard->mApp->mMusic)
		return false;
	EffectSystem* anEffectSystem = theBoard->mApp->mEffectSystem;
	PvzpParticleHolder* aParticleHolder = anEffectSystem->mParticleHolder.get();
	if (!aParticleHolder || !anEffectSystem->mReanimationHolder ||
		!anEffectSystem->mTrailHolder || !anEffectSystem->mAttachmentHolder)
		return false;

	if (!ReadBoard(theSave.mBoard, *theBoard) ||
		!ApplyArray(theBoard->mZombies, theSave.mZombies, ReadZombie) ||
		!ApplyArray(theBoard->mPlants, theSave.mPlants, ReadPlant) ||
		!ApplyArray(theBoard->mProjectiles, theSave.mProjectiles, ReadProjectile) ||
		!ApplyArray(theBoard->mCoins, theSave.mCoins, ReadCoin) ||
		!ApplyArray(theBoard->mLawnMowers, theSave.mMowers, ReadLawnMower) ||
		!ApplyArray(theBoard->mGridItems, theSave.mGridItems, ReadGridItem) ||
		!ApplyArray(aParticleHolder->mParticleSystems, theSave.mParticleSystems, ReadParticleSystem) ||
		!ApplyArray(aParticleHolder->mEmitters, theSave.mEmitters, ReadParticleEmitter) ||
		!ApplyArray(aParticleHolder->mParticles, theSave.mParticles, ReadParticle) ||
		!ApplyArray(anEffectSystem->mReanimationHolder->mReanimations, theSave.mReanimations, ReadReanimation) ||
		!ApplyArray(anEffectSystem->mTrailHolder->mTrails, theSave.mTrails, ReadTrail) ||
		!ApplyArray(anEffectSystem->mAttachmentHolder->mAttachments, theSave.mAttachments, ReadAttachment) ||
		!ApplyEffectExtras(theBoard, theSave) ||
		!ReadCursor(theSave.mCursor, *theBoard->mCursorObject) ||
		!ReadCursorPreview(theSave.mCursorPreview, *theBoard->mCursorPreview) ||
		!ReadMessageWidget(theSave.mAdvice, *theBoard->mAdvice) ||
		!ReadSeedBank(theSave.mSeedBank, *theBoard->mSeedBank) ||
		!ReadChallenge(theSave.mChallenge, *theBoard->mChallenge) ||
		!ReadMusic(theSave.mMusic, *theBoard->mApp->mMusic))
		return false;
	return true;
}
}

bool LawnLoadLegacy1051Game(Board* theBoard, const std::string& theFilePath)
{
	Buffer aBuffer;
	if (!gSexyAppBase->ReadBufferFromFile(theFilePath, &aBuffer, false) || aBuffer.GetDataLen() <= 0)
		return false;
	LegacyReader aReader;
	aReader.mBytes = { static_cast<const unsigned char*>(aBuffer.GetDataPtr()), static_cast<size_t>(aBuffer.GetDataLen()) };
	LegacySaveData aSave;
	if (!ParseLegacySave(aReader, aSave))
	{
		PvzpTraceAndLogLn("Failed to parse 1.0.0.1051 save at byte %llu of %llu",
			static_cast<unsigned long long>(aReader.mPosition),
			static_cast<unsigned long long>(aReader.mBytes.mSize));
		return false;
	}
	if (!ApplyLegacySave(theBoard, aSave))
	{
		PvzpTraceAndLogLn("Failed to apply 1.0.0.1051 save");
		return false;
	}
	return true;
}
