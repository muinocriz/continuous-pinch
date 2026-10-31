#include "SettingsWindow.h"

#include "App.h"
#include "Config.h"
#include "InputThread.h"
#include "Tray.h"
#include "Util.h"

// ============================================================================
// Exit hotkey conversions
// ============================================================================

static UINT HotKeyControlFlagsToMod(BYTE flags)
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


static BYTE ModToHotKeyControlFlags(UINT mods)
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


bool RegisterExitHotkey()
{
    UnregisterHotKey(g_mainWindow, Const::ExitHotkeyId);

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    if (cfg.exitVk == 0)
        return true;

    return RegisterHotKey(g_mainWindow, Const::ExitHotkeyId, cfg.exitMods | MOD_NOREPEAT,
                          cfg.exitVk)
           != FALSE;
}


// ============================================================================
// Settings UI helpers
// ============================================================================

static HFONT GetDefaultGuiFont()
{
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}


static void SetControlFont(HWND hwnd)
{
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetDefaultGuiFont()), TRUE);
}


static HWND AddStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                                nullptr, GetModuleHandleW(nullptr), nullptr);

    SetControlFont(hwnd);

    return hwnd;
}


static HWND AddCombo(HWND parent, int id, int x, int y, int w, int h)
{
    HWND hwnd = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x,
        y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);

    SetControlFont(hwnd);

    return hwnd;
}


static void FillModifierCombo(HWND combo, UINT selectedMask)
{
    struct Item
    {
        const wchar_t* text;
        UINT mask;
    };

    const Item items[] = {{L"Alt", MM_ALT},
                          {L"Ctrl", MM_CTRL},
                          {L"Shift", MM_SHIFT},
                          {L"Ctrl + Alt", MM_CTRL | MM_ALT},
                          {L"Ctrl + Shift", MM_CTRL | MM_SHIFT},
                          {L"Alt + Shift", MM_ALT | MM_SHIFT},
                          {L"Ctrl + Alt + Shift", MM_CTRL | MM_ALT | MM_SHIFT}};

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    int selectIndex = 0;

    for (int i = 0; i < static_cast<int>(sizeof(items) / sizeof(items[0])); ++i)
    {
        const LRESULT index =
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i].text));

        SendMessageW(combo, CB_SETITEMDATA, index, items[i].mask);

        if (items[i].mask == selectedMask)
        {
            selectIndex = static_cast<int>(index);
        }
    }

    SendMessageW(combo, CB_SETCURSEL, selectIndex, 0);
}


static UINT GetModifierComboValue(HWND combo)
{
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);

    if (index == CB_ERR)
    {
        return MM_ALT;
    }

    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, index, 0);

    return static_cast<UINT>(data);
}


static void UpdateSensitivityLabel(HWND settings)
{
    HWND slider = GetDlgItem(settings, Const::IDC_SENS_SLIDER);

    HWND label = GetDlgItem(settings, Const::IDC_SENS_VALUE);

    const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));

    const double logValue = static_cast<double>(pos) / 1000.0;

    const double percent = (std::exp(logValue) - 1.0) * 100.0;

    wchar_t text[128]{};

    swprintf_s(text, L10n(L"每格滚轮 %.1f%%", L"%.1f%% per notch"), percent);

    SetWindowTextW(label, text);

    // Real-time: sensitivity changes immediately while the slider moves.
    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        g_cfg.sensitivityLog = logValue;
    }

    NotifyInputConfigChanged();
}


static void UpdateCurveRealtime(HWND settings)
{
    HWND combo = GetDlgItem(settings, Const::IDC_CURVE);

    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);

    if (index >= 0 && index <= 3)
    {
        {
            std::lock_guard<std::mutex> lock(g_cfgMutex);

            g_cfg.curve = static_cast<SmoothingCurve>(index);
        }

        NotifyInputConfigChanged();
    }
}


