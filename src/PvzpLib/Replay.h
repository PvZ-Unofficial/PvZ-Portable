#pragma once
#include "Plugin.h"

namespace PvzpReplay {
// Internal native serialization primitives. Loading requires a trusted,
// immutable checkpoint from this exact SDK; this is not a file-import API.
PVZP_API bool SaveCheckpoint(std::uintptr_t file) noexcept;
PVZP_API bool RestoreCheckpoint(const char* path, std::int32_t mode) noexcept;
}
