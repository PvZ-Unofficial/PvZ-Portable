#include "Replay.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "NativeControls.h"
#include "PvzpParticle.h"
#include "Reanimator.h"
#include <limits>
#include <algorithm>
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/System/SaveGame.h"
#include "../SexyAppFramework/widget/WidgetManager.h"

namespace PvzpReplay {
int ParticleEmitterCount(int kind) noexcept {
    if (!gLawnApp || !gParticleDefArray || kind<0 || kind>=static_cast<int>(ParticleEffect::NUM_PARTICLES) || kind>=gParticleDefCount) return -1;
    return gParticleDefArray[kind].mEmitterDefCount;
}
int AnimationTrackCount(int kind) noexcept {
    try {
        if (!gLawnApp || !gReanimatorDefArray || kind<0 || kind>=static_cast<int>(ReanimationType::NUM_REANIMS) || static_cast<unsigned>(kind)>=gReanimatorDefCount) return -1;
        ReanimatorEnsureDefinitionLoaded(static_cast<ReanimationType>(kind),true);
        return gReanimatorDefArray[kind].mTracks.count;
    } catch (...) {return -1;}
}
int AnimationFrameCount(int kind) noexcept {
    const int count=AnimationTrackCount(kind);
    if (count<=0) return count;
    if (!gReanimatorDefArray[kind].mTracks.tracks) return -1;
    int frames=std::numeric_limits<int>::max();
    for (int i=0;i<count;++i) {
        const auto& track=gReanimatorDefArray[kind].mTracks.tracks[i];
        if (track.mTransforms.count<=0 || !track.mTransforms.mTransforms) return -1;
        frames=std::min(frames,track.mTransforms.count);
    }
    return frames;
}
bool SaveCheckpoint(std::uintptr_t file) noexcept
{
#if defined(_WIN32)
    try {
        if (!file || !gLawnApp || !gLawnApp->mBoard || gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING || gLawnApp->mBoard->mPaused || (gLawnApp->mWidgetManager && gLawnApp->mWidgetManager->mBaseModalWidget)) return false;
        std::vector<unsigned char> bytes;
        if (!LawnSerializeGame(gLawnApp->mBoard,bytes) || bytes.size()>MAXDWORD) return false;
        DWORD written=0;
        return WriteFile(reinterpret_cast<HANDLE>(file),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size();
    } catch (...) {return false;}
#else
    return false;
#endif
}
bool RestoreCheckpoint(const char* path, std::int32_t mode) noexcept
{
    try {
        if (!path || mode<0 || mode>=GameMode::NUM_GAME_MODES || !gLawnApp || !gLawnApp->mBoard ||
            gLawnApp->mGameScene != GameScenes::SCENE_PLAYING || gLawnApp->mBoard->mPaused || (gLawnApp->mWidgetManager && gLawnApp->mWidgetManager->mBaseModalWidget))
            return false;
        // A replay replacement must not turn a saved result into a newly
        // completed survival round. Existing pending natural deltas are retained.
        const auto result = gLawnApp->mBoardResult;
        gLawnApp->mGameMode=static_cast<GameMode>(mode);
        gLawnApp->mBoardResult = BoardResult::BOARDRESULT_NONE;
        gLawnApp->MakeNewBoard();
        gLawnApp->mBoardResult = result;
        gLawnApp->ProcessReplaySafeDeletes();
        return gLawnApp->mBoard && gLawnApp->mBoard->LoadGame(std::string(path));
    } catch (...) { return false; }
}
}