static void ApplySettingsLanguage(HWND settings)
{
    const bool zh = CurrentLanguage() == UiLanguage::Chinese;

    SetWindowTextW(settings, zh ? L"Continuous Pinch V3 设置" : L"Continuous Pinch V3 Settings");

    SetWindowTextW(GetDlgItem(settings, Const::IDC_ENABLED),
                   zh ? L"启用捏合缩放" : L"Enable pinch zoom");

    SetWindowTextW(GetDlgItem(settings, Const::IDC_STARTUP),
                   zh ? L"开机启动" : L"Start with Windows");

    SetWindowTextW(GetDlgItem(settings, Const::IDC_DEFAULTS), zh ? L"恢复默认" : L"Defaults");

    SetWindowTextW(GetDlgItem(settings, Const::IDC_APPLY), zh ? L"应用" : L"Apply");

    SetWindowTextW(GetDlgItem(settings, Const::IDC_CLOSE), zh ? L"关闭" : L"Close");

    HWND curve = GetDlgItem(settings, Const::IDC_CURVE);
    const LRESULT currentCurve = SendMessageW(curve, CB_GETCURSEL, 0, 0);

    SendMessageW(curve, CB_RESETCONTENT, 0, 0);
    SendMessageW(curve, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(zh ? L"直接 / 即时" : L"Direct / immediate"));
    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(zh ? L"线性" : L"Linear"));
    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(zh ? L"指数" : L"Exponential"));
    SendMessageW(curve, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(zh ? L"自适应（推荐）" : L"Adaptive (recommended)"));

    if (currentCurve >= 0 && currentCurve <= 3)
        SendMessageW(curve, CB_SETCURSEL, currentCurve, 0);

    HWND language = GetDlgItem(settings, Const::IDC_LANGUAGE);
    if (language)
    {
        SendMessageW(language, CB_RESETCONTENT, 0, 0);
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中文"));
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(language, CB_SETCURSEL, CurrentLanguage() == UiLanguage::Chinese ? 0 : 1, 0);
    }

    HWND targetMode = GetDlgItem(settings, Const::IDC_TARGET_MODE);
    if (targetMode)
    {
        const LRESULT currentMode = SendMessageW(targetMode, CB_GETCURSEL, 0, 0);

        SendMessageW(targetMode, CB_RESETCONTENT, 0, 0);
        SendMessageW(targetMode, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(zh ? L"鼠标悬停窗口（推荐）"
                                                 : L"Window under mouse (recommended)"));
        SendMessageW(targetMode, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(zh ? L"当前前台窗口" : L"Foreground window"));

        if (currentMode >= 0 && currentMode <= 1)
            SendMessageW(targetMode, CB_SETCURSEL, currentMode, 0);
    }

    // Re-render the sensitivity readout in the new language.
    UpdateSensitivityLabel(settings);
}

