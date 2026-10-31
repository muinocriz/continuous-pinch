#include "Touchpad.h"

// ============================================================================
// Synthetic touchpad API compatibility
// ============================================================================

enum : ULONG
{
    Compat_SDCO_PHYSICAL_SIZE = 0x1,
    Compat_SDCO_TOUCHPAD_GESTURE_ONLY = 0x2
};

struct SyntheticDeviceCreationParamsCompat
{
    POINTER_INPUT_TYPE pointerType;
    ULONG maxCount;
    POINTER_FEEDBACK_MODE feedbackMode;
    HMONITOR hMonitor;
    ULONG deviceWidth;
    ULONG deviceHeight;
    ULONG options;
};

using PFN_CreateSyntheticPointerDevice2 =
    HSYNTHETICPOINTERDEVICE(WINAPI*)(const SyntheticDeviceCreationParamsCompat*);

static PFN_CreateSyntheticPointerDevice2 g_CreateSyntheticPointerDevice2 = nullptr;
static HSYNTHETICPOINTERDEVICE g_touchpad = nullptr;

// v1.0.x timing: a private counter starting at 1 that advances by the
// nominal frame interval on every injected frame. Reset when the device is
// (re)created, as v1.0.x reset it together with the pinch state.
static DWORD g_syntheticTime = 1;


// ============================================================================
// Touchpad creation
// ============================================================================

static bool ResolveCreateSyntheticPointerDevice2()
{
    if (g_CreateSyntheticPointerDevice2)
        return true;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");

    if (!user32)
        return false;

    g_CreateSyntheticPointerDevice2 = reinterpret_cast<PFN_CreateSyntheticPointerDevice2>(
        GetProcAddress(user32, "CreateSyntheticPointerDevice2"));

    if (!g_CreateSyntheticPointerDevice2)
    {
        g_CreateSyntheticPointerDevice2 = reinterpret_cast<PFN_CreateSyntheticPointerDevice2>(
            GetProcAddress(user32, MAKEINTRESOURCEA(2690)));
    }

    return g_CreateSyntheticPointerDevice2 != nullptr;
}


bool CreateTouchpadDevice()
{
    if (g_touchpad)
        return true;

    if (!ResolveCreateSyntheticPointerDevice2())
        return false;

    SyntheticDeviceCreationParamsCompat params{};

    params.pointerType = PT_TOUCHPAD;
    params.maxCount = 2;
    params.feedbackMode = POINTER_FEEDBACK_NONE;
    params.hMonitor = nullptr;
    params.deviceWidth = Const::TouchpadWidth;
    params.deviceHeight = Const::TouchpadHeight;
    params.options = Compat_SDCO_PHYSICAL_SIZE | Compat_SDCO_TOUCHPAD_GESTURE_ONLY;

    SetLastError(ERROR_SUCCESS);

    g_syntheticTime = 1;

    g_touchpad = g_CreateSyntheticPointerDevice2(&params);

    return g_touchpad != nullptr;
}


void DestroyTouchpadDevice()
{
    if (!g_touchpad)
        return;

    DestroySyntheticPointerDevice(g_touchpad);

    g_touchpad = nullptr;
}


bool HasTouchpadDevice()
{
    return g_touchpad != nullptr;
}


// ============================================================================
// Frame injection
// ============================================================================

// v1.0.x flags: contacts down => INRANGE | INCONTACT | CONFIDENCE,
// lifted => CONFIDENCE only. No DOWN / UPDATE / UP transition flags.
static POINTER_FLAGS FlagsForFrame(TouchpadFrame frame)
{
    return frame == TouchpadFrame::Up
               ? POINTER_FLAG_CONFIDENCE
               : (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_CONFIDENCE);
}


bool InjectTouchpadFrame(const POINT& p1, const POINT& p2, TouchpadFrame frame)
{
    if (!g_touchpad)
        return false;

    POINTER_TYPE_INFO infos[2]{};
    const POINT points[2] = {p1, p2};

    const POINTER_FLAGS flags = FlagsForFrame(frame);
    const DWORD time = g_syntheticTime;

    for (UINT32 i = 0; i < 2; ++i)
    {
        auto& info = infos[i];
        auto& pointerInfo = info.touchInfo.pointerInfo;

        info.type = PT_TOUCHPAD;

        pointerInfo.pointerId = i;
        pointerInfo.pointerFlags = flags;
        pointerInfo.ptHimetricLocation = points[i];
        pointerInfo.dwTime = time;
    }

    if (!InjectSyntheticPointerInput(g_touchpad, infos, 2))
        return false;

    g_syntheticTime += Const::AnimationIntervalMs;

    return true;
}
