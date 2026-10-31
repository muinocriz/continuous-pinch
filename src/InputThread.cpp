#include "InputThread.h"

#include "App.h"
#include "Config.h"
#include "MouseHook.h"
#include "Pinch.h"
#include "Target.h"
#include "Touchpad.h"
#include "Util.h"

#include <future>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ============================================================================
// State (input thread only, except where noted)
// ============================================================================

static std::thread g_inputThread;              // UI thread
static std::atomic<DWORD> g_inputThreadId{0};  // read by the UI thread

static InputConfig g_inputCfg;

static HANDLE g_frameTimer = nullptr;
static bool g_frameTimerHighRes = false;
static bool g_frameTimerRunning = false;

static double g_lastMenuMaskTime = -1.0e9;


// ============================================================================
// Configuration snapshot
// ============================================================================

static void LoadInputConfig()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    g_inputCfg.enabled = cfg.enabled;
    g_inputCfg.targetMode = cfg.targetMode;
    g_inputCfg.sensitivityLog = cfg.sensitivityLog;
    g_inputCfg.curve = cfg.curve;
    g_inputCfg.zoomModifiers = cfg.zoomModifiers;
    g_inputCfg.resetModifiers = cfg.resetModifiers;
    g_inputCfg.hardResetModifiers = cfg.hardResetModifiers;

    // Parsed once here instead of on every process lookup.
    g_inputCfg.whitelist = ParseWhitelist(cfg.whitelist);

    // The whitelist or target mode may have changed.
    InvalidateTargetCache();
}


const InputConfig& InputThreadConfig()
{
    return g_inputCfg;
}


// ============================================================================
// Keyboard state
// ============================================================================

UINT CurrentModifierMask()
{
    UINT mask = MM_NONE;

    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mask |= MM_ALT;

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mask |= MM_CTRL;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mask |= MM_SHIFT;

    return mask;
}


// A lone Alt press+release activates the focused window's menu (in Chrome /
// Edge it focuses the app menu, so the next keystrokes go there). When the
// hook swallows Alt+Wheel or Alt+Middle, that is exactly what the app sees.
// Injecting an unassigned key while Alt is still held makes the Alt release
// a normal key-combination release. Rate-limited: one mask per Alt hold is
// enough, a few per second while scrolling is harmless.
static void SendMenuMaskIfAltHeld(LPARAM altHeld)
{
    if (!altHeld)
        return;

    const double now = NowSeconds();

    if (now - g_lastMenuMaskTime < Const::MenuMaskRepeatSec)
        return;

    g_lastMenuMaskTime = now;

    INPUT inputs[2]{};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = Const::MenuMaskVk;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = Const::MenuMaskVk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}


// ============================================================================
// Frame timer
// ============================================================================

static bool CreateFrameTimer()
{
    // High-resolution waitable timers (Windows 10 1803+) fire on time without
    // raising the global timer resolution with timeBeginPeriod().
    g_frameTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);

    g_frameTimerHighRes = g_frameTimer != nullptr;

    if (!g_frameTimer)
        g_frameTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    return g_frameTimer != nullptr;
}


static void StartFrameTimer()
{
    if (g_frameTimerRunning)
        return;

    // Fallback timer only: raise the timer resolution while animating, never
    // while idle.
    if (!g_frameTimerHighRes)
        timeBeginPeriod(1);

    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(Const::AnimationIntervalMs) * 10000;  // relative, 100 ns

    SetWaitableTimer(g_frameTimer, &due, static_cast<LONG>(Const::AnimationIntervalMs), nullptr,
                     nullptr, FALSE);

    g_frameTimerRunning = true;
}


static void StopFrameTimer()
{
    if (!g_frameTimerRunning)
        return;

    CancelWaitableTimer(g_frameTimer);

    if (!g_frameTimerHighRes)
        timeEndPeriod(1);

    g_frameTimerRunning = false;
}


// Run the frame timer only while the engine has work to do. When nothing is
// being zoomed the input thread sleeps until the next hook event.
static void UpdateFrameTimer()
{
    if (g_inputCfg.enabled && PinchNeedsTicks())
        StartFrameTimer();
    else
        StopFrameTimer();
}


static void OnFrameTimer()
{
    if (!g_inputCfg.enabled)
    {
        EndActiveSegment();
        return;
    }

    const bool zoomModifierHeld = CurrentModifierMask() == g_inputCfg.zoomModifiers;

    POINT cursor{};
    GetCursorPos(&cursor);

    const bool targetAllowed = IsTargetAllowed(g_inputCfg.targetMode, cursor, g_inputCfg.whitelist);

    TickPinch(g_inputCfg.curve, zoomModifierHeld, targetAllowed);
}


