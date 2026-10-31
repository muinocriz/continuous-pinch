#pragma once

// Target application (whitelist) detection.

#include "Common.h"

// Whether the current target window belongs to a whitelisted application.
extern std::atomic<bool> g_targetAllowed;

// Refresh the whitelist result for the selected target mode.
// Hover mode uses WindowFromPoint(); foreground mode uses GetForegroundWindow().
// Process lookup runs only when the selected root HWND changes, or when
// force=true after config changes.
void RefreshTargetAllowed(bool force = false);
