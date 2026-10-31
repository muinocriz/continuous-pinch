#include "Config.h"

#include "Util.h"

AppConfig g_cfg;
AppConfig g_committedCfg;
std::mutex g_cfgMutex;

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


static std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");

    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}


static bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}


// Can this process create files in `dir`? (e.g. not in Program Files)
static bool IsDirectoryWritable(const std::wstring& dir)
{
    const std::wstring probe =
        dir + L".continuous-pinch-write-test-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";

    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(file);

    return true;
}


// %APPDATA%\ContinuousPinch\ (with trailing backslash), or empty.
static std::wstring AppDataConfigDirectory()
{
    wchar_t appData[MAX_PATH]{};

    const DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);

    if (len == 0 || len >= MAX_PATH)
        return {};

    return std::wstring(appData, len) + L"\\" + Const::AppDataFolderName + L"\\";
}


// Where the INI lives:
//   1. an existing INI beside the EXE (portable use, and all v1.0.x setups);
//   2. an existing INI in %APPDATA%\ContinuousPinch;
//   3. beside the EXE if that folder is writable;
//   4. otherwise %APPDATA%\ContinuousPinch (e.g. EXE in Program Files).
std::wstring GetConfigPath()
{
    const std::wstring exeDir = DirectoryOf(GetExePath());
    const std::wstring portable = exeDir + Const::ConfigFileName;

    const std::wstring appDataDir = AppDataConfigDirectory();
    const std::wstring roaming =
        appDataDir.empty() ? std::wstring() : appDataDir + Const::ConfigFileName;

    if (FileExists(portable))
        return portable;

    if (!roaming.empty() && FileExists(roaming))
        return roaming;

    if (roaming.empty() || IsDirectoryWritable(exeDir))
        return portable;

    CreateDirectoryW(appDataDir.c_str(), nullptr);

    return roaming;
}


// GetPrivateProfileString / WritePrivateProfileString write UTF-16 only if
// the file already starts with a UTF-16 LE BOM; otherwise they use the ANSI
// code page, which cannot represent e.g. Chinese text on a non-Chinese
// system. This makes sure the INI is UTF-16:
//   - missing file: created with just a BOM (only when createIfMissing);
//   - ANSI file (v1.0.x) or UTF-8 file (hand-edited): converted in place.
static void EnsureUnicodeIniFile(bool createIfMissing)
{
    static const BYTE bom[2] = {0xFF, 0xFE};

    HANDLE file = CreateFileW(g_configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        if (!createIfMissing)
            return;

        file = CreateFileW(g_configPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, bom, sizeof(bom), &written, nullptr);
            CloseHandle(file);
        }

        return;
    }

    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);

    // A settings file is a few hundred bytes; leave anything odd alone.
    if (size.QuadPart > 1024 * 1024)
    {
        CloseHandle(file);
        return;
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;

    const BOOL ok =
        bytes.empty()
        || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);

    CloseHandle(file);

    if (!ok || read != bytes.size())
        return;

    if (bytes.size() >= 2 && static_cast<BYTE>(bytes[0]) == 0xFF
        && static_cast<BYTE>(bytes[1]) == 0xFE)
    {
        return;  // already UTF-16 LE
    }

    UINT codePage = CP_ACP;
    size_t offset = 0;

    if (bytes.size() >= 3 && static_cast<BYTE>(bytes[0]) == 0xEF
        && static_cast<BYTE>(bytes[1]) == 0xBB && static_cast<BYTE>(bytes[2]) == 0xBF)
    {
        codePage = CP_UTF8;
        offset = 3;
    }

    const int srcLen = static_cast<int>(bytes.size() - offset);

    std::wstring text;

    if (srcLen > 0)
    {
        const int wideLen =
            MultiByteToWideChar(codePage, 0, bytes.data() + offset, srcLen, nullptr, 0);

        if (wideLen <= 0)
            return;

        text.resize(static_cast<size_t>(wideLen));

        MultiByteToWideChar(codePage, 0, bytes.data() + offset, srcLen, text.data(), wideLen);
    }

    // Write a temporary file, then replace, so a failure never leaves a
    // half-written INI behind.
    const std::wstring temp = g_configPath + L".tmp";

    HANDLE out = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);

    if (out == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;

    bool writeOk = WriteFile(out, bom, sizeof(bom), &written, nullptr) != FALSE;

    const DWORD textBytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));

    if (writeOk && textBytes > 0)
        writeOk = WriteFile(out, text.data(), textBytes, &written, nullptr) != FALSE;

    CloseHandle(out);

    if (!writeOk
        || !MoveFileExW(temp.c_str(), g_configPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
    }
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


static constexpr wchar_t RunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";


// The executable a Run-key command line points to: the quoted part, or the
// text up to the first space if it is not quoted. Environment variables are
// expanded for REG_EXPAND_SZ values.
static std::wstring ExecutableFromCommand(std::wstring command, DWORD type)
{
    if (type == REG_EXPAND_SZ)
    {
        wchar_t expanded[32768]{};

        const DWORD len = ExpandEnvironmentStringsW(command.c_str(), expanded, 32768);

        if (len > 0 && len <= 32768)
            command = expanded;
    }

    command = Trim(command);

    if (!command.empty() && command.front() == L'"')
    {
        const size_t close = command.find(L'"', 1);

        return command.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1);
    }

    return command.substr(0, command.find(L' '));
}


StartupRegistration QueryStartupRegistration(std::wstring* registeredPath)
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return StartupRegistration::None;
    }

    std::wstring value(32768, L'\0');
    DWORD type = 0;
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));

    const LSTATUS status = RegQueryValueExW(key, Const::RunRegistryValue, nullptr, &type,
                                            reinterpret_cast<BYTE*>(value.data()), &bytes);

    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return StartupRegistration::None;

    value.resize(bytes / sizeof(wchar_t));

    while (!value.empty() && value.back() == L'\0')
        value.pop_back();

    const std::wstring exe = ExecutableFromCommand(value, type);

    if (registeredPath)
        *registeredPath = exe;

    const std::wstring self = GetExePath();

    const bool same = CompareStringOrdinal(exe.c_str(), static_cast<int>(exe.size()), self.c_str(),
                                           static_cast<int>(self.size()), TRUE)
                      == CSTR_EQUAL;

    return same ? StartupRegistration::ThisExe : StartupRegistration::OtherPath;
}


bool SetRunAtStartup(bool enabled)
{
    HKEY key = nullptr;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr)
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
    // Convert a v1.0.x ANSI (or hand-edited UTF-8) INI to UTF-16 first.
    EnsureUnicodeIniFile(false);

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

    // "Start with Windows" is on only if the Run entry points to this EXE.
    // An entry for another copy (e.g. the EXE was moved) shows as off; the
    // Settings window explains this, and enabling it repoints the entry.
    cfg.startWithWindows = QueryStartupRegistration() == StartupRegistration::ThisExe;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        g_cfg = cfg;
        g_committedCfg = cfg;
    }
}


void SaveConfig()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_committedCfg;
    }

    EnsureUnicodeIniFile(true);

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

    // Only touch the Run entry when it has to change, and never remove an
    // entry that belongs to another copy of the program.
    const StartupRegistration registration = QueryStartupRegistration();

    if (cfg.startWithWindows && registration != StartupRegistration::ThisExe)
        SetRunAtStartup(true);
    else if (!cfg.startWithWindows && registration == StartupRegistration::ThisExe)
        SetRunAtStartup(false);
}