static void PopulateSettingsFromConfig(HWND settings, const AppConfig& cfg)
{
    SendMessageW(GetDlgItem(settings, Const::IDC_ENABLED), BM_SETCHECK,
                 cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    HWND slider = GetDlgItem(settings, Const::IDC_SENS_SLIDER);

    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(20, 180));

    SendMessageW(slider, TBM_SETTICFREQ, 10, 0);

    SendMessageW(slider, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(cfg.sensitivityLog * 1000.0)));

    UpdateSensitivityLabel(settings);

    HWND curve = GetDlgItem(settings, Const::IDC_CURVE);

    SendMessageW(curve, CB_RESETCONTENT, 0, 0);

    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Direct / immediate"));

    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Linear"));

    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Exponential"));

    SendMessageW(curve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Adaptive (recommended)"));

    SendMessageW(curve, CB_SETCURSEL, static_cast<WPARAM>(cfg.curve), 0);

    const std::wstring whitelistUi = WhitelistForUi(cfg.whitelist);

    SetWindowTextW(GetDlgItem(settings, Const::IDC_WHITELIST), whitelistUi.c_str());

    FillModifierCombo(GetDlgItem(settings, Const::IDC_ZOOM_MOD), cfg.zoomModifiers);

    FillModifierCombo(GetDlgItem(settings, Const::IDC_RESET_MOD), cfg.resetModifiers);

    FillModifierCombo(GetDlgItem(settings, Const::IDC_HARDRESET_MOD), cfg.hardResetModifiers);

    const WORD hotkeyWord =
        MAKEWORD(static_cast<BYTE>(cfg.exitVk), ModToHotKeyControlFlags(cfg.exitMods));

    SendMessageW(GetDlgItem(settings, Const::IDC_EXIT_HOTKEY), HKM_SETHOTKEY, hotkeyWord, 0);

    SendMessageW(GetDlgItem(settings, Const::IDC_STARTUP), BM_SETCHECK,
                 cfg.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(GetDlgItem(settings, Const::IDC_LANGUAGE), CB_SETCURSEL,
                 cfg.language == UiLanguage::Chinese ? 0 : 1, 0);

    SendMessageW(GetDlgItem(settings, Const::IDC_TARGET_MODE), CB_SETCURSEL,
                 static_cast<WPARAM>(cfg.targetMode), 0);

    ApplySettingsLanguage(settings);
}


static bool ApplySettingsFromWindow(HWND settings, bool showErrors)
{
    AppConfig newCfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        newCfg = g_committedCfg;
    }

    newCfg.enabled =
        SendMessageW(GetDlgItem(settings, Const::IDC_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;

    const int sensPos = static_cast<int>(
        SendMessageW(GetDlgItem(settings, Const::IDC_SENS_SLIDER), TBM_GETPOS, 0, 0));

    newCfg.sensitivityLog = std::clamp(static_cast<double>(sensPos) / 1000.0, 0.020, 0.180);

    const LRESULT curveIndex =
        SendMessageW(GetDlgItem(settings, Const::IDC_CURVE), CB_GETCURSEL, 0, 0);

    if (curveIndex >= 0 && curveIndex <= 3)
    {
        newCfg.curve = static_cast<SmoothingCurve>(curveIndex);
    }

    newCfg.zoomModifiers = GetModifierComboValue(GetDlgItem(settings, Const::IDC_ZOOM_MOD));

    newCfg.resetModifiers = GetModifierComboValue(GetDlgItem(settings, Const::IDC_RESET_MOD));

    newCfg.hardResetModifiers =
        GetModifierComboValue(GetDlgItem(settings, Const::IDC_HARDRESET_MOD));

    const WORD hotkey = static_cast<WORD>(
        SendMessageW(GetDlgItem(settings, Const::IDC_EXIT_HOTKEY), HKM_GETHOTKEY, 0, 0));

    newCfg.exitVk = LOBYTE(hotkey);

    newCfg.exitMods = HotKeyControlFlagsToMod(HIBYTE(hotkey));

    newCfg.startWithWindows =
        SendMessageW(GetDlgItem(settings, Const::IDC_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED;

    const LRESULT languageIndex =
        SendMessageW(GetDlgItem(settings, Const::IDC_LANGUAGE), CB_GETCURSEL, 0, 0);

    newCfg.language = languageIndex == 1 ? UiLanguage::English : UiLanguage::Chinese;

    const LRESULT targetModeIndex =
        SendMessageW(GetDlgItem(settings, Const::IDC_TARGET_MODE), CB_GETCURSEL, 0, 0);

    newCfg.targetMode = targetModeIndex == 1 ? TargetMode::Foreground : TargetMode::Hover;

    HWND whitelistEdit = GetDlgItem(settings, Const::IDC_WHITELIST);

    const int len = GetWindowTextLengthW(whitelistEdit);

    std::wstring whitelistUi;

    if (len > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(len) + 1, L'\0');

        GetWindowTextW(whitelistEdit, buffer.data(), len + 1);

        whitelistUi.assign(buffer.data());
    }

    newCfg.whitelist = WhitelistForIni(whitelistUi);

    if (newCfg.whitelist.empty())
    {
        if (showErrors)
        {
            MessageBoxW(
                settings,
                L10n(L"应用白名单不能为空。", L"The application whitelist cannot be empty."),
                Const::AppName, MB_OK | MB_ICONWARNING);
        }

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        g_cfg = newCfg;
        g_committedCfg = newCfg;
    }

    // The input thread re-reads the config: picks up the new whitelist and
    // target mode, and lifts an active gesture if the tool was disabled.
    NotifyInputConfigChanged();

    SaveConfig();

    const bool hotkeyOk = RegisterExitHotkey();

    UpdateTrayTooltip();
    ApplySettingsLanguage(settings);

    if (!hotkeyOk && showErrors)
    {
        MessageBoxW(settings,
                    L10n(L"所选退出热键无法注册。\n可能已被其他程序占用。",
                         L"The selected Exit hotkey could not be registered.\nAnother application "
                         L"may already be using it."),
                    Const::AppName, MB_OK | MB_ICONWARNING);
    }

    return true;
}


// "Defaults" only fills the controls with default values. Nothing is
// committed until Apply / Close. Like manual edits, only the preview fields
// (sensitivity, curve, language) take effect live; whitelist, modifiers and
// the enabled flag keep their committed values until applied.
static void ResetSettingsToDefaults(HWND settings)
{
    AppConfig defaults{};

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        // Start-with-Windows is a system setting; Defaults keeps it as is.
        defaults.startWithWindows = g_committedCfg.startWithWindows;

        g_cfg.sensitivityLog = defaults.sensitivityLog;
        g_cfg.curve = defaults.curve;
        g_cfg.language = defaults.language;
    }

    NotifyInputConfigChanged();

    PopulateSettingsFromConfig(settings, defaults);

    UpdateTrayTooltip();
}


// Discards unapplied preview values (used when Settings is closed via X).
static void RevertSettingsPreview()
{
    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        g_cfg = g_committedCfg;
    }

    NotifyInputConfigChanged();
    UpdateTrayTooltip();
}


// ============================================================================
// Settings window
// ============================================================================

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            AddStatic(hwnd, L"Continuous Pinch V3", 20, 15, 520, 24);

            AddStatic(hwnd, L"语言 / Language", 400, 18, 100, 22);

            HWND languageCombo = AddCombo(hwnd, Const::IDC_LANGUAGE, 500, 12, 90, 120);

            SendMessageW(languageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中文"));

            SendMessageW(languageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));

            HWND enabled = CreateWindowExW(
                0, L"BUTTON", L"Enable pinch zoom",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 20, 48, 200, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_ENABLED)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(enabled);

            AddStatic(hwnd, L"灵敏度 / Sensitivity", 20, 85, 100, 22);

            HWND slider = CreateWindowExW(
                0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS, 120,
                78, 300, 38, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_SENS_SLIDER)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(slider);

            // Explicit value label for the live sensitivity readout.
            HWND sensValue = CreateWindowExW(
                0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 430, 85, 155, 22, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_SENS_VALUE)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(sensValue);

            AddStatic(hwnd, L"平滑曲线 / Smoothing", 20, 126, 100, 22);

            AddCombo(hwnd, Const::IDC_CURVE, 120, 120, 300, 180);

            AddStatic(hwnd, L"缩放修饰键 / Zoom", 20, 166, 100, 22);

            AddCombo(hwnd, Const::IDC_ZOOM_MOD, 120, 160, 220, 180);

            AddStatic(hwnd, L"软复位 / Soft reset", 20, 206, 100, 22);

            AddCombo(hwnd, Const::IDC_RESET_MOD, 120, 200, 220, 180);

            AddStatic(hwnd, L"硬复位 / Hard reset", 20, 246, 100, 22);

            AddCombo(hwnd, Const::IDC_HARDRESET_MOD, 120, 240, 220, 180);

            AddStatic(hwnd, L"（修饰键 + 鼠标中键）", 355, 206, 220, 60);

            AddStatic(hwnd, L"退出热键 / Exit hotkey", 20, 286, 100, 22);

            HWND hotkey = CreateWindowExW(
                WS_EX_CLIENTEDGE, HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 120, 280,
                220, 26, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_EXIT_HOTKEY)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(hotkey);

            AddStatic(hwnd, L"目标判定 / Target", 20, 326, 100, 22);

            HWND targetMode = AddCombo(hwnd, Const::IDC_TARGET_MODE, 120, 320, 300, 120);

            SetControlFont(targetMode);

            AddStatic(hwnd, L"应用白名单（每行一个 exe）", 20, 366, 420, 22);

            HWND whitelist =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE
                                    | ES_AUTOVSCROLL | ES_WANTRETURN,
                                20, 390, 555, 120, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_WHITELIST)),
                                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(whitelist);

            HWND startup = CreateWindowExW(
                0, L"BUTTON", L10n(L"开机启动", L"Start with Windows"),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 20, 525, 200, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_STARTUP)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(startup);

            HWND defaults = CreateWindowExW(
                0, L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 250,
                560, 95, 30, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_DEFAULTS)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(defaults);

            HWND apply = CreateWindowExW(
                0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 355,
                560, 95, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_APPLY)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(apply);

            HWND close = CreateWindowExW(
                0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 460,
                560, 95, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Const::IDC_CLOSE)),
                GetModuleHandleW(nullptr), nullptr);

            SetControlFont(close);

            AppConfig cfg;

            {
                std::lock_guard<std::mutex> lock(g_cfgMutex);

                cfg = g_committedCfg;
            }

            PopulateSettingsFromConfig(hwnd, cfg);

            return 0;
        }

        case WM_HSCROLL:
        {
            HWND source = reinterpret_cast<HWND>(lParam);

            if (source == GetDlgItem(hwnd, Const::IDC_SENS_SLIDER))
            {
                UpdateSensitivityLabel(hwnd);
            }

            return 0;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);

            const int notify = HIWORD(wParam);

            if (id == Const::IDC_CURVE && notify == CBN_SELCHANGE)
            {
                UpdateCurveRealtime(hwnd);

                return 0;
            }

            if (id == Const::IDC_LANGUAGE && notify == CBN_SELCHANGE)
            {
                const LRESULT index =
                    SendMessageW(GetDlgItem(hwnd, Const::IDC_LANGUAGE), CB_GETCURSEL, 0, 0);

                {
                    std::lock_guard<std::mutex> lock(g_cfgMutex);

                    g_cfg.language = index == 1 ? UiLanguage::English : UiLanguage::Chinese;
                }

                ApplySettingsLanguage(hwnd);

                UpdateTrayTooltip();

                return 0;
            }

            if (id == Const::IDC_DEFAULTS)
            {
                ResetSettingsToDefaults(hwnd);

                return 0;
            }

            if (id == Const::IDC_APPLY)
            {
                ApplySettingsFromWindow(hwnd, true);

                return 0;
            }

            if (id == Const::IDC_CLOSE)
            {
                if (ApplySettingsFromWindow(hwnd, true))
                {
                    ShowWindow(hwnd, SW_HIDE);
                }

                return 0;
            }

            break;
        }

        case WM_CLOSE:
        {
            // X = cancel: drop unapplied preview values.
            RevertSettingsPreview();

            ShowWindow(hwnd, SW_HIDE);

            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


bool CreateSettingsWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"ContinuousPinchV3SettingsWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;

    RegisterClassExW(&wc);

    g_settingsWindow =
        CreateWindowExW(WS_EX_APPWINDOW, className, L"Continuous Pinch V3 设置",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                        CW_USEDEFAULT, 620, 650, nullptr, nullptr, hInstance, nullptr);

    return g_settingsWindow != nullptr;
}


void ShowSettingsWindow()
{
    if (!g_settingsWindow)
        return;

    // Already open: just bring it forward, keep the user's pending edits.
    if (IsWindowVisible(g_settingsWindow))
    {
        if (IsIconic(g_settingsWindow))
        {
            ShowWindow(g_settingsWindow, SW_RESTORE);
        }

        SetForegroundWindow(g_settingsWindow);

        return;
    }

    // Fresh open: start from the committed configuration.
    RevertSettingsPreview();

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_committedCfg;
    }

    PopulateSettingsFromConfig(g_settingsWindow, cfg);

    ShowWindow(g_settingsWindow, SW_SHOW);

    SetForegroundWindow(g_settingsWindow);
}
