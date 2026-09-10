#include "Plugin.h"
#include "PluginLayout.h"
#include "PvzpDebug.h"
#include "NativeControls.h"

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#endif

namespace PvzpPlugin
{
namespace
{
    Host& State() { return gLawnApp->mPlugin; }

    void Revoke()
    {
        State().updateCallback = nullptr;
        State().boardDestroyingCallback = nullptr;
        ClearBattleCallbacks();
    }

}

CallbackScope::CallbackScope() { ++State().callbackDepth; }
CallbackScope::~CallbackScope() { --State().callbackDepth; }

bool BattleDispatchEnabled() { return State().enabled && !State().callbackDepth && !State().stopRequested && State().battleInterest; }

bool SetBattleCallbacks(const BattleCallbacks* callbacks, std::uint32_t interest)
{
    if (!State().validated || State().shutdownAttempted || State().stopRequested || !callbacks || !interest || State().battleInterest)
        return false;
    if (!callbacks->beginLogicFrame || !callbacks->endLogicFrame || !callbacks->beginPlantEffect
        || !callbacks->finishPlantEffect || !callbacks->emitHomeEntry || !callbacks->emitGargantuarSpawned
        || !callbacks->emitImpThrown || !callbacks->emitGargantuarAshHit)
        return false;
    State().battleCallbacks = *callbacks;
    State().battleInterest = interest;
    return true;
}

void ClearBattleCallbacks()
{
    State().battleInterest = 0;
    State().battleCallbacks = {};
}

bool ValidateLayout(const LayoutEntry* entries, std::uint32_t count)
{
    if (!State().initializing || State().validated || !entries || count != sizeof(Layout) / sizeof(Layout[0]))
        return false;
    for (std::uint32_t i = 0; i < count; ++i)
        if (entries[i].name != Layout[i].name || entries[i].value != Layout[i].value)
            return false;
    State().validated = true;
    return true;
}

bool SetUpdateCallback(UpdateCallback callback)
{
    if (!State().validated || State().shutdownAttempted || State().stopRequested)
        return false;
    State().updateCallback = callback;
    return true;
}

bool SetBoardDestroyingCallback(BoardDestroyingCallback callback)
{
    if (!State().validated || State().shutdownAttempted || State().stopRequested)
        return false;
    State().boardDestroyingCallback = callback;
    return true;
}

bool InCallback() { return State().callbackDepth != 0; }
void RequestStop() { State().stopRequested = true; }

void SafePoint()
{
    if (!State().stopRequested || State().callbackDepth || State().initializing || State().shutdownAttempted)
        return;
    State().enabled = false;
    State().shutdownAttempted = true;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    std::int32_t result = 0;
    if (State().shutdownPlugin)
    {
        CallbackScope scope;
        try { result = State().shutdownPlugin(); }
        catch (...) { result = -1; }
    }
    Revoke();
    State().validated = false;
    PvzpNative::RestoreAll();
    if (result != 0)
    {
        PvzpTraceAndLogLn("Plugin cleanup incomplete (%d); DLL retained and further loading disabled", result);
        return;
    }
    if (State().module)
        FreeLibrary(static_cast<HMODULE>(State().module));
    State().module = nullptr;
    State().shutdownPlugin = nullptr;
#else
    Revoke();
    State().validated = false;
#endif
}

void Load()
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (State().module || State().shutdownAttempted)
        return;
    const wchar_t* path = _wgetenv(L"PVZP_PLUGIN");
    if (!path || !*path)
        return;
    State().module = LoadLibraryW(path);
    if (!State().module)
    {
        PvzpTraceAndLogLn("Plugin load failed with Windows error %lu", GetLastError());
        return;
    }
    auto version = reinterpret_cast<std::uint32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_abi_version"));
    auto initialize = reinterpret_cast<std::int32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_initialize"));
    State().shutdownPlugin = reinterpret_cast<std::int32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_shutdown"));
    bool compatible = false;
    try { compatible = version && initialize && State().shutdownPlugin && version() == AbiVersion; }
    catch (...) {}
    if (!compatible)
    {
        PvzpTraceAndLogLn("Plugin ABI mismatch or missing entry point");
        FreeLibrary(static_cast<HMODULE>(State().module));
        State().module = nullptr;
        State().shutdownPlugin = nullptr;
        return;
    }
    State().initializing = true;
    std::int32_t result = -1;
    {
        CallbackScope scope;
        try { result = initialize(); }
        catch (...) {}
    }
    State().initializing = false;
    if (result != 0 || !State().validated || State().stopRequested)
    {
        PvzpTraceAndLogLn("Plugin initialization failed (%d, layout validated=%d)", result, State().validated);
        Revoke();
        RequestStop();
        SafePoint();
        return;
    }
    State().enabled = true;
#endif
}

std::int32_t Update(std::uint8_t replaced, std::uint64_t rounds)
{
    SafePoint();
    if (!State().enabled || State().callbackDepth || !State().updateCallback)
        return 0;
    std::int32_t result = 2;
    {
        CallbackScope scope;
        try { result = State().updateCallback(replaced, rounds); }
        catch (...) { PvzpTraceAndLogLn("Plugin update threw an exception"); }
    }
    if (result != 0 && result != 1)
        RequestStop();
    SafePoint();
    return result;
}

void BoardDestroying()
{
    ClearBattleCallbacks();
    if (!State().enabled || !State().boardDestroyingCallback)
        return;
    CallbackScope scope;
    try { State().boardDestroyingCallback(); }
    catch (...) { RequestStop(); }
}

void Shutdown()
{
    RequestStop();
    SafePoint();
}
}
