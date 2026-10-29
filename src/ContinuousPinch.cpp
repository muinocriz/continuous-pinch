#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define IDI_APP_ICON 101

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")

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

namespace Const {

constexpr wchar_t AppName[] = L"Continuous Pinch V3";
constexpr wchar_t ConfigFileName[] = L"continuous_pinch_v3.ini";
constexpr wchar_t RunRegistryValue[] = L"ContinuousPinchV3";

// Synthetic precision touchpad size: 100 mm x 60 mm in HIMETRIC.
constexpr LONG TouchpadWidth  = 10000;
constexpr LONG TouchpadHeight = 6000;
constexpr LONG CenterX = TouchpadWidth / 2;
constexpr LONG CenterY = TouchpadHeight / 2;

// A slightly wider neutral separation gives each wheel notch more absolute
// finger travel while preserving the same scale ratio. This helps Windows'
 // touchpad gesture recognizer notice slow single-notch input.
constexpr double BaseDistance = 4200.0;
constexpr double MinDistance  = 1000.0;
constexpr double MaxDistance  = 8200.0;
constexpr double MaxLogicalScale = 16.0;

constexpr UINT AnimationTimerId = 1;
constexpr UINT AnimationIntervalMs = 8;  // ~125 Hz
constexpr double DistanceEpsilon = 2.0;
constexpr double LogZoomEpsilon = 0.0015;
constexpr double ReleaseDelaySec = 0.120;
constexpr double WatchdogSec = 3.0;
constexpr double MaxAbsNotchesPerEvent = 5.0;

constexpr double LinearFingerSpeed = 23000.0;
constexpr double ExponentialRate = 64.0;
constexpr double AdaptiveRateNear = 46.0;
constexpr double AdaptiveRateFar  = 88.0;
constexpr double AdaptiveFullDiff = 900.0;
constexpr double MaxFingerSpeed = 22000.0;
constexpr double ResetFingerSpeed = 26000.0;

constexpr int ExitHotkeyId = 3001;

// Tray
constexpr UINT TrayIconId = 1;
constexpr UINT WM_TRAY = WM_APP + 20;

// App messages
constexpr UINT WM_APP_WHEEL      = WM_APP + 1;
constexpr UINT WM_APP_SOFT_RESET = WM_APP + 2;
constexpr UINT WM_APP_HARD_RESET = WM_APP + 3;
constexpr UINT WM_APP_SHOW_SETTINGS = WM_APP + 4;

// Tray menu IDs
constexpr UINT IDM_TRAY_ENABLE   = 4001;
constexpr UINT IDM_TRAY_SETTINGS = 4002;
constexpr UINT IDM_TRAY_RESET    = 4003;
constexpr UINT IDM_TRAY_HARDRESET= 4004;
constexpr UINT IDM_TRAY_STARTUP  = 4005;
constexpr UINT IDM_TRAY_EXIT     = 4006;

// Settings controls
constexpr int IDC_SENS_SLIDER   = 5001;
constexpr int IDC_SENS_VALUE    = 5002;
constexpr int IDC_CURVE         = 5003;
constexpr int IDC_WHITELIST     = 5004;
constexpr int IDC_ZOOM_MOD      = 5005;
constexpr int IDC_RESET_MOD     = 5006;
constexpr int IDC_HARDRESET_MOD = 5007;
constexpr int IDC_EXIT_HOTKEY   = 5008;
constexpr int IDC_STARTUP       = 5009;
constexpr int IDC_ENABLED       = 5010;
constexpr int IDC_APPLY         = 5011;
constexpr int IDC_CLOSE         = 5012;
constexpr int IDC_DEFAULTS      = 5013;
constexpr int IDC_LANGUAGE      = 5014;

} // namespace Const


// ============================================================================
// Config
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

enum ModifierMask : UINT
{
    MM_NONE  = 0,
    MM_ALT   = 1 << 0,
    MM_CTRL  = 1 << 1,
    MM_SHIFT = 1 << 2
};

struct AppConfig
{
    bool enabled = true;
    bool startWithWindows = false;
    UiLanguage language = UiLanguage::Chinese;

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

static AppConfig g_cfg;
static std::mutex g_cfgMutex;

// ============================================================================
// Localization
// ============================================================================

static UiLanguage CurrentLanguage()
{
    std::lock_guard<std::mutex> lock(g_cfgMutex);
    return g_cfg.language;
}

static const wchar_t* L10n(
    const wchar_t* zh,
    const wchar_t* en)
{
    return CurrentLanguage() == UiLanguage::Chinese
        ? zh
        : en;
}



// ============================================================================
// Synthetic touchpad API compatibility
// ============================================================================

enum : ULONG
{
    Compat_SDCO_PHYSICAL_SIZE          = 0x1,
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
    HSYNTHETICPOINTERDEVICE (WINAPI*)(
        const SyntheticDeviceCreationParamsCompat*);

static PFN_CreateSyntheticPointerDevice2 g_CreateSyntheticPointerDevice2 = nullptr;
static HSYNTHETICPOINTERDEVICE g_touchpad = nullptr;


// ============================================================================
// Globals
// ============================================================================

static HWND g_mainWindow = nullptr;
static HWND g_settingsWindow = nullptr;

static HHOOK g_mouseHook = nullptr;
static HWINEVENTHOOK g_foregroundHook = nullptr;

static std::atomic<bool> g_foregroundAllowed{ false };
static std::atomic<bool> g_swallowMiddleUp{ false };
static std::atomic<bool> g_enabled{ true };

static NOTIFYICONDATAW g_nid{};

static std::wstring g_configPath;


// ============================================================================
// Time
// ============================================================================

static double NowSeconds()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);

    return static_cast<double>(counter.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}


// ============================================================================
// String helpers
// ============================================================================

static std::wstring Trim(std::wstring s)
{
    auto notSpace = [](wchar_t c) {
        return !iswspace(c);
    };

    auto first = std::find_if(s.begin(), s.end(), notSpace);
    auto last = std::find_if(s.rbegin(), s.rend(), notSpace).base();

    if (first >= last)
        return L"";

    return std::wstring(first, last);
}


static std::wstring ToLower(std::wstring s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](wchar_t c) {
            return static_cast<wchar_t>(towlower(c));
        });

    return s;
}


static std::wstring BaseName(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");

    return (pos == std::wstring::npos)
        ? path
        : path.substr(pos + 1);
}


static std::vector<std::wstring> ParseWhitelist(const std::wstring& text)
{
    std::vector<std::wstring> result;

    std::wstring normalized = text;

    for (auto& ch : normalized)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L',')
            ch = L';';
    }

    std::wstringstream ss(normalized);
    std::wstring item;

    while (std::getline(ss, item, L';'))
    {
        item = ToLower(Trim(item));

        if (!item.empty())
            result.push_back(item);
    }

    return result;
}


static std::wstring WhitelistForUi(const std::wstring& whitelist)
{
    std::wstring s = whitelist;

    for (auto& ch : s)
    {
        if (ch == L';')
            ch = L'\n';
    }

    return s;
}


static std::wstring WhitelistForIni(const std::wstring& uiText)
{
    auto items = ParseWhitelist(uiText);

    std::wstring out;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i != 0)
            out += L";";

        out += items[i];
    }

    return out;
}


// ============================================================================
// Config path / INI
// ============================================================================

static std::wstring GetExePath()
{
    wchar_t path[32768]{};

    const DWORD len = GetModuleFileNameW(
        nullptr,
        path,
        static_cast<DWORD>(
            sizeof(path) / sizeof(path[0])));

    return std::wstring(path, len);
}


static std::wstring GetConfigPath()
{
    std::wstring exe = GetExePath();

    const size_t slash = exe.find_last_of(L"\\/");

    if (slash == std::wstring::npos)
        return Const::ConfigFileName;

    return exe.substr(0, slash + 1) +
           Const::ConfigFileName;
}


