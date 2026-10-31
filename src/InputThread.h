#pragma once

// The input thread owns everything on the input path:
//
//   - the WH_MOUSE_LL hook (its callback runs on this thread),
//   - the pinch engine (Pinch.cpp) and the synthetic touchpad,
//   - the frame timer, which only runs while a gesture is in progress.
//
// Keeping this off the UI thread means a busy UI (Settings window, INI or
// registry writes, message boxes) can never delay mouse input or make
// Windows silently remove the hook (LowLevelHooksTimeout). It also means the
// input side is single-threaded: the UI thread never touches engine state,
// it only posts commands and config-change notifications.

#include "Common.h"

// Snapshot of the configuration used on the input thread. Rebuilt from
// g_cfg whenever the UI calls NotifyInputConfigChanged().
struct InputConfig
{
    bool enabled = true;
    TargetMode targetMode = TargetMode::Hover;
    double sensitivityLog = 0.070;
    SmoothingCurve curve = SmoothingCurve::Adaptive;

    UINT zoomModifiers = MM_ALT;
    UINT resetModifiers = MM_ALT;
    UINT hardResetModifiers = MM_ALT | MM_CTRL;

    std::vector<std::wstring> whitelist;  // lower-case exe names
};

struct InputStartError
{
    enum class Stage
    {
        None,
        Touchpad,
        Timer,
        MouseHook
    };

    Stage stage = Stage::None;
    DWORD code = 0;  // GetLastError() of the failing call
};

// ---- UI thread -------------------------------------------------------------

// Starts the input thread and waits until it has created the touchpad and
// installed the mouse hook. Returns false (and fills `error`) on failure.
bool StartInputThread(HINSTANCE hInstance, InputStartError& error);

// Lifts any active contacts, removes the hook, destroys the touchpad and
// joins the thread.
void StopInputThread();

// Re-read g_cfg on the input thread. Call after every change to g_cfg.
void NotifyInputConfigChanged();

// Post one of the Const::WM_INPUT_* commands to the input thread.
void PostInputCommand(UINT message, WPARAM wParam = 0, LPARAM lParam = 0);

// ---- Input thread only -----------------------------------------------------

const InputConfig& InputThreadConfig();

// MM_* mask of the modifier keys currently held.
UINT CurrentModifierMask();
