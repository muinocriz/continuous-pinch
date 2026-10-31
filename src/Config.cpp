#include "Config.h"

#include "Util.h"

AppConfig g_cfg;
AppConfig g_committedCfg;
std::mutex g_cfgMutex;

std::atomic<bool> g_enabled{true};

std::wstring g_configPath;

// ============================================================================
// Localization
// ============================================================================

UiLanguage CurrentLanguage()
{
    std::lock_guard<std::mutex> lock(g_cfgMutex);
    return g_cfg.language;
}

const wchar_t* L10n(const wchar_t* zh, const wchar_t* en)
{
    return CurrentLanguage() == UiLanguage::Chinese ? zh : en;
}


// ============================================================================
// Config path / INI
// ============================================================================


std::wstring GetConfigPath()
{
    std::wstring exe = GetExePath();

    const size_t slash = exe.find_last_of(L"\\/");

    if (slash == std::wstring::npos)
        return Const::ConfigFileName;

    return exe.substr(0, slash + 1) + Const::ConfigFileName;
}


static int ReadIniInt(const wchar_t* section, const wchar_t* key, int defaultValue)
{
    return static_cast<int>(
        GetPrivateProfileIntW(section, key, defaultValue, g_configPath.c_str()));
}


static double ReadIniDouble(const wchar_t* section, const wchar_t* key, double defaultValue)
{
    wchar_t buffer[128]{};

    wchar_t def[64]{};
    swprintf_s(def, L"%.6f", defaultValue);

    GetPrivateProfileStringW(section, key, def, buffer,
                             static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                             g_configPath.c_str());

    wchar_t* end = nullptr;
    const double value = wcstod(buffer, &end);

    if (end == buffer)
        return defaultValue;

    return value;
}


static std::wstring ReadIniString(const wchar_t* section, const wchar_t* key,
                                  const wchar_t* defaultValue)
{
    wchar_t buffer[8192]{};

    GetPrivateProfileStringW(section, key, defaultValue, buffer,
                             static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                             g_configPath.c_str());

    return buffer;
}


static void WriteIniInt(const wchar_t* section, const wchar_t* key, int value)
{
    wchar_t buffer[64]{};

    swprintf_s(buffer, L"%d", value);

    WritePrivateProfileStringW(section, key, buffer, g_configPath.c_str());
}


static void WriteIniDouble(const wchar_t* section, const wchar_t* key, double value)
{
    wchar_t buffer[64]{};

    swprintf_s(buffer, L"%.6f", value);

    WritePrivateProfileStringW(section, key, buffer, g_configPath.c_str());
}


bool IsRunAtStartup()
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_QUERY_VALUE, &key)
        != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t value[32768]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);

    const LSTATUS status = RegQueryValueExW(key, Const::RunRegistryValue, nullptr, &type,
                                            reinterpret_cast<BYTE*>(value), &bytes);

    RegCloseKey(key);

    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}


bool SetRunAtStartup(bool enabled)
{
    HKEY key = nullptr;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        != ERROR_SUCCESS)
    {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;

    if (enabled)
    {
        std::wstring command = L"\"" + GetExePath() + L"\"";

        const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));

        status = RegSetValueExW(key, Const::RunRegistryValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    }
    else
    {
        status = RegDeleteValueW(key, Const::RunRegistryValue);

        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }

    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}


void LoadConfig()
{
    AppConfig cfg{};

    cfg.language = static_cast<UiLanguage>(std::clamp(
        ReadIniInt(L"General", L"Language", static_cast<int>(UiLanguage::Chinese)), 0, 1));

    // The INI key is always "Enabled".
    //
    // v1.0.0 / v1.0.1 accidentally localized this key: they wrote "启用"
    // while the UI was Chinese and "Enabled" while it was English. The key
    // matching the saved language is the one the old version wrote last.
    // SaveConfig() removes the legacy key, so after the first save only
    // "Enabled" remains.
    const int enabledValue = ReadIniInt(L"General", Const::IniKeyEnabled, -1);

    const int legacyEnabledValue = ReadIniInt(L"General", Const::IniKeyEnabledLegacy, -1);

    if (legacyEnabledValue != -1 && (cfg.language == UiLanguage::Chinese || enabledValue == -1))
    {
        cfg.enabled = legacyEnabledValue != 0;
    }
    else
    {
        cfg.enabled = enabledValue != 0;  // -1 (missing) => enabled
    }

    cfg.sensitivityLog = std::clamp(ReadIniDouble(L"Zoom", L"SensitivityLog", 0.070), 0.020, 0.180);

    cfg.curve = static_cast<SmoothingCurve>(std::clamp(
        ReadIniInt(L"Zoom", L"SmoothingCurve", static_cast<int>(SmoothingCurve::Adaptive)), 0, 3));

    cfg.zoomModifiers = static_cast<UINT>(ReadIniInt(L"Hotkeys", L"ZoomModifiers", MM_ALT));

    cfg.resetModifiers = static_cast<UINT>(ReadIniInt(L"Hotkeys", L"ResetModifiers", MM_ALT));

    cfg.hardResetModifiers =
        static_cast<UINT>(ReadIniInt(L"Hotkeys", L"HardResetModifiers", MM_ALT | MM_CTRL));

    cfg.exitVk = static_cast<UINT>(ReadIniInt(L"Hotkeys", L"ExitVk", VK_F12));

    cfg.exitMods =
        static_cast<UINT>(ReadIniInt(L"Hotkeys", L"ExitMods", MOD_CONTROL | MOD_ALT | MOD_SHIFT));

    cfg.whitelist = ReadIniString(L"Applications", L"Whitelist", L"chrome.exe;msedge.exe");

    cfg.targetMode = static_cast<TargetMode>(std::clamp(
        ReadIniInt(L"Applications", L"TargetMode", static_cast<int>(TargetMode::Hover)), 0, 1));

    cfg.startWithWindows = IsRunAtStartup();

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        g_cfg = cfg;
        g_committedCfg = cfg;
    }

    g_enabled.store(cfg.enabled, std::memory_order_relaxed);
}


void SaveConfig()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_committedCfg;
    }

    WriteIniInt(L"General", Const::IniKeyEnabled, cfg.enabled ? 1 : 0);

    // Remove the legacy localized key written by v1.0.0 / v1.0.1.
    WritePrivateProfileStringW(L"General", Const::IniKeyEnabledLegacy, nullptr,
                               g_configPath.c_str());

    WriteIniInt(L"General", L"Language", static_cast<int>(cfg.language));

    WriteIniDouble(L"Zoom", L"SensitivityLog", cfg.sensitivityLog);

    WriteIniInt(L"Zoom", L"SmoothingCurve", static_cast<int>(cfg.curve));

    WriteIniInt(L"Hotkeys", L"ZoomModifiers", static_cast<int>(cfg.zoomModifiers));

    WriteIniInt(L"Hotkeys", L"ResetModifiers", static_cast<int>(cfg.resetModifiers));

    WriteIniInt(L"Hotkeys", L"HardResetModifiers", static_cast<int>(cfg.hardResetModifiers));

    WriteIniInt(L"Hotkeys", L"ExitVk", static_cast<int>(cfg.exitVk));

    WriteIniInt(L"Hotkeys", L"ExitMods", static_cast<int>(cfg.exitMods));

    WritePrivateProfileStringW(L"Applications", L"Whitelist", cfg.whitelist.c_str(),
                               g_configPath.c_str());

    WriteIniInt(L"Applications", L"TargetMode", static_cast<int>(cfg.targetMode));

    SetRunAtStartup(cfg.startWithWindows);
}