static int ReadIniInt(
    const wchar_t* section,
    const wchar_t* key,
    int defaultValue)
{
    return static_cast<int>(
        GetPrivateProfileIntW(
            section,
            key,
            defaultValue,
            g_configPath.c_str()));
}


static double ReadIniDouble(
    const wchar_t* section,
    const wchar_t* key,
    double defaultValue)
{
    wchar_t buffer[128]{};

    wchar_t def[64]{};
    swprintf_s(def, L"%.6f", defaultValue);

    GetPrivateProfileStringW(
        section,
        key,
        def,
        buffer,
        static_cast<DWORD>(
            sizeof(buffer) / sizeof(buffer[0])),
        g_configPath.c_str());

    wchar_t* end = nullptr;
    const double value = wcstod(buffer, &end);

    if (end == buffer)
        return defaultValue;

    return value;
}


static std::wstring ReadIniString(
    const wchar_t* section,
    const wchar_t* key,
    const wchar_t* defaultValue)
{
    wchar_t buffer[8192]{};

    GetPrivateProfileStringW(
        section,
        key,
        defaultValue,
        buffer,
        static_cast<DWORD>(
            sizeof(buffer) / sizeof(buffer[0])),
        g_configPath.c_str());

    return buffer;
}


static void WriteIniInt(
    const wchar_t* section,
    const wchar_t* key,
    int value)
{
    wchar_t buffer[64]{};

    swprintf_s(
        buffer,
        L"%d",
        value);

    WritePrivateProfileStringW(
        section,
        key,
        buffer,
        g_configPath.c_str());
}


static void WriteIniDouble(
    const wchar_t* section,
    const wchar_t* key,
    double value)
{
    wchar_t buffer[64]{};

    swprintf_s(
        buffer,
        L"%.6f",
        value);

    WritePrivateProfileStringW(
        section,
        key,
        buffer,
        g_configPath.c_str());
}


static bool IsRunAtStartup()
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,
            KEY_QUERY_VALUE,
            &key) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t value[32768]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);

    const LSTATUS status = RegQueryValueExW(
        key,
        Const::RunRegistryValue,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(value),
        &bytes);

    RegCloseKey(key);

    return status == ERROR_SUCCESS &&
           (type == REG_SZ || type == REG_EXPAND_SZ);
}


static bool SetRunAtStartup(bool enabled)
{
    HKEY key = nullptr;

    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;

    if (enabled)
    {
        std::wstring command =
            L"\"" + GetExePath() + L"\"";

        const DWORD bytes =
            static_cast<DWORD>(
                (command.size() + 1) *
                sizeof(wchar_t));

        status = RegSetValueExW(
            key,
            Const::RunRegistryValue,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(
                command.c_str()),
            bytes);
    }
    else
    {
        status = RegDeleteValueW(
            key,
            Const::RunRegistryValue);

        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }

    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}


static void LoadConfig()
{
    AppConfig cfg{};

    cfg.enabled =
        ReadIniInt(
            L"General",
            L10n(L"启用", L"Enabled"),
            1) != 0;

    cfg.sensitivityLog =
        std::clamp(
            ReadIniDouble(
                L"Zoom",
                L"SensitivityLog",
                0.070),
            0.020,
            0.180);

    cfg.curve =
        static_cast<SmoothingCurve>(
            std::clamp(
                ReadIniInt(
                    L"Zoom",
                    L"SmoothingCurve",
                    static_cast<int>(
                        SmoothingCurve::Adaptive)),
                0,
                3));

    cfg.zoomModifiers =
        static_cast<UINT>(
            ReadIniInt(
                L"Hotkeys",
                L"ZoomModifiers",
                MM_ALT));

    cfg.resetModifiers =
        static_cast<UINT>(
            ReadIniInt(
                L"Hotkeys",
                L"ResetModifiers",
                MM_ALT));

    cfg.hardResetModifiers =
        static_cast<UINT>(
            ReadIniInt(
                L"Hotkeys",
                L"HardResetModifiers",
                MM_ALT | MM_CTRL));

    cfg.exitVk =
        static_cast<UINT>(
            ReadIniInt(
                L"Hotkeys",
                L"ExitVk",
                VK_F12));

    cfg.exitMods =
        static_cast<UINT>(
            ReadIniInt(
                L"Hotkeys",
                L"ExitMods",
                MOD_CONTROL |
                MOD_ALT |
                MOD_SHIFT));

    cfg.whitelist =
        ReadIniString(
            L"Applications",
            L"Whitelist",
            L"chrome.exe;msedge.exe");

    cfg.startWithWindows =
        IsRunAtStartup();

    cfg.language =
        static_cast<UiLanguage>(
            std::clamp(
                ReadIniInt(
                    L"General",
                    L"Language",
                    static_cast<int>(
                        UiLanguage::Chinese)),
                0,
                1));

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg = cfg;
    }

    g_enabled.store(
        cfg.enabled,
        std::memory_order_relaxed);
}


static void SaveConfig()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    WriteIniInt(
        L"General",
        L10n(L"启用", L"Enabled"),
        cfg.enabled ? 1 : 0);

    WriteIniInt(
        L"General",
        L"Language",
        static_cast<int>(
            cfg.language));

    WriteIniDouble(
        L"Zoom",
        L"SensitivityLog",
        cfg.sensitivityLog);

    WriteIniInt(
        L"Zoom",
        L"SmoothingCurve",
        static_cast<int>(cfg.curve));

    WriteIniInt(
        L"Hotkeys",
        L"ZoomModifiers",
        static_cast<int>(
            cfg.zoomModifiers));

    WriteIniInt(
        L"Hotkeys",
        L"ResetModifiers",
        static_cast<int>(
            cfg.resetModifiers));

    WriteIniInt(
        L"Hotkeys",
        L"HardResetModifiers",
        static_cast<int>(
            cfg.hardResetModifiers));

    WriteIniInt(
        L"Hotkeys",
        L"ExitVk",
        static_cast<int>(
            cfg.exitVk));

    WriteIniInt(
        L"Hotkeys",
        L"ExitMods",
        static_cast<int>(
            cfg.exitMods));

    WritePrivateProfileStringW(
        L"Applications",
        L"Whitelist",
        cfg.whitelist.c_str(),
        g_configPath.c_str());

    SetRunAtStartup(
        cfg.startWithWindows);
}


// ============================================================================
// Foreground application whitelist
// ============================================================================

static bool IsAllowedProcess(DWORD pid)
{
    if (pid == 0)
        return false;

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        pid);

    if (!process)
        return false;

    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(
        sizeof(path) / sizeof(path[0]));

    const BOOL ok =
        QueryFullProcessImageNameW(
            process,
            0,
            path,
            &size);

    CloseHandle(process);

    if (!ok)
        return false;

    const std::wstring exe =
        ToLower(
            BaseName(
                std::wstring(
                    path,
                    size)));

    std::wstring whitelist;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        whitelist =
            g_cfg.whitelist;
    }

    const auto entries =
        ParseWhitelist(
            whitelist);

    return std::find(
        entries.begin(),
        entries.end(),
        exe) !=
        entries.end();
}


static void UpdateForegroundAllowed(HWND hwnd)
{
    if (!hwnd)
    {
        g_foregroundAllowed.store(
            false,
            std::memory_order_relaxed);

        return;
    }

    DWORD pid = 0;

    GetWindowThreadProcessId(
        hwnd,
        &pid);

    g_foregroundAllowed.store(
        IsAllowedProcess(pid),
        std::memory_order_relaxed);
}


static void CALLBACK ForegroundWinEventProc(
    HWINEVENTHOOK,
    DWORD,
    HWND hwnd,
    LONG,
    LONG,
    DWORD,
    DWORD)
{
    UpdateForegroundAllowed(hwnd);
}


// ============================================================================
// Touchpad creation
// ============================================================================

