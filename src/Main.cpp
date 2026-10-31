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
#include "InputThread.h"
#include "SettingsWindow.h"
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

    // Lifts an active gesture on the input thread if the tool was disabled.
    NotifyInputConfigChanged();

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
        case Const::WM_APP_HARD_RESET_FAILED:
        {
            // Posted by the input thread; shown here so the input thread
            // never blocks in a message box.
            MessageBoxW(nullptr,
                        L10n(L"硬复位失败：无法重新创建虚拟触控板。",
                             L"Hard reset failed to recreate the synthetic touchpad."),
                        Const::AppName, MB_OK | MB_ICONERROR);

            return 0;
        }

        case Const::WM_APP_SHOW_SETTINGS:
        {
            ShowSettingsWindow();
            return 0;
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
                    PostInputCommand(Const::WM_INPUT_SOFT_RESET);
                    return 0;

                case Const::IDM_TRAY_HARDRESET:
                    PostInputCommand(Const::WM_INPUT_HARD_RESET);
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
            PostInputCommand(Const::WM_INPUT_END_GESTURE);

            return 0;
        }

        case WM_QUERYENDSESSION:
        {
            PostInputCommand(Const::WM_INPUT_END_GESTURE);

            return TRUE;
        }

        case WM_DESTROY:
        {
            // Lifts any active contacts, removes the hook and destroys the
            // touchpad on the input thread, then joins it.
            StopInputThread();

            RemoveTrayIcon();

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

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;

    InitCommonControlsEx(&icc);

    g_configPath = GetConfigPath();

    LoadConfig();

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
        return 1;
    }

    // Creates the synthetic touchpad and installs the mouse hook on the
    // input thread. Needs g_mainWindow (the input thread posts to it).
    InputStartError inputError{};

    if (!StartInputThread(hInstance, inputError))
    {
        const wchar_t* what = L"";

        switch (inputError.stage)
        {
            case InputStartError::Stage::Touchpad:
                what = L"Failed to create a Windows 11 synthetic precision touchpad.";
                break;

            case InputStartError::Stage::Timer:
                what = L"Failed to create the frame timer.";
                break;

            case InputStartError::Stage::MouseHook:
                what = L"Failed to install the low-level mouse hook.";
                break;

            case InputStartError::Stage::None:
                break;
        }

        wchar_t text[512]{};

        swprintf_s(text, L"%s\n\nGetLastError = %lu", what, inputError.code);

        MessageBoxW(nullptr, text, Const::AppName, MB_OK | MB_ICONERROR);

        DestroyWindow(g_mainWindow);
        return 1;
    }

    RegisterExitHotkey();

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

    // Normally already stopped in WM_DESTROY; harmless if so.
    StopInputThread();

    if (g_settingsWindow)
    {
        DestroyWindow(g_settingsWindow);

        g_settingsWindow = nullptr;
    }

    g_mainWindow = nullptr;

    return 0;
}
