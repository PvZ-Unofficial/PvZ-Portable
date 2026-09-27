#pragma once
#include "Plugin.h"
#include <cstddef>

namespace PvzpReplay {
// Internal native serialization primitives. Loading requires a trusted,
// immutable checkpoint from this exact SDK; this is not a file-import API.
// Resource definition sizes used by cold file validation; never construct a Board.
PVZP_API int ParticleEmitterCount(int kind) noexcept;
PVZP_API int AnimationTrackCount(int kind) noexcept;
PVZP_API int AnimationFrameCount(int kind) noexcept;
PVZP_API bool SaveCheckpoint(std::uintptr_t file) noexcept;
PVZP_API bool RestoreCheckpointBytes(const unsigned char* bytes, std::size_t length, std::int32_t mode) noexcept;
PVZP_API bool RestoreCheckpoint(const char* path, std::int32_t mode) noexcept;
}