static bool ResolveCreateSyntheticPointerDevice2()
{
    if (g_CreateSyntheticPointerDevice2)
        return true;

    HMODULE user32 =
        GetModuleHandleW(
            L"user32.dll");

    if (!user32)
        return false;

    g_CreateSyntheticPointerDevice2 =
        reinterpret_cast<
            PFN_CreateSyntheticPointerDevice2>(
                GetProcAddress(
                    user32,
                    "CreateSyntheticPointerDevice2"));

    if (!g_CreateSyntheticPointerDevice2)
    {
        g_CreateSyntheticPointerDevice2 =
            reinterpret_cast<
                PFN_CreateSyntheticPointerDevice2>(
                    GetProcAddress(
                        user32,
                        MAKEINTRESOURCEA(2690)));
    }

    return
        g_CreateSyntheticPointerDevice2 !=
        nullptr;
}


static bool CreateTouchpadDevice()
{
    if (g_touchpad)
        return true;

    if (!ResolveCreateSyntheticPointerDevice2())
        return false;

    SyntheticDeviceCreationParamsCompat params{};

    params.pointerType = PT_TOUCHPAD;
    params.maxCount = 2;
    params.feedbackMode =
        POINTER_FEEDBACK_NONE;
    params.hMonitor = nullptr;
    params.deviceWidth =
        Const::TouchpadWidth;
    params.deviceHeight =
        Const::TouchpadHeight;
    params.options =
        Compat_SDCO_PHYSICAL_SIZE |
        Compat_SDCO_TOUCHPAD_GESTURE_ONLY;

    SetLastError(
        ERROR_SUCCESS);

    g_touchpad =
        g_CreateSyntheticPointerDevice2(
            &params);

    return g_touchpad != nullptr;
}


static void DestroyTouchpadDevice()
{
    if (!g_touchpad)
        return;

    DestroySyntheticPointerDevice(
        g_touchpad);

    g_touchpad = nullptr;
}


// ============================================================================
// Pinch state
// ============================================================================

struct PinchState
{
    bool active = false;
    bool softResetting = false;

    double targetLogZoom = 0.0;
    double appliedLogZoom = 0.0;

    double segmentBaseLogZoom = 0.0;
    double currentDistance =
        Const::BaseDistance;

    POINT lastP1{};
    POINT lastP2{};

    DWORD syntheticTime = 1;

    double lastWheelTime = 0.0;
    double lastTickTime = 0.0;
    double lastProgressTime = 0.0;
};

static PinchState g_pinch;


static void FingerPositionsForDistance(
    double distance,
    POINT& p1,
    POINT& p2)
{
    distance =
        std::clamp(
            distance,
            Const::MinDistance,
            Const::MaxDistance);

    const LONG half =
        static_cast<LONG>(
            std::lround(
                distance / 2.0));

    p1.x = Const::CenterX - half;
    p1.y = Const::CenterY;

    p2.x = Const::CenterX + half;
    p2.y = Const::CenterY;
}


static double MaxLogZoom()
{
    return std::log(
        Const::MaxLogicalScale);
}


static bool InjectTouchpadFrame(
    const POINT& p1,
    const POINT& p2,
    bool contactsActive)
{
    if (!g_touchpad)
        return false;

    POINTER_TYPE_INFO infos[2]{};
    const POINT points[2] =
        { p1, p2 };

    for (UINT32 i = 0;
         i < 2;
         ++i)
    {
        auto& info =
            infos[i];

        auto& pointerInfo =
            info.touchInfo.pointerInfo;

        info.type = PT_TOUCHPAD;

        pointerInfo.pointerId = i;

        pointerInfo.pointerFlags =
            contactsActive
            ? (POINTER_FLAG_INRANGE |
               POINTER_FLAG_INCONTACT |
               POINTER_FLAG_CONFIDENCE)
            : POINTER_FLAG_CONFIDENCE;

        pointerInfo.ptHimetricLocation =
            points[i];

        pointerInfo.dwTime =
            g_pinch.syntheticTime;
    }

    if (!InjectSyntheticPointerInput(
            g_touchpad,
            infos,
            2))
    {
        return false;
    }

    g_pinch.syntheticTime +=
        Const::AnimationIntervalMs;

    return true;
}


static void ClearActiveSegmentOnly()
{
    g_pinch.active = false;

    g_pinch.currentDistance =
        Const::BaseDistance;
}


static bool EndActiveSegment()
{
    if (!g_pinch.active)
        return true;

    if (!InjectTouchpadFrame(
            g_pinch.lastP1,
            g_pinch.lastP2,
            false))
    {
        return false;
    }

    ClearActiveSegmentOnly();

    return true;
}


static bool BeginSegment()
{
    if (g_pinch.active)
        return true;

    if (!g_touchpad &&
        !CreateTouchpadDevice())
    {
        return false;
    }

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(
        Const::BaseDistance,
        p1,
        p2);

    g_pinch.segmentBaseLogZoom =
        g_pinch.appliedLogZoom;

    g_pinch.currentDistance =
        Const::BaseDistance;

    if (!InjectTouchpadFrame(
            p1,
            p2,
            true))
    {
        return false;
    }

    g_pinch.lastP1 = p1;
    g_pinch.lastP2 = p2;
    g_pinch.active = true;

    const double now =
        NowSeconds();

    g_pinch.lastTickTime = now;
    g_pinch.lastProgressTime = now;

    return true;
}


// ============================================================================
// Reset
// ============================================================================

static void RequestSoftReset()
{
    g_pinch.targetLogZoom = 0.0;
    g_pinch.softResetting = true;

    if (g_pinch.appliedLogZoom <=
        Const::LogZoomEpsilon)
    {
        g_pinch.appliedLogZoom = 0.0;
        g_pinch.targetLogZoom = 0.0;

        if (g_pinch.active)
            EndActiveSegment();

        g_pinch.softResetting = false;
    }
}


static bool HardResetDevice()
{
    if (g_pinch.active)
        EndActiveSegment();

    DestroyTouchpadDevice();

    g_pinch = PinchState{};

    return CreateTouchpadDevice();
}


// ============================================================================
// Wheel input
// ============================================================================

static void ApplyWheelDelta(short wheelDelta)
{
    if (wheelDelta == 0 ||
        !g_enabled.load(
            std::memory_order_relaxed))
    {
        return;
    }

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    double notches =
        static_cast<double>(
            wheelDelta) /
        static_cast<double>(
            WHEEL_DELTA);

    notches =
        std::clamp(
            notches,
            -Const::MaxAbsNotchesPerEvent,
            Const::MaxAbsNotchesPerEvent);

    const double oldTarget =
        g_pinch.targetLogZoom;

    g_pinch.targetLogZoom +=
        cfg.sensitivityLog *
        notches;

    g_pinch.targetLogZoom =
        std::clamp(
            g_pinch.targetLogZoom,
            0.0,
            MaxLogZoom());

    g_pinch.softResetting = false;
    g_pinch.lastWheelTime =
        NowSeconds();

    if (std::abs(
            g_pinch.targetLogZoom -
            oldTarget) <=
            Const::LogZoomEpsilon &&
        g_pinch.targetLogZoom <=
            Const::LogZoomEpsilon &&
        notches < 0.0)
    {
        return;
    }

    if (std::abs(
            g_pinch.targetLogZoom -
            g_pinch.appliedLogZoom) >
        Const::LogZoomEpsilon)
    {
        BeginSegment();
    }
}


// ============================================================================
// Smoothing
// ============================================================================

