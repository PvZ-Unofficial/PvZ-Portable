#include "PvzpLib/Plugin.h"
#include "PvzpLib/PluginLayout.h"
#ifdef PVZP_TEST_PAUSED_CURSOR
#include "PvzpLib/NativeControls.h"
#include "Lawn/CursorObject.h"
#include "Lawn/SeedPacket.h"
#include "widget/WidgetManager.h"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void Record(const char* message, DWORD size)
{
    wchar_t path[32768];
    if (!GetEnvironmentVariableW(L"PVZP_TEST_REPORT", path, 32768))
        return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(file, message, size, &written, nullptr);
    CloseHandle(file);
}

#define RECORD(text) Record(text "\n", sizeof(text))

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        RECORD("attach");
    if (reason == DLL_PROCESS_DETACH && !reserved)
        RECORD("unload");
    return TRUE;
}

#define PLUGIN_EXPORT __declspec(dllexport)
#else
#include <cstdio>
#include <cstdlib>
#define PLUGIN_EXPORT __attribute__((visibility("default")))
static void Record(const char* message)
{
    const char* path = std::getenv("PVZP_TEST_REPORT");
    if (!path) return;
    if (FILE* file = std::fopen(path, "a"))
    {
        std::fprintf(file, "%s\n", message);
        std::fclose(file);
    }
}
#define RECORD(text) Record(text)
#endif

static int Update(std::uint8_t, std::uint64_t)
{
#ifdef PVZP_TEST_PAUSED_CURSOR
    static int step = 0, clock = 0, cooldown = 0;
    if (!gLawnApp->mLoadingThreadCompleted) return 0;
    if (step == 0) {
        gLawnApp->PreNewGame(static_cast<GameMode>(13), false);
        ++step;
        return 0;
    }
    auto* board = gLawnApp->mBoard;
    auto* mouse = gLawnApp->mWidgetManager.get();
    if (step == 1) {
        gLawnApp->mGameScene = GameScenes::SCENE_PLAYING;
        PvzpNative::gAdvancedPause = true;
        mouse->mMouseIn = true;
        mouse->mLastMouseX = 200;
        mouse->mLastMouseY = 300;
        board->mCursorObject->mCursorType = CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
        board->mCursorObject->mType = SeedType::SEED_ICESHROOM;
        clock = board->mMainCounter;
        cooldown = board->mSeedBank->mSeedPackets[0].mRefreshCounter;
        ++step;
        return 0;
    }
    const int x = step == 2 ? 200 : 600;
    const int y = step == 2 ? 300 : 450;
    if (board->mCursorObject->mX != x - 25 || board->mCursorObject->mY != y - 35
        || board->mMainCounter != clock || board->mSeedBank->mSeedPackets[0].mRefreshCounter != cooldown) {
        RECORD("cursor-failed");
        PvzpNative::gAdvancedPause = false;
        PvzpPlugin::RequestStop();
        return 0;
    }
    if (step++ == 2) {
        mouse->mLastMouseX = 600;
        mouse->mLastMouseY = 450;
        return 0;
    }
    PvzpNative::gAdvancedPause = false;
    RECORD("cursor-passed");
#else
    RECORD("update");
#endif
#ifdef PVZP_TEST_INVALID_UPDATE
    return -1;
#endif
    PvzpPlugin::RequestStop();
    PvzpPlugin::RequestStop();
    return 0;
}

extern "C" PLUGIN_EXPORT std::uint32_t pvzp_plugin_abi_version()
{
#ifdef PVZP_TEST_BAD_VERSION
    return 0;
#endif
    return PvzpPlugin::AbiVersion;
}

extern "C" PLUGIN_EXPORT std::int32_t pvzp_plugin_initialize()
{
    RECORD("initialize");
    if (PvzpPlugin::SetUpdateCallback(Update))
        return 3; // Must reject registration before validation.
#ifdef PVZP_TEST_BAD_LAYOUT
    PvzpPlugin::LayoutEntry wrong[sizeof(PvzpPlugin::Layout) / sizeof(PvzpPlugin::Layout[0])];
    for (unsigned i = 0; i < sizeof(wrong) / sizeof(wrong[0]); ++i)
        wrong[i] = PvzpPlugin::Layout[i];
    const auto changed = PvzpPlugin::LayoutName(PVZP_TEST_BAD_LAYOUT == 1 ? "LawnApp.mBoard"
        : PVZP_TEST_BAD_LAYOUT == 2 ? "Board.mMainCounter" : "DataArray<Plant>.mItems");
    for (auto& entry : wrong)
        if (entry.name == changed) entry.value++;
    if (PvzpPlugin::ValidateLayout(wrong, sizeof(wrong) / sizeof(wrong[0])))
        RECORD("unexpected-layout-accepted");
    return 4;
#endif
    if (!PvzpPlugin::ValidateLayout(PvzpPlugin::Layout, sizeof(PvzpPlugin::Layout) / sizeof(PvzpPlugin::Layout[0])))
        return 1;
    RECORD("validated");
    if (!gLawnApp) return 6;
    volatile int counter = gLawnApp->mAppCounter;
    (void)counter;
    if (!PvzpPlugin::SetUpdateCallback(Update))
        return 2;
#ifdef PVZP_TEST_PARTIAL_INIT
    return 5;
#endif
    return 0;
}

extern "C" PLUGIN_EXPORT std::int32_t pvzp_plugin_shutdown()
{
    RECORD("shutdown");
#ifdef PVZP_TEST_SHUTDOWN_FAILURE
    return 1;
#endif
    return 0;
}
