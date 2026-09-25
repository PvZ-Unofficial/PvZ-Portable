#include "Replay.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "NativeControls.h"
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/System/SaveGame.h"
#include "../SexyAppFramework/widget/WidgetManager.h"

namespace PvzpReplay {
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
