#pragma once

// Configuration state, INI persistence, startup registration, localization.

#include "Common.h"

// g_cfg is the live configuration used at runtime. While the Settings window
// is open it may contain preview values (sensitivity / curve / language)
// that the user has not applied yet.
//
// g_committedCfg is what has been applied and is what SaveConfig() persists.
// Closing Settings without applying restores g_cfg from g_committedCfg.
extern AppConfig g_cfg;
extern AppConfig g_committedCfg;
extern std::mutex g_cfgMutex;

extern std::wstring g_configPath;

UiLanguage CurrentLanguage();
const wchar_t* L10n(const wchar_t* zh, const wchar_t* en);

// Chooses the INI location: beside the EXE when possible (portable),
// otherwise %APPDATA%\ContinuousPinch.
std::wstring GetConfigPath();

enum class StartupRegistration
{
    None,      // no Run entry
    ThisExe,   // the Run entry starts this EXE
    OtherPath  // the Run entry starts another copy / an old location
};

// Reads HKCU\...\Run. `registeredPath` receives the EXE the entry points to.
StartupRegistration QueryStartupRegistration(std::wstring* registeredPath = nullptr);

// Points the Run entry at this EXE, or removes it.
bool SetRunAtStartup(bool enabled);

void LoadConfig();

// Persists g_committedCfg, never the live preview values in g_cfg.
void SaveConfig();
