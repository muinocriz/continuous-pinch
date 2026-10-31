#pragma once

// WH_MOUSE_LL hook: modifier+Wheel zooms, modifier+Middle resets.
//
// Installed by the input thread; the callback runs on that thread.

#include "Common.h"

bool InstallMouseHook(HINSTANCE hInstance);
void UninstallMouseHook();
