#pragma once

#include <cstdint>

// The SDK and game must come from the same build and C++ ABI. Only this fixed
// layout protocol may be used before ValidateLayout succeeds.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
# if defined(PVZP_BUILD_GAME)
#  define PVZP_API __declspec(dllexport)
# else
#  define PVZP_API __declspec(dllimport)
# endif
#else
# define PVZP_API
#endif

namespace PvzpPlugin
{
inline constexpr std::uint32_t AbiVersion = 6;

struct LayoutEntry
{
    std::uint64_t name;
    std::uint64_t value;
};

// Each callback belongs to the current App. Battle registrations are revoked
// both on ExitFight and before destroying its Board.
// 0: advance native logic; 1: skip this update; any other value: request stop.
using UpdateCallback = std::int32_t (*)(std::uint8_t, std::uint64_t);
using BoardDestroyingCallback = void (*)();

struct BattleCallbacks
{
    void (*beginLogicFrame)(std::uint64_t, std::int32_t) = nullptr;
    void (*endLogicFrame)(std::int32_t) = nullptr;
    std::uint64_t (*beginPlantEffect)(std::int32_t, void*, void*, std::int32_t, std::int32_t) = nullptr;
    void (*finishPlantEffect)(std::uint32_t, std::int32_t, std::int32_t) = nullptr;
    void (*emitHomeEntry)(void*) = nullptr;
    void (*emitActivation)(std::int32_t,std::int32_t,std::int32_t,std::int32_t,std::int32_t,std::int32_t) = nullptr;
    void (*emitGargantuarSpawned)(void*) = nullptr;
    void (*emitImpThrown)(void*, void*) = nullptr;
    void (*emitGargantuarAshHit)(void*) = nullptr;
};

// Owned by LawnApp; there is one physical plugin slot for that App's lifetime.
struct Host
{
    void* module = nullptr;
    std::int32_t (*shutdownPlugin)() = nullptr;
    UpdateCallback updateCallback = nullptr;
    void (*paintCallback)() = nullptr;
    BoardDestroyingCallback boardDestroyingCallback = nullptr;
    BattleCallbacks battleCallbacks;
    std::uint32_t battleInterest = 0;
    std::uint32_t callbackDepth = 0;
    bool validated = false;
    bool initializing = false;
    bool enabled = false;
    bool stopRequested = false;
    bool shutdownAttempted = false;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    // Owned bootstrap endpoint; no game object addresses or query results.
    void* controlPipe = nullptr;
    std::uint32_t controlStarted = 0;
    bool controlAttempted = false;
    bool controlConnected = false;
    bool controlReplied = false;
#endif
};

PVZP_API bool ValidateLayout(const LayoutEntry* entries, std::uint32_t count);
PVZP_API bool SetUpdateCallback(UpdateCallback callback);
PVZP_API bool SetBoardDestroyingCallback(BoardDestroyingCallback callback);
PVZP_API void RequestStop();
PVZP_API bool SetBattleCallbacks(const BattleCallbacks* callbacks, std::uint32_t interest);
PVZP_API void ClearBattleCallbacks();

// Called by LawnApp outside loader lock. Nested UI loops continue native
// updates but cannot dispatch another ordinary plugin callback.
void Load();
std::int32_t Update(std::uint8_t replaced, std::uint64_t rounds);
void BoardDestroying();
void Shutdown();
void SafePoint();
bool InCallback();
bool BattleDispatchEnabled();
struct CallbackScope
{
    CallbackScope();
    ~CallbackScope();
};
}
