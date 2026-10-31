// ============================================================================
// Continuous Pinch V3
// Windows 11 Synthetic Precision Touchpad
//
// Features:
//   - System tray
//   - INI configuration beside the EXE
//   - Configurable mouse modifiers + exit hotkey
//   - Multiple smoothing curves
//   - Application whitelist UI
//   - Real-time sensitivity slider
//   - Start with Windows
//
// Defaults:
//   Alt + Wheel            : zoom, never below logical 100% baseline
//   Alt + Middle Click     : smoothly return to baseline
//   Ctrl + Alt + Middle    : hard reset synthetic touchpad
//   Ctrl + Alt + Shift+F12 : exit
//
// IMPORTANT:
// "100%" is the baseline maintained by this tool. If a page is pinch-zoomed
// through some other device while V3 is running, V3 cannot read that external
// visual-viewport scale back from Chromium.
// ============================================================================

#include "App.h"
#include "Config.h"
#include "MouseHook.h"
#include "Pinch.h"
#include "SettingsWindow.h"
#include "Target.h"
#include "Touchpad.h"
#include "Tray.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")

HWND g_mainWindow = nullptr;
HWND g_settingsWindow = nullptr;

// Broadcast by Explorer when the taskbar is (re)created, e.g. after
// explorer.exe restarts. Tray icons must be re-added in response.
static UINT g_taskbarCreatedMsg = 0;

// ============================================================================
// Main window
// ============================================================================

