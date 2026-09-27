#include "Plugin.h"
#include "Paint.h"
#include "PluginLayout.h"
#include "PvzpDebug.h"
#include "NativeControls.h"
#include <SDL_loadso.h>
#include <cstdlib>

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <array>
#include <cstring>
#include <cwchar>
#endif

namespace PvzpPlugin
{
#ifdef _WIN32
using PluginPathChar = wchar_t;
#else
using PluginPathChar = char;
#endif
static bool LoadPath(const PluginPathChar* path, bool dependenciesBesidePlugin);
namespace
{
    Host& State() { return gLawnApp->mPlugin; }

    void Revoke()
    {
        State().updateCallback = nullptr;
        ClearPaint();
        State().boardDestroyingCallback = nullptr;
        ClearBattleCallbacks();
    }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    // One request per connection: three LE u32 values (RVP1, version 1,
    // UTF-16 code-unit count), followed by an absolute DLL path without NUL.
    // All loading occurs here on the game's outer update thread, never on a
    // remote injection thread or inside DllMain.
    std::uint32_t ReadLoadRequest(HANDLE pipe)
    {
        alignas(wchar_t) std::array<unsigned char, 12 + 32768 * sizeof(wchar_t)> bytes;
        DWORD received = 0;
        if (!ReadFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) || received < 12)
            return 1;
        std::uint32_t header[3];
        std::memcpy(header, bytes.data(), sizeof(header));
        if (header[0] != 0x31505652 || header[1] != 1 || header[2] == 0 || header[2] >= 32768
            || received != 12 + header[2] * sizeof(wchar_t))
            return 1;
        auto* path = reinterpret_cast<wchar_t*>(bytes.data() + 12);
        for (std::uint32_t i = 0; i < header[2]; ++i)
            if (!path[i]) return 1;
        path[header[2]] = L'\0';
        const bool drivePath = header[2] >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
        const bool networkPath = header[2] >= 2 && path[0] == L'\\' && path[1] == L'\\';
        if (!drivePath && !networkPath) return 1;
        if (State().module) return 2;
        return LoadPath(path, true) ? 0 : 3;
    }

    void PollExternalLoad()
    {
        if (State().callbackDepth || State().initializing) return;
        if (!State().controlAttempted)
        {
            State().controlAttempted = true;
            wchar_t name[96];
            std::swprintf(name, 96, L"\\\\.\\pipe\\pvzp-plugin-load-%lu", GetCurrentProcessId());
            HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, 64, 12 + 32768 * sizeof(wchar_t), 0, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) State().controlPipe = pipe;
            else PvzpTraceAndLogLn("Plugin bootstrap endpoint unavailable: %lu", GetLastError());
        }
        if (!State().controlPipe) return;
        HANDLE pipe = static_cast<HANDLE>(State().controlPipe);
        if (!State().controlConnected)
        {
            if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
            State().controlConnected = true;
            State().controlReplied = false;
            State().controlStarted = GetTickCount();
        }
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)
            || static_cast<std::uint32_t>(GetTickCount() - State().controlStarted) >= 2000)
        {
            DisconnectNamedPipe(pipe);
            State().controlConnected = false;
            return;
        }
        if (!State().controlReplied && available)
        {
            const std::uint32_t result = ReadLoadRequest(pipe);
            DWORD written = 0;
            // Never FlushFileBuffers here: a client must not block game updates.
            if (!WriteFile(pipe, &result, sizeof(result), &written, nullptr) || written != sizeof(result))
            {
                DisconnectNamedPipe(pipe);
                State().controlConnected = false;
            }
            State().controlReplied = true;
        }
    }
#endif

}

CallbackScope::CallbackScope() { ++State().callbackDepth; }
CallbackScope::~CallbackScope() { --State().callbackDepth; }

bool BattleDispatchEnabled() { return State().enabled && !State().callbackDepth && !State().stopRequested && State().battleInterest; }

bool SetBattleCallbacks(const BattleCallbacks* callbacks, std::uint32_t interest)
{
    if (!State().validated || State().shutdownAttempted || State().stopRequested || !callbacks || !interest || State().battleInterest)
        return false;
    if (!callbacks->beginLogicFrame || !callbacks->endLogicFrame || !callbacks->beginPlantEffect
        || !callbacks->finishPlantEffect || !callbacks->emitActivation || !callbacks->emitHomeEntry || !callbacks->emitGargantuarSpawned
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
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(State().module));
#else
        SDL_UnloadObject(State().module);
#endif
    State().module = nullptr;
    State().shutdownPlugin = nullptr;
}

static bool LoadPath(const PluginPathChar* path, bool dependenciesBesidePlugin)
{
    if (State().module || State().callbackDepth || State().initializing)
        return false;
    if (!path || !*path)
        return false;
    State().shutdownAttempted = false;
    State().stopRequested = false;
#ifdef _WIN32
    State().module = dependenciesBesidePlugin
        ? LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
        : LoadLibraryW(path);
#else
    (void)dependenciesBesidePlugin;
    State().module = SDL_LoadObject(path);
#endif
    if (!State().module)
    {
#ifdef _WIN32
        PvzpTraceAndLogLn("Plugin load failed with Windows error %lu", GetLastError());
#else
        PvzpTraceAndLogLn("Plugin load failed: %s", SDL_GetError());
#endif
        return false;
    }
#ifdef _WIN32
    auto version = reinterpret_cast<std::uint32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_abi_version"));
    auto initialize = reinterpret_cast<std::int32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_initialize"));
    State().shutdownPlugin = reinterpret_cast<std::int32_t (*)()>(GetProcAddress(static_cast<HMODULE>(State().module), "pvzp_plugin_shutdown"));
#else
    auto version = reinterpret_cast<std::uint32_t (*)()>(SDL_LoadFunction(State().module, "pvzp_plugin_abi_version"));
    auto initialize = reinterpret_cast<std::int32_t (*)()>(SDL_LoadFunction(State().module, "pvzp_plugin_initialize"));
    State().shutdownPlugin = reinterpret_cast<std::int32_t (*)()>(SDL_LoadFunction(State().module, "pvzp_plugin_shutdown"));
#endif
    bool compatible = false;
    try { compatible = version && initialize && State().shutdownPlugin && version() == AbiVersion; }
    catch (...) {}
    if (!compatible)
    {
        PvzpTraceAndLogLn("Plugin ABI mismatch or missing entry point");
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(State().module));
#else
        SDL_UnloadObject(State().module);
#endif
        State().module = nullptr;
        State().shutdownPlugin = nullptr;
        return false;
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
        return false;
    }
    State().enabled = true;
    return true;
}

#if defined(__linux__) || defined(__APPLE__)
void PollUnixControl();
void CloseUnixControl();
bool LoadUnixPath(const char* path) { return LoadPath(path, true); }
#endif

void Load()
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    LoadPath(_wgetenv(L"PVZP_PLUGIN"), false);
#else
    LoadPath(std::getenv("PVZP_PLUGIN"), false);
#endif
}

std::int32_t Update(std::uint8_t replaced, std::uint64_t rounds)
{
    SafePoint();
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    PollExternalLoad();
#elif defined(__linux__) || defined(__APPLE__)
    PollUnixControl();
#endif
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
#if defined(__linux__) || defined(__APPLE__)
    CloseUnixControl();
#endif
    RequestStop();
    SafePoint();
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (State().controlPipe)
    {
        CloseHandle(static_cast<HANDLE>(State().controlPipe));
        State().controlPipe = nullptr;
    }
#endif
}
}
