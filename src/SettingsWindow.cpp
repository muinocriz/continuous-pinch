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
    HWND slider = GetDlgItem(settings, IDC_SENS_SLIDER);

    HWND label = GetDlgItem(settings, IDC_SENS_VALUE);

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
    HWND combo = GetDlgItem(settings, IDC_CURVE);

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


static void SetItemText(HWND dialog, int id, const wchar_t* text)
{
    SetDlgItemTextW(dialog, id, text);
}


// Explains a Run entry that points to another copy of the program.
static void UpdateStartupHint(HWND settings, bool zh)
{
    std::wstring other;

    const bool otherCopy = QueryStartupRegistration(&other) == StartupRegistration::OtherPath;

    SetItemText(settings, IDC_STARTUP_HINT,
                !otherCopy ? L""
                : zh ? L"开机启动当前指向另一个位置的程序，勾选后将改为本程序。"
                     : L"Startup currently launches another copy; checking this points it here.");
}


static void ApplySettingsLanguage(HWND settings)
{
    const bool zh = CurrentLanguage() == UiLanguage::Chinese;

    SetWindowTextW(settings, zh ? L"Continuous Pinch V3 设置" : L"Continuous Pinch V3 Settings");

    SetItemText(settings, IDC_GRP_GENERAL, zh ? L"常规" : L"General");
    SetItemText(settings, IDC_ENABLED, zh ? L"启用捏合缩放" : L"Enable pinch zoom");
    SetItemText(settings, IDC_LBL_LANGUAGE, zh ? L"语言" : L"Language");
    SetItemText(settings, IDC_STARTUP, zh ? L"开机启动" : L"Start with Windows");
    UpdateStartupHint(settings, zh);

    SetItemText(settings, IDC_GRP_ZOOM, zh ? L"缩放" : L"Zoom");
    SetItemText(settings, IDC_LBL_SENS, zh ? L"灵敏度" : L"Sensitivity");
    SetItemText(settings, IDC_LBL_CURVE, zh ? L"平滑曲线" : L"Smoothing");
    SetItemText(settings, IDC_LBL_TARGET, zh ? L"目标窗口" : L"Target window");

    SetItemText(settings, IDC_GRP_KEYS, zh ? L"按键" : L"Keys");
    SetItemText(settings, IDC_LBL_ZOOM_MOD, zh ? L"缩放" : L"Zoom");
    SetItemText(settings, IDC_LBL_ZOOM_HINT, zh ? L"+ 滚轮" : L"+ wheel");
    SetItemText(settings, IDC_LBL_RESET_MOD, zh ? L"软复位" : L"Soft reset");
    SetItemText(settings, IDC_LBL_RESET_HINT,
                zh ? L"+ 中键：回到 100%" : L"+ middle: back to 100%");
    SetItemText(settings, IDC_LBL_HARDRESET_MOD, zh ? L"硬复位" : L"Hard reset");
    SetItemText(settings, IDC_LBL_HARDRESET_HINT,
                zh ? L"+ 中键：重建虚拟触控板" : L"+ middle: recreate touchpad");
    SetItemText(settings, IDC_LBL_EXIT, zh ? L"退出热键" : L"Exit hotkey");

    SetItemText(settings, IDC_GRP_APPS, zh ? L"应用白名单" : L"Applications");
    SetItemText(settings, IDC_LBL_WHITELIST,
                zh ? L"每行一个进程名，例如 chrome.exe"
                   : L"One executable name per line, e.g. chrome.exe");

    SetItemText(settings, IDC_LBL_CONFIG, zh ? L"配置文件：" : L"Config file:");
    SetItemText(settings, IDC_CONFIG_PATH, g_configPath.c_str());
    SetItemText(settings, IDC_OPEN_CONFIG_DIR, zh ? L"打开位置" : L"Show file");

    SetItemText(settings, IDC_DEFAULTS, zh ? L"恢复默认" : L"Defaults");
    SetItemText(settings, IDOK, zh ? L"确定" : L"OK");
    SetItemText(settings, IDCANCEL, zh ? L"取消" : L"Cancel");
    SetItemText(settings, IDC_APPLY, zh ? L"应用" : L"Apply");

    HWND curve = GetDlgItem(settings, IDC_CURVE);
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

    HWND language = GetDlgItem(settings, IDC_LANGUAGE);
    if (language)
    {
        SendMessageW(language, CB_RESETCONTENT, 0, 0);
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中文"));
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(language, CB_SETCURSEL, CurrentLanguage() == UiLanguage::Chinese ? 0 : 1, 0);
    }

    HWND targetMode = GetDlgItem(settings, IDC_TARGET_MODE);
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
    SendMessageW(GetDlgItem(settings, IDC_ENABLED), BM_SETCHECK,
                 cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    HWND slider = GetDlgItem(settings, IDC_SENS_SLIDER);

    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(20, 180));

    SendMessageW(slider, TBM_SETTICFREQ, 10, 0);

    SendMessageW(slider, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(cfg.sensitivityLog * 1000.0)));

    UpdateSensitivityLabel(settings);

    // Fill the localized combo items first, then select the values.
    ApplySettingsLanguage(settings);

    SendMessageW(GetDlgItem(settings, IDC_CURVE), CB_SETCURSEL, static_cast<WPARAM>(cfg.curve), 0);

    const std::wstring whitelistUi = WhitelistForUi(cfg.whitelist);

    SetWindowTextW(GetDlgItem(settings, IDC_WHITELIST), whitelistUi.c_str());

    FillModifierCombo(GetDlgItem(settings, IDC_ZOOM_MOD), cfg.zoomModifiers);

    FillModifierCombo(GetDlgItem(settings, IDC_RESET_MOD), cfg.resetModifiers);

    FillModifierCombo(GetDlgItem(settings, IDC_HARDRESET_MOD), cfg.hardResetModifiers);

    const WORD hotkeyWord =
        MAKEWORD(static_cast<BYTE>(cfg.exitVk), ModToHotKeyControlFlags(cfg.exitMods));

    SendMessageW(GetDlgItem(settings, IDC_EXIT_HOTKEY), HKM_SETHOTKEY, hotkeyWord, 0);

    SendMessageW(GetDlgItem(settings, IDC_STARTUP), BM_SETCHECK,
                 cfg.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(GetDlgItem(settings, IDC_LANGUAGE), CB_SETCURSEL,
                 cfg.language == UiLanguage::Chinese ? 0 : 1, 0);

    SendMessageW(GetDlgItem(settings, IDC_TARGET_MODE), CB_SETCURSEL,
                 static_cast<WPARAM>(cfg.targetMode), 0);
}