static double CalculateSmoothedDistance(
    double current,
    double desired,
    double dt,
    bool resetting,
    SmoothingCurve curve)
{
    const double diff =
        desired - current;

    if (std::abs(diff) <=
        Const::DistanceEpsilon)
    {
        return desired;
    }

    if (curve ==
        SmoothingCurve::Direct)
    {
        return desired;
    }

    const double speedLimit =
        resetting
        ? Const::ResetFingerSpeed
        : Const::MaxFingerSpeed;

    if (curve ==
        SmoothingCurve::Linear)
    {
        const double speed =
            resetting
            ? Const::ResetFingerSpeed
            : Const::LinearFingerSpeed;

        const double step =
            std::clamp(
                diff,
                -speed * dt,
                speed * dt);

        return current + step;
    }

    double rate =
        Const::ExponentialRate;

    if (curve ==
        SmoothingCurve::Adaptive)
    {
        const double normalized =
            std::clamp(
                std::abs(diff) /
                    Const::AdaptiveFullDiff,
                0.0,
                1.0);

        rate =
            Const::AdaptiveRateNear +
            (Const::AdaptiveRateFar -
             Const::AdaptiveRateNear) *
            normalized;
    }

    const double alpha =
        1.0 -
        std::exp(
            -rate * dt);

    double step =
        diff * alpha;

    step =
        std::clamp(
            step,
            -speedLimit * dt,
            speedLimit * dt);

    double result =
        current + step;

    if ((diff > 0.0 &&
         result > desired) ||
        (diff < 0.0 &&
         result < desired))
    {
        result = desired;
    }

    return result;
}



// ============================================================================
// Current modifier state (also used by the animation state machine)
// ============================================================================

static UINT CurrentModifierMaskForState()
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


static bool ZoomModifierStillHeld()
{
    UINT expected = MM_ALT;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);
        expected = g_cfg.zoomModifiers;
    }

    return CurrentModifierMaskForState() == expected;
}


// ============================================================================
// Animation
// ============================================================================

static void RecoverAfterInjectionFailure()
{
    const double preserveApplied =
        std::max(
            0.0,
            g_pinch.appliedLogZoom);

    const double preserveTarget =
        std::max(
            0.0,
            g_pinch.targetLogZoom);

    const bool preserveSoftReset =
        g_pinch.softResetting;

    DestroyTouchpadDevice();

    PinchState recovered{};

    recovered.appliedLogZoom =
        preserveApplied;
    recovered.targetLogZoom =
        preserveTarget;
    recovered.softResetting =
        preserveSoftReset;

    g_pinch = recovered;

    CreateTouchpadDevice();
}


static void TickPinch()
{
    const double now =
        NowSeconds();

    if (!g_enabled.load(
            std::memory_order_relaxed))
    {
        if (g_pinch.active)
            EndActiveSegment();

        return;
    }

    if (!g_pinch.active &&
        std::abs(
            g_pinch.targetLogZoom -
            g_pinch.appliedLogZoom) <=
            Const::LogZoomEpsilon)
    {
        if (g_pinch.softResetting)
        {
            g_pinch.targetLogZoom = 0.0;
            g_pinch.appliedLogZoom = 0.0;
            g_pinch.softResetting = false;
        }

        return;
    }

    if (!g_pinch.active)
    {
        if (!BeginSegment())
            return;
    }

    double dt =
        now -
        g_pinch.lastTickTime;

    g_pinch.lastTickTime = now;

    dt =
        std::clamp(
            dt,
            0.0,
            0.050);

    const double desiredSegmentLogDelta =
        g_pinch.targetLogZoom -
        g_pinch.segmentBaseLogZoom;

    const double desiredDistance =
        std::clamp(
            Const::BaseDistance *
                std::exp(
                    desiredSegmentLogDelta),
            Const::MinDistance,
            Const::MaxDistance);

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    const double newDistance =
        CalculateSmoothedDistance(
            g_pinch.currentDistance,
            desiredDistance,
            dt,
            g_pinch.softResetting,
            cfg.curve);

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(
        newDistance,
        p1,
        p2);

    const bool moved =
        p1.x != g_pinch.lastP1.x ||
        p1.y != g_pinch.lastP1.y ||
        p2.x != g_pinch.lastP2.x ||
        p2.y != g_pinch.lastP2.y;

    if (moved)
    {
        if (!InjectTouchpadFrame(
                p1,
                p2,
                true))
        {
            RecoverAfterInjectionFailure();
            return;
        }

        g_pinch.currentDistance =
            newDistance;

        g_pinch.lastP1 = p1;
        g_pinch.lastP2 = p2;

        const double segmentLog =
            std::log(
                g_pinch.currentDistance /
                Const::BaseDistance);

        g_pinch.appliedLogZoom =
            std::max(
                0.0,
                g_pinch.segmentBaseLogZoom +
                segmentLog);

        g_pinch.lastProgressTime =
            now;
    }
    else
    {
        g_pinch.currentDistance =
            newDistance;
    }

    const bool targetReached =
        std::abs(
            g_pinch.targetLogZoom -
            g_pinch.appliedLogZoom) <=
            Const::LogZoomEpsilon;

    const bool atUpperLimit =
        g_pinch.currentDistance >=
        (Const::MaxDistance -
         Const::DistanceEpsilon);

    const bool atLowerLimit =
        g_pinch.currentDistance <=
        (Const::MinDistance +
         Const::DistanceEpsilon);

    const bool stillNeedsIn =
        g_pinch.targetLogZoom >
        g_pinch.appliedLogZoom +
        Const::LogZoomEpsilon;

    const bool stillNeedsOut =
        g_pinch.targetLogZoom <
        g_pinch.appliedLogZoom -
        Const::LogZoomEpsilon;

    if ((atUpperLimit &&
         stillNeedsIn) ||
        (atLowerLimit &&
         stillNeedsOut))
    {
        EndActiveSegment();
        return;
    }

    if (targetReached)
    {
        g_pinch.appliedLogZoom =
            g_pinch.targetLogZoom;

        const bool zoomModifierHeld =
            ZoomModifierStillHeld();

        const bool foregroundStillAllowed =
            g_foregroundAllowed.load(
                std::memory_order_relaxed);

        // V3.1 slow-wheel behavior:
        //
        // While the configured zoom modifier is still held, keep the two
        // synthetic touchpad contacts DOWN even if there has been no wheel
        // event for a long time. Therefore:
        //
        //   hold Alt
        //   wheel one notch
        //   wait 500 ms / 1 s
        //   wheel another notch
        //
        // remains ONE continuous precision-touchpad pinch session.
        //
        // We release when:
        //   - soft reset has completed, or
        //   - the zoom modifier is released/changed, or
        //   - focus leaves the whitelist.
        if (g_pinch.softResetting ||
            !zoomModifierHeld ||
            !foregroundStillAllowed)
        {
            EndActiveSegment();

            if (g_pinch.targetLogZoom <=
                Const::LogZoomEpsilon)
            {
                g_pinch.targetLogZoom = 0.0;
                g_pinch.appliedLogZoom = 0.0;
                g_pinch.softResetting = false;
            }

            return;
        }
    }

    // Watchdog is only a fallback for a genuinely orphaned gesture. Do not
    // interrupt a gesture the user is intentionally holding with Alt.
    if (g_pinch.active &&
        !ZoomModifierStillHeld() &&
        (now -
         g_pinch.lastProgressTime) >
        Const::WatchdogSec)
    {
        EndActiveSegment();
    }
}


// ============================================================================
// Modifier handling
// ============================================================================

static UINT CurrentModifierMask()
{
    return CurrentModifierMaskForState();
}


static bool ExactModifiersDown(
    UINT expected)
{
    return CurrentModifierMask() ==
           expected;
}


// ============================================================================
// Mouse hook
// ============================================================================