static void ToggleEnabled()
{
    bool enabled;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        enabled = !g_committedCfg.enabled;

        // Tray toggles commit immediately. Update only this field so any
        // unapplied preview values in g_cfg are neither saved nor lost.
        g_committedCfg.enabled = enabled;
        g_cfg.enabled = enabled;
    }

    g_enabled.store(enabled, std::memory_order_relaxed);

    if (!enabled && g_pinch.active)
    {
        EndActiveSegment();
    }

    SaveConfig();
    UpdateTrayTooltip();

    // Keep an open Settings window in sync, so a later Apply does not
    // silently undo the tray toggle.
    if (g_settingsWindow)
    {
        SendMessageW(GetDlgItem(g_settingsWindow, Const::IDC_ENABLED), BM_SETCHECK,
                     enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}


static void ToggleStartup()
{
    bool startup;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        startup = !g_committedCfg.startWithWindows;

        g_committedCfg.startWithWindows = startup;
        g_cfg.startWithWindows = startup;
    }

    SaveConfig();

    if (g_settingsWindow)
    {
        SendMessageW(GetDlgItem(g_settingsWindow, Const::IDC_STARTUP), BM_SETCHECK,
                     startup ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}


static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg)
    {
        AddTrayIcon();
        UpdateTrayTooltip();
        return 0;
    }

    switch (msg)
    {
        case Const::WM_APP_WHEEL:
        {
            const short delta = static_cast<short>(LOWORD(wParam));

            ApplyWheelDelta(delta);

            return 0;
        }

        case Const::WM_APP_SOFT_RESET:
        {
            RequestSoftReset();
            return 0;
        }

        case Const::WM_APP_HARD_RESET:
        {
            if (!HardResetDevice())
            {
                MessageBoxW(nullptr,
                            L10n(L"硬复位失败：无法重新创建虚拟触控板。",
                                 L"Hard reset failed to recreate the synthetic touchpad."),
                            Const::AppName, MB_OK | MB_ICONERROR);
            }

            return 0;
        }

        case Const::WM_APP_SHOW_SETTINGS:
        {
            ShowSettingsWindow();
            return 0;
        }

        case WM_TIMER:
        {
            if (wParam == Const::AnimationTimerId)
            {
                TickPinch();
                return 0;
            }

            break;
        }

        case WM_HOTKEY:
        {
            if (wParam == Const::ExitHotkeyId)
            {
                DestroyWindow(hwnd);

                return 0;
            }

            break;
        }

        case Const::WM_TRAY:
        {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            {
                ShowTrayMenu();
                return 0;
            }

            if (lParam == WM_LBUTTONDBLCLK)
            {
                ShowSettingsWindow();
                return 0;
            }

            break;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case Const::IDM_TRAY_ENABLE:
                    ToggleEnabled();
                    return 0;

                case Const::IDM_TRAY_SETTINGS:
                    ShowSettingsWindow();
                    return 0;

                case Const::IDM_TRAY_RESET:
                    RequestSoftReset();
                    return 0;

                case Const::IDM_TRAY_HARDRESET:
                    HardResetDevice();
                    return 0;

                case Const::IDM_TRAY_STARTUP:
                    ToggleStartup();
                    return 0;

                case Const::IDM_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
            }

            break;
        }

        case WM_DISPLAYCHANGE:
        {
            if (g_pinch.active)
                EndActiveSegment();

            return 0;
        }

        case WM_QUERYENDSESSION:
        {
            if (g_pinch.active)
                EndActiveSegment();

            return TRUE;
        }

        case WM_DESTROY:
        {
            if (g_pinch.active)
                EndActiveSegment();

            RemoveTrayIcon();

            DestroyTouchpadDevice();

            KillTimer(hwnd, Const::AnimationTimerId);

            UnregisterHotKey(hwnd, Const::ExitHotkeyId);

            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// ============================================================================
// Entry
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Single instance: a second launch opens the running instance's Settings
    // instead of creating a second hook, touchpad and tray icon.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, Const::SingleInstanceMutex);

    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(Const::MainWindowClass, nullptr);

        if (existing)
        {
            DWORD pid = 0;

            GetWindowThreadProcessId(existing, &pid);

            // Let the running instance bring Settings to the foreground.
            AllowSetForegroundWindow(pid);

            PostMessageW(existing, Const::WM_APP_SHOW_SETTINGS, 0, 0);
        }

        CloseHandle(instanceMutex);

        return 0;
    }

    // Held for the lifetime of the process; released on every return path.
    struct MutexHolder
    {
        HANDLE handle;

        ~MutexHolder()
        {
            if (handle)
                CloseHandle(handle);
        }
    } instanceMutexHolder{instanceMutex};

    timeBeginPeriod(1);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;

    InitCommonControlsEx(&icc);

    g_configPath = GetConfigPath();

    LoadConfig();

    if (!CreateTouchpadDevice())
    {
        const DWORD err = GetLastError();

        wchar_t text[512]{};

        swprintf_s(text,
                   L"Failed to create a Windows 11 synthetic precision touchpad.\n\n"
                   L"GetLastError = %lu",
                   err);

        MessageBoxW(nullptr, text, Const::AppName, MB_OK | MB_ICONERROR);

        timeEndPeriod(1);
        return 1;
    }

    const wchar_t* mainClass = Const::MainWindowClass;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.lpszClassName = mainClass;

    if (!RegisterClassExW(&wc))
    {
        DestroyTouchpadDevice();
        timeEndPeriod(1);
        return 1;
    }

    // A hidden top-level window, NOT a message-only (HWND_MESSAGE) window:
    //   - message-only windows cannot become the foreground window, so the
    //     tray menu would not close when clicking elsewhere;
    //   - message-only windows do not receive broadcasts such as
    //     TaskbarCreated, WM_DISPLAYCHANGE and WM_QUERYENDSESSION;
    //   - FindWindowW() (used by the single-instance check) skips them.
    // The window is never shown; WS_EX_TOOLWINDOW keeps it out of Alt+Tab.
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    g_mainWindow = CreateWindowExW(WS_EX_TOOLWINDOW, mainClass, Const::AppName, WS_POPUP, 0, 0, 0,
                                   0, nullptr, nullptr, hInstance, nullptr);

    if (!g_mainWindow)
    {
        DestroyTouchpadDevice();
        timeEndPeriod(1);
        return 1;
    }

    // Allow TaskbarCreated through UIPI in case the tool runs elevated.
    if (g_taskbarCreatedMsg != 0)
    {
        ChangeWindowMessageFilterEx(g_mainWindow, g_taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);
    }

    if (!CreateSettingsWindow(hInstance))
    {
        DestroyWindow(g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    if (!SetTimer(g_mainWindow, Const::AnimationTimerId, Const::AnimationIntervalMs, nullptr))
    {
        DestroyWindow(g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    RegisterExitHotkey();

    // Default target mode is Hover, so the browser does not need focus.
    // Existing users can switch to Foreground mode in Settings.
    RefreshTargetAllowed(true);

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

    if (!g_mouseHook)
    {
        DestroyWindow(g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    AddTrayIcon();
    UpdateTrayTooltip();

    // First run: show Settings so the controls are discoverable.
    if (GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        SaveConfig();
        ShowSettingsWindow();
    }

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);

        DispatchMessageW(&msg);
    }

    if (g_mouseHook)
    {
        UnhookWindowsHookEx(g_mouseHook);

        g_mouseHook = nullptr;
    }

    if (g_settingsWindow)
    {
        DestroyWindow(g_settingsWindow);

        g_settingsWindow = nullptr;
    }

    g_mainWindow = nullptr;

    timeEndPeriod(1);

    return 0;
}
