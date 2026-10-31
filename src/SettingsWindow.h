#pragma once

// Settings window and the exit hotkey.

#include "Common.h"

bool RegisterExitHotkey();

bool CreateSettingsWindow(HINSTANCE hInstance);
void ShowSettingsWindow();

// Keyboard navigation for the modeless Settings dialog (Tab, Enter, Esc).
// Call from the message loop; returns true if the message was handled.
bool HandleSettingsDialogMessage(MSG& msg);