static LRESULT CALLBACK LowLevelMouseProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    if (nCode < 0)
    {
        return CallNextHookEx(
            g_mouseHook,
            nCode,
            wParam,
            lParam);
    }

    const auto* info =
        reinterpret_cast<
            const MSLLHOOKSTRUCT*>(
                lParam);

    if (nCode == HC_ACTION &&
        info &&
        (info->flags &
         LLMHF_INJECTED) == 0 &&
        g_enabled.load(
            std::memory_order_relaxed) &&
        g_foregroundAllowed.load(
            std::memory_order_relaxed))
    {
        AppConfig cfg;

        {
            std::lock_guard<std::mutex> lock(
                g_cfgMutex);

            cfg = g_cfg;
        }

        if (wParam == WM_MOUSEWHEEL &&
            ExactModifiersDown(
                cfg.zoomModifiers))
        {
            const short delta =
                GET_WHEEL_DELTA_WPARAM(
                    info->mouseData);

            PostMessageW(
                g_mainWindow,
                Const::WM_APP_WHEEL,
                static_cast<WPARAM>(
                    static_cast<USHORT>(
                        delta)),
                0);

            return 1;
        }

        if (wParam ==
            WM_MBUTTONDOWN)
        {
            const UINT current =
                CurrentModifierMask();

            if (current ==
                cfg.hardResetModifiers)
            {
                g_swallowMiddleUp.store(
                    true,
                    std::memory_order_relaxed);

                PostMessageW(
                    g_mainWindow,
                    Const::WM_APP_HARD_RESET,
                    0,
                    0);

                return 1;
            }

            if (current ==
                cfg.resetModifiers)
            {
                g_swallowMiddleUp.store(
                    true,
                    std::memory_order_relaxed);

                PostMessageW(
                    g_mainWindow,
                    Const::WM_APP_SOFT_RESET,
                    0,
                    0);

                return 1;
            }
        }

        if (wParam ==
            WM_MBUTTONUP &&
            g_swallowMiddleUp.exchange(
                false,
                std::memory_order_relaxed))
        {
            return 1;
        }
    }

    return CallNextHookEx(
        g_mouseHook,
        nCode,
        wParam,
        lParam);
}


// ============================================================================
// Exit hotkey conversions
// ============================================================================

static UINT HotKeyControlFlagsToMod(
    BYTE flags)
{
    UINT mods = 0;

    if (flags & HOTKEYF_CONTROL)
        mods |= MOD_CONTROL;

    if (flags & HOTKEYF_ALT)
        mods |= MOD_ALT;

    if (flags & HOTKEYF_SHIFT)
        mods |= MOD_SHIFT;

    return mods;
}


static BYTE ModToHotKeyControlFlags(
    UINT mods)
{
    BYTE flags = 0;

    if (mods & MOD_CONTROL)
        flags |= HOTKEYF_CONTROL;

    if (mods & MOD_ALT)
        flags |= HOTKEYF_ALT;

    if (mods & MOD_SHIFT)
        flags |= HOTKEYF_SHIFT;

    return flags;
}


static bool RegisterExitHotkey()
{
    UnregisterHotKey(
        g_mainWindow,
        Const::ExitHotkeyId);

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    if (cfg.exitVk == 0)
        return true;

    return RegisterHotKey(
        g_mainWindow,
        Const::ExitHotkeyId,
        cfg.exitMods |
        MOD_NOREPEAT,
        cfg.exitVk) != FALSE;
}



// ============================================================================
// Tray
// ============================================================================

static void UpdateTrayTooltip()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    const wchar_t* state =
        cfg.enabled
        ? (cfg.language == UiLanguage::Chinese ? L"已启用" : L10n(L"启用", L"Enabled"))
        : (cfg.language == UiLanguage::Chinese ? L"已禁用" : L"Disabled");

    swprintf_s(
        g_nid.szTip,
        L"Continuous Pinch V3 - %s",
        state);

    if (g_nid.hWnd)
    {
        Shell_NotifyIconW(
            NIM_MODIFY,
            &g_nid);
    }
}


static bool AddTrayIcon()
{
    ZeroMemory(
        &g_nid,
        sizeof(g_nid));

    g_nid.cbSize =
        sizeof(g_nid);

    g_nid.hWnd =
        g_mainWindow;

    g_nid.uID =
        Const::TrayIconId;

    g_nid.uFlags =
        NIF_MESSAGE |
        NIF_ICON |
        NIF_TIP;

    g_nid.uCallbackMessage =
        Const::WM_TRAY;

	g_nid.hIcon =
		LoadIconW(
			GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(
				IDI_APP_ICON));

    wcscpy_s(
        g_nid.szTip,
        L"Continuous Pinch V3");

    return Shell_NotifyIconW(
        NIM_ADD,
        &g_nid) != FALSE;
}


static void RemoveTrayIcon()
{
    if (g_nid.hWnd)
    {
        Shell_NotifyIconW(
            NIM_DELETE,
            &g_nid);

        g_nid.hWnd = nullptr;
    }
}


static void ShowTrayMenu()
{
    HMENU menu =
        CreatePopupMenu();

    if (!menu)
        return;

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    AppendMenuW(
        menu,
        MF_STRING |
        (cfg.enabled
            ? MF_CHECKED
            : MF_UNCHECKED),
        Const::IDM_TRAY_ENABLE,
        L10n(L"启用", L"Enabled"));

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        Const::IDM_TRAY_SETTINGS,
        L10n(L"设置...", L"Settings..."));

    AppendMenuW(
        menu,
        MF_STRING,
        Const::IDM_TRAY_RESET,
        L10n(L"回到 100% 基准", L"Return to 100% baseline"));

    AppendMenuW(
        menu,
        MF_STRING,
        Const::IDM_TRAY_HARDRESET,
        L10n(L"硬复位触控板", L"Hard reset touchpad"));

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr);

    AppendMenuW(
        menu,
        MF_STRING |
        (cfg.startWithWindows
            ? MF_CHECKED
            : MF_UNCHECKED),
        Const::IDM_TRAY_STARTUP,
        L10n(L"开机启动", L"Start with Windows"));

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        Const::IDM_TRAY_EXIT,
        L10n(L"退出", L"Exit"));

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(
        g_mainWindow);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON |
        TPM_BOTTOMALIGN,
        pt.x,
        pt.y,
        0,
        g_mainWindow,
        nullptr);

    DestroyMenu(
        menu);
}


// ============================================================================
// Settings UI helpers
// ============================================================================

static HFONT GetDefaultGuiFont()
{
    return static_cast<HFONT>(
        GetStockObject(
            DEFAULT_GUI_FONT));
}


static void SetControlFont(HWND hwnd)
{
    SendMessageW(
        hwnd,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            GetDefaultGuiFont()),
        TRUE);
}


static HWND AddStatic(
    HWND parent,
    const wchar_t* text,
    int x,
    int y,
    int w,
    int h)
{
    HWND hwnd =
        CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD |
            WS_VISIBLE,
            x,
            y,
            w,
            h,
            parent,
            nullptr,
            GetModuleHandleW(
                nullptr),
            nullptr);

    SetControlFont(hwnd);

    return hwnd;
}


static HWND AddCombo(
    HWND parent,
    int id,
    int x,
    int y,
    int w,
    int h)
{
    HWND hwnd =
        CreateWindowExW(
            0,
            L"COMBOBOX",
            L"",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            CBS_DROPDOWNLIST |
            WS_VSCROLL,
            x,
            y,
            w,
            h,
            parent,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    id)),
            GetModuleHandleW(
                nullptr),
            nullptr);

    SetControlFont(hwnd);

    return hwnd;
}


static void FillModifierCombo(
    HWND combo,
    UINT selectedMask)
{
    struct Item
    {
        const wchar_t* text;
        UINT mask;
    };

    const Item items[] =
    {
        { L"Alt", MM_ALT },
        { L"Ctrl", MM_CTRL },
        { L"Shift", MM_SHIFT },
        { L"Ctrl + Alt", MM_CTRL | MM_ALT },
        { L"Ctrl + Shift", MM_CTRL | MM_SHIFT },
        { L"Alt + Shift", MM_ALT | MM_SHIFT },
        { L"Ctrl + Alt + Shift", MM_CTRL | MM_ALT | MM_SHIFT }
    };

    SendMessageW(
        combo,
        CB_RESETCONTENT,
        0,
        0);

    int selectIndex = 0;

    for (int i = 0;
         i <
         static_cast<int>(
             sizeof(items) /
             sizeof(items[0]));
         ++i)
    {
        const LRESULT index =
            SendMessageW(
                combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    items[i].text));

        SendMessageW(
            combo,
            CB_SETITEMDATA,
            index,
            items[i].mask);

        if (items[i].mask ==
            selectedMask)
        {
            selectIndex =
                static_cast<int>(
                    index);
        }
    }

    SendMessageW(
        combo,
        CB_SETCURSEL,
        selectIndex,
        0);
}