static bool ApplySettingsFromWindow(HWND settings, bool showErrors)
{
    AppConfig newCfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        newCfg = g_committedCfg;
    }

    newCfg.enabled =
        SendMessageW(GetDlgItem(settings, IDC_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;

    const int sensPos =
        static_cast<int>(SendMessageW(GetDlgItem(settings, IDC_SENS_SLIDER), TBM_GETPOS, 0, 0));

    newCfg.sensitivityLog = std::clamp(static_cast<double>(sensPos) / 1000.0, 0.020, 0.180);

    const LRESULT curveIndex = SendMessageW(GetDlgItem(settings, IDC_CURVE), CB_GETCURSEL, 0, 0);

    if (curveIndex >= 0 && curveIndex <= 3)
    {
        newCfg.curve = static_cast<SmoothingCurve>(curveIndex);
    }

    newCfg.zoomModifiers = GetModifierComboValue(GetDlgItem(settings, IDC_ZOOM_MOD));

    newCfg.resetModifiers = GetModifierComboValue(GetDlgItem(settings, IDC_RESET_MOD));

    newCfg.hardResetModifiers = GetModifierComboValue(GetDlgItem(settings, IDC_HARDRESET_MOD));

    // Both resets use the middle button, so they need different modifiers;
    // otherwise the hard reset would silently shadow the soft reset.
    if (newCfg.resetModifiers == newCfg.hardResetModifiers)
    {
        if (showErrors)
        {
            MessageBoxW(settings,
                        L10n(L"软复位和硬复位不能使用相同的修饰键。",
                             L"Soft reset and hard reset cannot use the same modifier keys."),
                        Const::AppName, MB_OK | MB_ICONWARNING);

            SetFocus(GetDlgItem(settings, IDC_RESET_MOD));
        }

        return false;
    }

    const WORD hotkey =
        static_cast<WORD>(SendMessageW(GetDlgItem(settings, IDC_EXIT_HOTKEY), HKM_GETHOTKEY, 0, 0));

    newCfg.exitVk = LOBYTE(hotkey);

    newCfg.exitMods = HotKeyControlFlagsToMod(HIBYTE(hotkey));

    newCfg.startWithWindows =
        SendMessageW(GetDlgItem(settings, IDC_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED;

    const LRESULT languageIndex =
        SendMessageW(GetDlgItem(settings, IDC_LANGUAGE), CB_GETCURSEL, 0, 0);

    newCfg.language = languageIndex == 1 ? UiLanguage::English : UiLanguage::Chinese;

    const LRESULT targetModeIndex =
        SendMessageW(GetDlgItem(settings, IDC_TARGET_MODE), CB_GETCURSEL, 0, 0);

    newCfg.targetMode = targetModeIndex == 1 ? TargetMode::Foreground : TargetMode::Hover;

    HWND whitelistEdit = GetDlgItem(settings, IDC_WHITELIST);

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
// committed until OK / Apply. Like manual edits, only the preview fields
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


// Discards unapplied preview values (Cancel, Esc, or the X button).
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
// Settings dialog
// ============================================================================

// Opens Explorer with the INI selected (or its folder if it does not exist).
static void ShowConfigFileInExplorer()
{
    const DWORD attributes = GetFileAttributesW(g_configPath.c_str());

    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        const std::wstring args = L"/select,\"" + g_configPath + L"\"";

        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }

    const size_t slash = g_configPath.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
    {
        const std::wstring dir = g_configPath.substr(0, slash);

        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}


static void SetDialogIcons(HWND dialog)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    const auto load = [instance](int cx, int cy)
    {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                             cx, cy, LR_DEFAULTCOLOR | LR_SHARED));
    };

    SendMessageW(
        dialog, WM_SETICON, ICON_BIG,
        reinterpret_cast<LPARAM>(load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));

    SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(
                     load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
}


// Modeless dialog (IDD_SETTINGS in app.rc). It is created once and hidden
// instead of destroyed. OK = apply + hide; Cancel / Esc / X = revert + hide.
static INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_INITDIALOG:
        {
            SetDialogIcons(hwnd);

            AppConfig cfg;

            {
                std::lock_guard<std::mutex> lock(g_cfgMutex);

                cfg = g_committedCfg;
            }

            PopulateSettingsFromConfig(hwnd, cfg);

            return TRUE;
        }

        case WM_HSCROLL:
        {
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_SENS_SLIDER))
            {
                UpdateSensitivityLabel(hwnd);
                return TRUE;
            }

            break;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            const int notify = HIWORD(wParam);

            switch (id)
            {
                case IDC_CURVE:
                    if (notify == CBN_SELCHANGE)
                        UpdateCurveRealtime(hwnd);
                    return TRUE;

                case IDC_LANGUAGE:
                    if (notify == CBN_SELCHANGE)
                    {
                        const LRESULT index =
                            SendMessageW(GetDlgItem(hwnd, IDC_LANGUAGE), CB_GETCURSEL, 0, 0);

                        {
                            std::lock_guard<std::mutex> lock(g_cfgMutex);

                            g_cfg.language = index == 1 ? UiLanguage::English : UiLanguage::Chinese;
                        }

                        ApplySettingsLanguage(hwnd);
                        UpdateTrayTooltip();
                    }
                    return TRUE;

                case IDC_DEFAULTS:
                    ResetSettingsToDefaults(hwnd);
                    return TRUE;

                case IDC_APPLY:
                    ApplySettingsFromWindow(hwnd, true);
                    return TRUE;

                case IDC_OPEN_CONFIG_DIR:
                    ShowConfigFileInExplorer();
                    return TRUE;

                case IDOK:
                    if (ApplySettingsFromWindow(hwnd, true))
                        ShowWindow(hwnd, SW_HIDE);
                    return TRUE;

                case IDCANCEL:  // Cancel button, Esc, and the X button
                    RevertSettingsPreview();
                    ShowWindow(hwnd, SW_HIDE);
                    return TRUE;
            }

            break;
        }
    }

    return FALSE;
}


bool CreateSettingsWindow(HINSTANCE hInstance)
{
    g_settingsWindow =
        CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, SettingsDlgProc, 0);

    return g_settingsWindow != nullptr;
}


bool HandleSettingsDialogMessage(MSG& msg)
{
    return g_settingsWindow && IsDialogMessageW(g_settingsWindow, &msg);
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
