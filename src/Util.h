#pragma once

// Small helpers: time, strings, whitelist text, paths.

#include "Common.h"

double NowSeconds();

std::wstring Trim(std::wstring s);
std::wstring ToLower(std::wstring s);
std::wstring BaseName(const std::wstring& path);

std::vector<std::wstring> ParseWhitelist(const std::wstring& text);
std::wstring WhitelistForUi(const std::wstring& whitelist);
std::wstring WhitelistForIni(const std::wstring& uiText);

std::wstring GetExePath();
