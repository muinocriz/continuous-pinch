#pragma once

// Shared includes, constants and configuration types.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include "../resource.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

namespace Const
{

constexpr wchar_t AppName[] = L"Continuous Pinch V3";
constexpr wchar_t ConfigFileName[] = L"continuous_pinch_v3.ini";
constexpr wchar_t AppDataFolderName[] = L"ContinuousPinch";
constexpr wchar_t RunRegistryValue[] = L"ContinuousPinchV3";
constexpr wchar_t MainWindowClass[] = L"ContinuousPinchV3MainWindow";
constexpr wchar_t SingleInstanceMutex[] = L"Local\\ContinuousPinchV3.SingleInstance";

// INI keys. Keys must never be localized.
constexpr wchar_t IniKeyEnabled[] = L"Enabled";
constexpr wchar_t IniKeyEnabledLegacy[] = L"启用";  // v1.0.0 / v1.0.1 bug

// Synthetic precision touchpad size: 100 mm x 60 mm in HIMETRIC.
constexpr LONG TouchpadWidth = 10000;
constexpr LONG TouchpadHeight = 6000;
constexpr LONG CenterX = TouchpadWidth / 2;
constexpr LONG CenterY = TouchpadHeight / 2;

// A slightly wider neutral separation gives each wheel notch more absolute
// finger travel while preserving the same scale ratio. This helps Windows'
// touchpad gesture recognizer notice slow single-notch input.
constexpr double BaseDistance = 4200.0;
constexpr double MinDistance = 1000.0;
constexpr double MaxDistance = 8200.0;
constexpr double MaxLogicalScale = 16.0;

// Gesture frame interval. The input thread drives it with a high-resolution
// waitable timer that only runs while a gesture is in progress.
constexpr UINT AnimationIntervalMs = 8;  // 125 Hz
constexpr double DistanceEpsilon = 2.0;
constexpr double LogZoomEpsilon = 0.0015;
constexpr double WatchdogSec = 3.0;
constexpr double MaxAbsNotchesPerEvent = 5.0;

constexpr double LinearFingerSpeed = 23000.0;
constexpr double ExponentialRate = 64.0;
constexpr double AdaptiveRateNear = 46.0;
constexpr double AdaptiveRateFar = 88.0;
constexpr double AdaptiveFullDiff = 900.0;
constexpr double MaxFingerSpeed = 22000.0;
constexpr double ResetFingerSpeed = 26000.0;

// Set POINTER_FLAG_DOWN / UPDATE / UP on injected touchpad frames, as
// documented for InjectSyntheticPointerInput. v1.0.x injected frames
// without these transition flags. If pinch zoom stops working on some
// Windows build, set this to false to restore the v1.0.x behavior.
constexpr bool UsePointerTransitionFlags = true;

// "Menu mask" key. A lone Alt press+release activates the menu / toolbar of
// the focused window (in Chrome / Edge: the app menu). When we swallow
// Alt+Wheel or Alt+Middle, the app only sees Alt down/up, so we inject this
// key while Alt is held. 0xE8 is unassigned and has no side effects; it is
// the same default AutoHotkey uses (A_MenuMaskKey).
constexpr WORD MenuMaskVk = 0xE8;
constexpr double MenuMaskRepeatSec = 0.25;

constexpr int ExitHotkeyId = 3001;

// Tray
constexpr UINT TrayIconId = 1;
constexpr UINT WM_TRAY = WM_APP + 20;

// Main (UI) window messages
constexpr UINT WM_APP_SHOW_SETTINGS = WM_APP + 4;
constexpr UINT WM_APP_HARD_RESET_FAILED = WM_APP + 5;

// Input thread messages (PostThreadMessage)
constexpr UINT WM_INPUT_WHEEL = WM_APP + 101;       // wParam: wheel delta, lParam: Alt held
constexpr UINT WM_INPUT_SOFT_RESET = WM_APP + 102;  // lParam: Alt held
constexpr UINT WM_INPUT_HARD_RESET = WM_APP + 103;  // lParam: Alt held
constexpr UINT WM_INPUT_CONFIG_CHANGED = WM_APP + 104;
constexpr UINT WM_INPUT_END_GESTURE = WM_APP + 105;

// Tray menu IDs
constexpr UINT IDM_TRAY_ENABLE = 4001;
constexpr UINT IDM_TRAY_SETTINGS = 4002;
constexpr UINT IDM_TRAY_RESET = 4003;
constexpr UINT IDM_TRAY_HARDRESET = 4004;
constexpr UINT IDM_TRAY_STARTUP = 4005;
constexpr UINT IDM_TRAY_EXIT = 4006;

// Settings controls
constexpr int IDC_SENS_SLIDER = 5001;
constexpr int IDC_SENS_VALUE = 5002;
constexpr int IDC_CURVE = 5003;
constexpr int IDC_WHITELIST = 5004;
constexpr int IDC_ZOOM_MOD = 5005;
constexpr int IDC_RESET_MOD = 5006;
constexpr int IDC_HARDRESET_MOD = 5007;
constexpr int IDC_EXIT_HOTKEY = 5008;
constexpr int IDC_STARTUP = 5009;
constexpr int IDC_ENABLED = 5010;
constexpr int IDC_APPLY = 5011;
constexpr int IDC_CLOSE = 5012;
constexpr int IDC_DEFAULTS = 5013;
constexpr int IDC_LANGUAGE = 5014;
constexpr int IDC_TARGET_MODE = 5015;

}  // namespace Const


// ============================================================================
// Configuration types
// ============================================================================

enum class SmoothingCurve : int
{
    Direct = 0,
    Linear = 1,
    Exponential = 2,
    Adaptive = 3
};

enum class UiLanguage : int
{
    Chinese = 0,
    English = 1
};

enum class TargetMode : int
{
    Hover = 0,
    Foreground = 1
};

enum ModifierMask : UINT
{
    MM_NONE = 0,
    MM_ALT = 1 << 0,
    MM_CTRL = 1 << 1,
    MM_SHIFT = 1 << 2
};

struct AppConfig
{
    bool enabled = true;
    bool startWithWindows = false;
    UiLanguage language = UiLanguage::Chinese;
    TargetMode targetMode = TargetMode::Hover;

    // Natural logarithmic zoom increment per standard wheel notch.
    double sensitivityLog = 0.070;

    SmoothingCurve curve = SmoothingCurve::Adaptive;

    UINT zoomModifiers = MM_ALT;
    UINT resetModifiers = MM_ALT;
    UINT hardResetModifiers = MM_ALT | MM_CTRL;

    UINT exitVk = VK_F12;
    UINT exitMods = MOD_CONTROL | MOD_ALT | MOD_SHIFT;

    std::wstring whitelist = L"chrome.exe;msedge.exe";
};
