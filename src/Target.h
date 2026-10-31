#pragma once

// Target application (whitelist) detection.
//
// Used only on the input thread (from the mouse hook and the frame timer),
// so the cache needs no locking.

#include "Common.h"

// Whether the target window belongs to a whitelisted application.
//
//   Hover mode      : the top-level window under `cursor`
//   Foreground mode : the foreground window (`cursor` is ignored)
//
// `whitelist` holds lower-case executable names (see ParseWhitelist()).
// The process lookup only runs when the target window or its process
// changes; otherwise the cached answer is returned.
//
// `root` (optional) receives the target's top-level window, or nullptr.
bool IsTargetAllowed(TargetMode mode, POINT cursor, const std::vector<std::wstring>& whitelist,
                     HWND* root = nullptr);

// Forget the cached answer, e.g. after the whitelist changed.
void InvalidateTargetCache();
