#include "Util.h"

// ============================================================================
// Time
// ============================================================================

double NowSeconds()
{
    static LARGE_INTEGER frequency = []
    {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);

    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}


// ============================================================================
// String helpers
// ============================================================================

std::wstring Trim(std::wstring s)
{
    auto notSpace = [](wchar_t c)
    {
        return !iswspace(c);
    };

    auto first = std::find_if(s.begin(), s.end(), notSpace);
    auto last = std::find_if(s.rbegin(), s.rend(), notSpace).base();

    if (first >= last)
        return L"";

    return std::wstring(first, last);
}


std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });

    return s;
}


std::wstring BaseName(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");

    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}


std::vector<std::wstring> ParseWhitelist(const std::wstring& text)
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


// A multi-line EDIT control only treats CRLF as a line break; a bare LF is
// rendered inline (e.g. "chrome.exemsedge.exe").
std::wstring WhitelistForUi(const std::wstring& whitelist)
{
    const auto items = ParseWhitelist(whitelist);

    std::wstring s;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i != 0)
            s += L"\r\n";

        s += items[i];
    }

    return s;
}


std::wstring WhitelistForIni(const std::wstring& uiText)
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
// Paths
// ============================================================================

std::wstring GetExePath()
{
    wchar_t path[32768]{};

    const DWORD len =
        GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));

    return std::wstring(path, len);
}
