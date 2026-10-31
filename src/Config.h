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

extern std::atomic<bool> g_enabled;

extern std::wstring g_configPath;

UiLanguage CurrentLanguage();
const wchar_t* L10n(const wchar_t* zh, const wchar_t* en);

std::wstring GetConfigPath();

bool IsRunAtStartup();
bool SetRunAtStartup(bool enabled);

void LoadConfig();

// Persists g_committedCfg, never the live preview values in g_cfg.
void SaveConfig();