static UINT GetModifierComboValue(
    HWND combo)
{
    const LRESULT index =
        SendMessageW(
            combo,
            CB_GETCURSEL,
            0,
            0);

    if (index ==
        CB_ERR)
    {
        return MM_ALT;
    }

    const LRESULT data =
        SendMessageW(
            combo,
            CB_GETITEMDATA,
            index,
            0);

    return static_cast<UINT>(
        data);
}


static void UpdateSensitivityLabel(
    HWND settings)
{
    HWND slider =
        GetDlgItem(
            settings,
            Const::IDC_SENS_SLIDER);

    HWND label =
        GetDlgItem(
            settings,
            Const::IDC_SENS_VALUE);

    const int pos =
        static_cast<int>(
            SendMessageW(
                slider,
                TBM_GETPOS,
                0,
                0));

    const double logValue =
        static_cast<double>(
            pos) /
        1000.0;

    const double percent =
        (std::exp(
             logValue) -
         1.0) *
        100.0;

    wchar_t text[128]{};

    swprintf_s(
        text,
        L"%.1f%% per wheel notch",
        percent);

    SetWindowTextW(
        label,
        text);

    // Real-time: sensitivity changes immediately while the slider moves.
    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg.sensitivityLog =
            logValue;
    }
}


static void UpdateCurveRealtime(
    HWND settings)
{
    HWND combo =
        GetDlgItem(
            settings,
            Const::IDC_CURVE);

    const LRESULT index =
        SendMessageW(
            combo,
            CB_GETCURSEL,
            0,
            0);

    if (index >= 0 &&
        index <= 3)
    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg.curve =
            static_cast<
                SmoothingCurve>(
                    index);
    }
}



static void ApplySettingsLanguage(HWND settings)
{
    const bool zh =
        CurrentLanguage() == UiLanguage::Chinese;

    SetWindowTextW(
        settings,
        zh
            ? L"Continuous Pinch V3 设置"
            : L"Continuous Pinch V3 设置");

    SetWindowTextW(
        GetDlgItem(settings, Const::IDC_ENABLED),
        zh
            ? L"启用捏合缩放"
            : L"Enable pinch zoom");

    SetWindowTextW(
        GetDlgItem(settings, Const::IDC_STARTUP),
        zh
            ? L"开机启动"
            : L"Start with Windows");

    SetWindowTextW(
        GetDlgItem(settings, Const::IDC_DEFAULTS),
        zh ? L"恢复默认" : L"Defaults");

    SetWindowTextW(
        GetDlgItem(settings, Const::IDC_APPLY),
        zh ? L"应用" : L"Apply");

    SetWindowTextW(
        GetDlgItem(settings, Const::IDC_CLOSE),
        zh ? L"关闭" : L"Close");

    HWND curve = GetDlgItem(settings, Const::IDC_CURVE);
    const LRESULT currentCurve =
        SendMessageW(curve, CB_GETCURSEL, 0, 0);

    SendMessageW(curve, CB_RESETCONTENT, 0, 0);
    SendMessageW(curve, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(zh ? L"直接 / 即时" : L"Direct / immediate"));
    SendMessageW(curve, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(zh ? L"线性" : L"Linear"));
    SendMessageW(curve, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(zh ? L"指数" : L"Exponential"));
    SendMessageW(curve, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(zh ? L"自适应（推荐）" : L"Adaptive (recommended)"));

    if (currentCurve >= 0 && currentCurve <= 3)
        SendMessageW(curve, CB_SETCURSEL, currentCurve, 0);

    HWND language = GetDlgItem(settings, Const::IDC_LANGUAGE);
    if (language)
    {
        SendMessageW(language, CB_RESETCONTENT, 0, 0);
        SendMessageW(language, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"中文"));
        SendMessageW(language, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(language, CB_SETCURSEL,
            CurrentLanguage() == UiLanguage::Chinese ? 0 : 1, 0);
    }
}

static void PopulateSettingsFromConfig(
    HWND settings)
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        cfg = g_cfg;
    }

    SendMessageW(
        GetDlgItem(
            settings,
            Const::IDC_ENABLED),
        BM_SETCHECK,
        cfg.enabled
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);

    HWND slider =
        GetDlgItem(
            settings,
            Const::IDC_SENS_SLIDER);

    SendMessageW(
        slider,
        TBM_SETRANGE,
        TRUE,
        MAKELPARAM(
            20,
            180));

    SendMessageW(
        slider,
        TBM_SETTICFREQ,
        10,
        0);

    SendMessageW(
        slider,
        TBM_SETPOS,
        TRUE,
        static_cast<LPARAM>(
            std::lround(
                cfg.sensitivityLog *
                1000.0)));

    UpdateSensitivityLabel(
        settings);

    HWND curve =
        GetDlgItem(
            settings,
            Const::IDC_CURVE);

    SendMessageW(
        curve,
        CB_RESETCONTENT,
        0,
        0);

    SendMessageW(
        curve,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Direct / immediate"));

    SendMessageW(
        curve,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Linear"));

    SendMessageW(
        curve,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Exponential"));

    SendMessageW(
        curve,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Adaptive (recommended)"));

    SendMessageW(
        curve,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            cfg.curve),
        0);

    const std::wstring whitelistUi =
        WhitelistForUi(
            cfg.whitelist);

    SetWindowTextW(
        GetDlgItem(
            settings,
            Const::IDC_WHITELIST),
        whitelistUi.c_str());

    FillModifierCombo(
        GetDlgItem(
            settings,
            Const::IDC_ZOOM_MOD),
        cfg.zoomModifiers);

    FillModifierCombo(
        GetDlgItem(
            settings,
            Const::IDC_RESET_MOD),
        cfg.resetModifiers);

    FillModifierCombo(
        GetDlgItem(
            settings,
            Const::IDC_HARDRESET_MOD),
        cfg.hardResetModifiers);

    const WORD hotkeyWord =
        MAKEWORD(
            static_cast<BYTE>(
                cfg.exitVk),
            ModToHotKeyControlFlags(
                cfg.exitMods));

    SendMessageW(
        GetDlgItem(
            settings,
            Const::IDC_EXIT_HOTKEY),
        HKM_SETHOTKEY,
        hotkeyWord,
        0);

    SendMessageW(
        GetDlgItem(
            settings,
            Const::IDC_STARTUP),
        BM_SETCHECK,
        cfg.startWithWindows
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);

    SendMessageW(
        GetDlgItem(
            settings,
            Const::IDC_LANGUAGE),
        CB_SETCURSEL,
        cfg.language == UiLanguage::Chinese ? 0 : 1,
        0);

    ApplySettingsLanguage(settings);
}


static bool ApplySettingsFromWindow(
    HWND settings,
    bool showErrors)
{
    AppConfig newCfg;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        newCfg = g_cfg;
    }

    newCfg.enabled =
        SendMessageW(
            GetDlgItem(
                settings,
                Const::IDC_ENABLED),
            BM_GETCHECK,
            0,
            0) ==
        BST_CHECKED;

    const int sensPos =
        static_cast<int>(
            SendMessageW(
                GetDlgItem(
                    settings,
                    Const::IDC_SENS_SLIDER),
                TBM_GETPOS,
                0,
                0));

    newCfg.sensitivityLog =
        std::clamp(
            static_cast<double>(
                sensPos) /
                1000.0,
            0.020,
            0.180);

    const LRESULT curveIndex =
        SendMessageW(
            GetDlgItem(
                settings,
                Const::IDC_CURVE),
            CB_GETCURSEL,
            0,
            0);

    if (curveIndex >= 0 &&
        curveIndex <= 3)
    {
        newCfg.curve =
            static_cast<
                SmoothingCurve>(
                    curveIndex);
    }

    newCfg.zoomModifiers =
        GetModifierComboValue(
            GetDlgItem(
                settings,
                Const::IDC_ZOOM_MOD));

    newCfg.resetModifiers =
        GetModifierComboValue(
            GetDlgItem(
                settings,
                Const::IDC_RESET_MOD));

    newCfg.hardResetModifiers =
        GetModifierComboValue(
            GetDlgItem(
                settings,
                Const::IDC_HARDRESET_MOD));

    const WORD hotkey =
        static_cast<WORD>(
            SendMessageW(
                GetDlgItem(
                    settings,
                    Const::IDC_EXIT_HOTKEY),
                HKM_GETHOTKEY,
                0,
                0));

    newCfg.exitVk =
        LOBYTE(
            hotkey);

    newCfg.exitMods =
        HotKeyControlFlagsToMod(
            HIBYTE(
                hotkey));

    newCfg.startWithWindows =
        SendMessageW(
            GetDlgItem(
                settings,
                Const::IDC_STARTUP),
            BM_GETCHECK,
            0,
            0) ==
        BST_CHECKED;

    const LRESULT languageIndex =
        SendMessageW(
            GetDlgItem(
                settings,
                Const::IDC_LANGUAGE),
            CB_GETCURSEL,
            0,
            0);

    newCfg.language =
        languageIndex == 1
            ? UiLanguage::English
            : UiLanguage::Chinese;

    HWND whitelistEdit =
        GetDlgItem(
            settings,
            Const::IDC_WHITELIST);

    const int len =
        GetWindowTextLengthW(
            whitelistEdit);

    std::wstring whitelistUi;

    if (len > 0)
    {
        std::vector<wchar_t> buffer(
            static_cast<size_t>(len) + 1,
            L'\0');

        GetWindowTextW(
            whitelistEdit,
            buffer.data(),
            len + 1);

        whitelistUi.assign(
            buffer.data());
    }

    newCfg.whitelist =
        WhitelistForIni(
            whitelistUi);

    if (newCfg.whitelist.empty())
    {
        if (showErrors)
        {
            MessageBoxW(
                settings,
                L10n(L"应用白名单不能为空。", L"The application whitelist cannot be empty."),
                Const::AppName,
                MB_OK |
                MB_ICONWARNING);
        }

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg = newCfg;
    }

    g_enabled.store(
        newCfg.enabled,
        std::memory_order_relaxed);

    if (!newCfg.enabled &&
        g_pinch.active)
    {
        EndActiveSegment();
    }

    SaveConfig();

    UpdateForegroundAllowed(
        GetForegroundWindow());

    const bool hotkeyOk =
        RegisterExitHotkey();

    UpdateTrayTooltip();
    ApplySettingsLanguage(settings);

    if (!hotkeyOk &&
        showErrors)
    {
        MessageBoxW(
            settings,
            L10n(L"所选退出热键无法注册。\n可能已被其他程序占用。", L"The selected Exit hotkey could not be registered.\nAnother application may already be using it."),
            Const::AppName,
            MB_OK |
            MB_ICONWARNING);
    }

    return true;
}


static void ResetSettingsToDefaults(
    HWND settings)
{
    AppConfig defaults{};

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        const bool currentStartup =
            g_cfg.startWithWindows;

        g_cfg = defaults;

        // Defaults button should not silently change startup until Apply.
        g_cfg.startWithWindows =
            currentStartup;
    }

    PopulateSettingsFromConfig(
        settings);
}


