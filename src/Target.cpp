#include "Target.h"

#include "Config.h"
#include "Util.h"

// Target-application cache. The main message thread updates it; WH_MOUSE_LL
// only reads the atomic flag so the low-level hook remains lightweight.
std::atomic<bool> g_targetAllowed{false};
static HWND g_lastTargetRoot = nullptr;

// ============================================================================
// Target application whitelist
// ============================================================================

static bool IsAllowedProcess(DWORD pid)
{
    if (pid == 0)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!process)
        return false;

    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));

    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);

    CloseHandle(process);

    if (!ok)
        return false;

    const std::wstring exe = ToLower(BaseName(std::wstring(path, size)));

    std::wstring whitelist;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        whitelist = g_cfg.whitelist;
    }

    const auto entries = ParseWhitelist(whitelist);

    return std::find(entries.begin(), entries.end(), exe) != entries.end();
}


static HWND RootWindowUnderPoint(POINT pt)
{
    HWND hwnd = WindowFromPoint(pt);

    if (!hwnd)
        return nullptr;

    HWND root = GetAncestor(hwnd, GA_ROOT);

    return root ? root : hwnd;
}


void RefreshTargetAllowed(bool force)
{
    TargetMode mode = TargetMode::Hover;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        mode = g_cfg.targetMode;
    }

    HWND root = nullptr;

    if (mode == TargetMode::Hover)
    {
        POINT pt{};

        if (GetCursorPos(&pt))
            root = RootWindowUnderPoint(pt);
    }
    else
    {
        HWND hwnd = GetForegroundWindow();

        if (hwnd)
        {
            HWND top = GetAncestor(hwnd, GA_ROOT);

            root = top ? top : hwnd;
        }
    }

    if (!force && root == g_lastTargetRoot)
    {
        return;
    }

    g_lastTargetRoot = root;

    if (!root)
    {
        g_targetAllowed.store(false, std::memory_order_relaxed);

        return;
    }

    DWORD pid = 0;

    GetWindowThreadProcessId(root, &pid);

    g_targetAllowed.store(IsAllowedProcess(pid), std::memory_order_relaxed);
}
