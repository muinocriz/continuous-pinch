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
HSYNTHETICPOINTERDEVICE g_touchpad = nullptr;


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
