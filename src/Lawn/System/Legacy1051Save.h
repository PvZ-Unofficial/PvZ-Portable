/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef __LEGACY1051SAVE_H__
#define __LEGACY1051SAVE_H__

#include <string>

class Board;

// Imports the raw mid-level save format written by Plants vs. Zombies 1.0.0.1051.
// The caller owns final runtime-pointer repair and migration to the portable format.
bool LawnLoadLegacy1051Game(Board* theBoard, const std::string& theFilePath);

#endif