// ============================================================================
// Settings window
// ============================================================================

static LRESULT CALLBACK SettingsWndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            AddStatic(
                hwnd,
                L"Continuous Pinch V3",
                20, 15, 520, 24);

            AddStatic(
                hwnd,
                L"语言 / Language",
                400, 18, 100, 22);

            HWND languageCombo =
                AddCombo(
                    hwnd,
                    Const::IDC_LANGUAGE,
                    500, 12, 90, 120);

            SendMessageW(
                languageCombo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"中文"));

            SendMessageW(
                languageCombo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"English"));

            HWND enabled =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Enable pinch zoom",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                    20, 48, 200, 24,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_ENABLED)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(enabled);

            AddStatic(
                hwnd,
                L"灵敏度 / Sensitivity",
                20, 85, 100, 22);

            HWND slider =
                CreateWindowExW(
                    0,
                    TRACKBAR_CLASSW,
                    L"",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    TBS_AUTOTICKS,
                    120, 78, 300, 38,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_SENS_SLIDER)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(slider);

            // Explicit value label for the live sensitivity readout.
            HWND sensValue =
                CreateWindowExW(
                    0,
                    L"STATIC",
                    L"",
                    WS_CHILD |
                    WS_VISIBLE,
                    430, 85, 155, 22,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_SENS_VALUE)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(sensValue);

            AddStatic(
                hwnd,
                L"平滑曲线 / Smoothing",
                20, 126, 100, 22);

            AddCombo(
                hwnd,
                Const::IDC_CURVE,
                120, 120, 300, 180);

            AddStatic(
                hwnd,
                L"缩放修饰键 / Zoom",
                20, 166, 100, 22);

            AddCombo(
                hwnd,
                Const::IDC_ZOOM_MOD,
                120, 160, 220, 180);

            AddStatic(
                hwnd,
                L"软复位 / Soft reset",
                20, 206, 100, 22);

            AddCombo(
                hwnd,
                Const::IDC_RESET_MOD,
                120, 200, 220, 180);

            AddStatic(
                hwnd,
                L"硬复位 / Hard reset",
                20, 246, 100, 22);

            AddCombo(
                hwnd,
                Const::IDC_HARDRESET_MOD,
                120, 240, 220, 180);

            AddStatic(
                hwnd,
                L"（修饰键 + 鼠标中键）",
                355, 206, 220, 60);

            AddStatic(
                hwnd,
                L"退出热键 / Exit hotkey",
                20, 286, 100, 22);

            HWND hotkey =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    HOTKEY_CLASSW,
                    L"",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP,
                    120, 280, 220, 26,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_EXIT_HOTKEY)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(hotkey);

            AddStatic(
                hwnd,
                L"应用白名单（每行一个 exe）",
                20, 326, 420, 22);

            HWND whitelist =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    L"",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    WS_VSCROLL |
                    ES_MULTILINE |
                    ES_AUTOVSCROLL |
                    ES_WANTRETURN,
                    20, 350, 555, 120,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_WHITELIST)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(whitelist);

            HWND startup =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L10n(L"开机启动", L"Start with Windows"),
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                    20, 485, 200, 24,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_STARTUP)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(startup);

            HWND defaults =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Defaults",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_PUSHBUTTON,
                    250, 520, 95, 30,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_DEFAULTS)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(defaults);

            HWND apply =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Apply",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_DEFPUSHBUTTON,
                    355, 520, 95, 30,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_APPLY)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(apply);

            HWND close =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Close",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_PUSHBUTTON,
                    460, 520, 95, 30,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_CLOSE)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);

            SetControlFont(close);

            PopulateSettingsFromConfig(
                hwnd);

            return 0;
        }

        case WM_HSCROLL:
        {
            HWND source =
                reinterpret_cast<HWND>(
                    lParam);

            if (source ==
                GetDlgItem(
                    hwnd,
                    Const::IDC_SENS_SLIDER))
            {
                UpdateSensitivityLabel(
                    hwnd);
            }

            return 0;
        }

        case WM_COMMAND:
        {
            const int id =
                LOWORD(
                    wParam);

            const int notify =
                HIWORD(
                    wParam);

            if (id ==
                    Const::IDC_CURVE &&
                notify ==
                    CBN_SELCHANGE)
            {
                UpdateCurveRealtime(
                    hwnd);

                return 0;
            }

            if (id ==
                    Const::IDC_LANGUAGE &&
                notify ==
                    CBN_SELCHANGE)
            {
                const LRESULT index =
                    SendMessageW(
                        GetDlgItem(
                            hwnd,
                            Const::IDC_LANGUAGE),
                        CB_GETCURSEL,
                        0,
                        0);

                {
                    std::lock_guard<std::mutex> lock(
                        g_cfgMutex);

                    g_cfg.language =
                        index == 1
                            ? UiLanguage::English
                            : UiLanguage::Chinese;
                }

                ApplySettingsLanguage(
                    hwnd);

                UpdateTrayTooltip();

                return 0;
            }

            if (id ==
                Const::IDC_DEFAULTS)
            {
                ResetSettingsToDefaults(
                    hwnd);

                return 0;
            }

            if (id ==
                Const::IDC_APPLY)
            {
                ApplySettingsFromWindow(
                    hwnd,
                    true);

                return 0;
            }

            if (id ==
                Const::IDC_CLOSE)
            {
                if (ApplySettingsFromWindow(
                        hwnd,
                        true))
                {
                    ShowWindow(
                        hwnd,
                        SW_HIDE);
                }

                return 0;
            }

            break;
        }

        case WM_CLOSE:
        {
            ShowWindow(
                hwnd,
                SW_HIDE);

            return 0;
        }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam);
}


