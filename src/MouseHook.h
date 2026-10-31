#pragma once

// WH_MOUSE_LL hook: Alt+Wheel zoom, Alt+Middle reset.

#include "Common.h"

extern HHOOK g_mouseHook;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