// ============================================================================
// Commands
// ============================================================================

static void HandleInputMessage(const MSG& msg)
{
    switch (msg.message)
    {
        case Const::WM_INPUT_WHEEL:
            SendMenuMaskIfAltHeld(msg.lParam);

            if (g_inputCfg.enabled)
            {
                ApplyWheelDelta(static_cast<short>(LOWORD(msg.wParam)), g_inputCfg.sensitivityLog);
            }
            break;

        case Const::WM_INPUT_SOFT_RESET:
            SendMenuMaskIfAltHeld(msg.lParam);
            RequestSoftReset();
            break;

        case Const::WM_INPUT_HARD_RESET:
            SendMenuMaskIfAltHeld(msg.lParam);

            if (!HardResetDevice())
                PostMessageW(g_mainWindow, Const::WM_APP_HARD_RESET_FAILED, 0, 0);
            break;

        case Const::WM_INPUT_CONFIG_CHANGED:
            LoadInputConfig();

            if (!g_inputCfg.enabled)
                EndActiveSegment();
            break;

        case Const::WM_INPUT_END_GESTURE:
            EndActiveSegment();
            break;

        default:
            break;
    }
}


// ============================================================================
// Thread
// ============================================================================

static void RunInputLoop()
{
    for (;;)
    {
        // Wakes for the frame timer, posted commands, and sent messages --
        // the low-level hook callback is delivered as a sent message and runs
        // inside PeekMessageW() below.
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &g_frameTimer, INFINITE, QS_ALLINPUT,
                                                       MWMO_INPUTAVAILABLE);

        if (wait == WAIT_OBJECT_0)
        {
            OnFrameTimer();
        }
        else if (wait == WAIT_FAILED)
        {
            Sleep(1);
        }

        MSG msg{};

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return;

            HandleInputMessage(msg);
        }

        UpdateFrameTimer();
    }
}


static void InputThreadMain(HINSTANCE hInstance, std::promise<InputStartError>* ready)
{
    // Create this thread's message queue before anyone posts to it.
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    // Mouse input and gesture frames are latency-sensitive; the work per
    // wake-up is tiny, so a modest boost cannot starve anything.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    LoadInputConfig();

    InputStartError error{};

    if (!CreateTouchpadDevice())
    {
        error = {InputStartError::Stage::Touchpad, GetLastError()};
    }
    else if (!CreateFrameTimer())
    {
        error = {InputStartError::Stage::Timer, GetLastError()};
    }
    else if (!InstallMouseHook(hInstance))
    {
        error = {InputStartError::Stage::MouseHook, GetLastError()};
    }

    if (error.stage != InputStartError::Stage::None)
    {
        if (g_frameTimer)
        {
            CloseHandle(g_frameTimer);
            g_frameTimer = nullptr;
        }

        DestroyTouchpadDevice();

        ready->set_value(error);
        return;
    }

    g_inputThreadId.store(GetCurrentThreadId());

    ready->set_value(error);  // `ready` must not be used after this point

    RunInputLoop();

    g_inputThreadId.store(0);

    EndActiveSegment();
    StopFrameTimer();
    UninstallMouseHook();
    DestroyTouchpadDevice();

    CloseHandle(g_frameTimer);
    g_frameTimer = nullptr;
}


bool StartInputThread(HINSTANCE hInstance, InputStartError& error)
{
    std::promise<InputStartError> ready;
    std::future<InputStartError> result = ready.get_future();

    g_inputThread = std::thread(InputThreadMain, hInstance, &ready);

    error = result.get();

    if (error.stage != InputStartError::Stage::None)
    {
        g_inputThread.join();
        return false;
    }

    return true;
}


void StopInputThread()
{
    if (!g_inputThread.joinable())
        return;

    const DWORD id = g_inputThreadId.load();

    if (id != 0)
        PostThreadMessageW(id, WM_QUIT, 0, 0);

    g_inputThread.join();
}


void PostInputCommand(UINT message, WPARAM wParam, LPARAM lParam)
{
    const DWORD id = g_inputThreadId.load();

    if (id != 0)
        PostThreadMessageW(id, message, wParam, lParam);
}


void NotifyInputConfigChanged()
{
    PostInputCommand(Const::WM_INPUT_CONFIG_CHANGED);
}