static bool CreateSettingsWindow(
    HINSTANCE hInstance)
{
    const wchar_t* className =
        L"ContinuousPinchV3SettingsWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc =
        SettingsWndProc;
    wc.hInstance =
        hInstance;
	wc.hIcon = LoadIconW(
		hInstance,
		MAKEINTRESOURCEW(IDI_APP_ICON));
	wc.hIconSm = LoadIconW(
		hInstance,
		MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor =
        LoadCursorW(
            nullptr,
            MAKEINTRESOURCEW(
                IDC_ARROW));
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1);
    wc.lpszClassName =
        className;

    RegisterClassExW(
        &wc);

    g_settingsWindow =
        CreateWindowExW(
            WS_EX_APPWINDOW,
            className,
            L"Continuous Pinch V3 设置",
            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            620,
            610,
            nullptr,
            nullptr,
            hInstance,
            nullptr);

    return
        g_settingsWindow !=
        nullptr;
}


static void ShowSettingsWindow()
{
    if (!g_settingsWindow)
        return;

    PopulateSettingsFromConfig(
        g_settingsWindow);

    ShowWindow(
        g_settingsWindow,
        SW_SHOW);

    SetForegroundWindow(
        g_settingsWindow);
}


// ============================================================================
// Main window
// ============================================================================

static void ToggleEnabled()
{
    bool enabled;

    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg.enabled =
            !g_cfg.enabled;

        enabled =
            g_cfg.enabled;
    }

    g_enabled.store(
        enabled,
        std::memory_order_relaxed);

    if (!enabled &&
        g_pinch.active)
    {
        EndActiveSegment();
    }

    SaveConfig();
    UpdateTrayTooltip();
}


static void ToggleStartup()
{
    {
        std::lock_guard<std::mutex> lock(
            g_cfgMutex);

        g_cfg.startWithWindows =
            !g_cfg.startWithWindows;
    }

    SaveConfig();
}


static LRESULT CALLBACK MainWndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        case Const::WM_APP_WHEEL:
        {
            const short delta =
                static_cast<short>(
                    LOWORD(
                        wParam));

            ApplyWheelDelta(
                delta);

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
                MessageBoxW(
                    nullptr,
                    L10n(L"硬复位失败：无法重新创建虚拟触控板。", L"Hard reset failed to recreate the synthetic touchpad."),
                    Const::AppName,
                    MB_OK |
                    MB_ICONERROR);
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
            if (wParam ==
                Const::AnimationTimerId)
            {
                TickPinch();
                return 0;
            }

            break;
        }

        case WM_HOTKEY:
        {
            if (wParam ==
                Const::ExitHotkeyId)
            {
                DestroyWindow(
                    hwnd);

                return 0;
            }

            break;
        }

        case Const::WM_TRAY:
        {
            if (lParam ==
                    WM_RBUTTONUP ||
                lParam ==
                    WM_CONTEXTMENU)
            {
                ShowTrayMenu();
                return 0;
            }

            if (lParam ==
                    WM_LBUTTONDBLCLK)
            {
                ShowSettingsWindow();
                return 0;
            }

            break;
        }

        case WM_COMMAND:
        {
            switch (
                LOWORD(
                    wParam))
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
                    DestroyWindow(
                        hwnd);
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

            KillTimer(
                hwnd,
                Const::AnimationTimerId);

            UnregisterHotKey(
                hwnd,
                Const::ExitHotkeyId);

            PostQuitMessage(
                0);

            return 0;
        }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam);
}


// ============================================================================
// Entry
// ============================================================================

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    timeBeginPeriod(1);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC =
        ICC_BAR_CLASSES |
        ICC_STANDARD_CLASSES |
        ICC_WIN95_CLASSES;

    InitCommonControlsEx(
        &icc);

    g_configPath =
        GetConfigPath();

    LoadConfig();

    if (!CreateTouchpadDevice())
    {
        const DWORD err =
            GetLastError();

        wchar_t text[512]{};

        swprintf_s(
            text,
            L"Failed to create a Windows 11 synthetic precision touchpad.\n\n"
            L"GetLastError = %lu",
            err);

        MessageBoxW(
            nullptr,
            text,
            Const::AppName,
            MB_OK |
            MB_ICONERROR);

        timeEndPeriod(1);
        return 1;
    }

    const wchar_t* mainClass =
        L"ContinuousPinchV3MainWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc =
        MainWndProc;
    wc.hInstance =
        hInstance;
    wc.lpszClassName =
        mainClass;

    if (!RegisterClassExW(
            &wc))
    {
        DestroyTouchpadDevice();
        timeEndPeriod(1);
        return 1;
    }

    g_mainWindow =
        CreateWindowExW(
            0,
            mainClass,
            Const::AppName,
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            hInstance,
            nullptr);

    if (!g_mainWindow)
    {
        DestroyTouchpadDevice();
        timeEndPeriod(1);
        return 1;
    }

    if (!CreateSettingsWindow(
            hInstance))
    {
        DestroyWindow(
            g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    if (!SetTimer(
            g_mainWindow,
            Const::AnimationTimerId,
            Const::AnimationIntervalMs,
            nullptr))
    {
        DestroyWindow(
            g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    RegisterExitHotkey();

    UpdateForegroundAllowed(
        GetForegroundWindow());

    g_foregroundHook =
        SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            ForegroundWinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT |
            WINEVENT_SKIPOWNPROCESS);

    g_mouseHook =
        SetWindowsHookExW(
            WH_MOUSE_LL,
            LowLevelMouseProc,
            hInstance,
            0);

    if (!g_mouseHook)
    {
        DestroyWindow(
            g_mainWindow);

        timeEndPeriod(1);
        return 1;
    }

    AddTrayIcon();
    UpdateTrayTooltip();

    // First run: show Settings so the controls are discoverable.
    if (GetFileAttributesW(
            g_configPath.c_str()) ==
        INVALID_FILE_ATTRIBUTES)
    {
        SaveConfig();
        ShowSettingsWindow();
    }

    MSG msg{};

    while (GetMessageW(
               &msg,
               nullptr,
               0,
               0) > 0)
    {
        TranslateMessage(
            &msg);

        DispatchMessageW(
            &msg);
    }

    if (g_mouseHook)
    {
        UnhookWindowsHookEx(
            g_mouseHook);

        g_mouseHook = nullptr;
    }

    if (g_foregroundHook)
    {
        UnhookWinEvent(
            g_foregroundHook);

        g_foregroundHook = nullptr;
    }

    if (g_settingsWindow)
    {
        DestroyWindow(
            g_settingsWindow);

        g_settingsWindow =
            nullptr;
    }

    g_mainWindow =
        nullptr;

    timeEndPeriod(1);

    return 0;
}
