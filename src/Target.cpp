#include "Target.h"

#include "Util.h"

// ============================================================================
// Target cache
// ============================================================================

namespace
{

struct TargetCache
{
    bool valid = false;
    HWND root = nullptr;
    DWORD pid = 0;
    bool allowed = false;
};

}  // namespace

static TargetCache g_targetCache;


void InvalidateTargetCache()
{
    g_targetCache.valid = false;
}


// ============================================================================
// Target application whitelist
// ============================================================================

static bool IsAllowedProcess(DWORD pid, const std::vector<std::wstring>& whitelist)
{
    if (pid == 0)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!process)
        return false;

    // Heap buffer: this can run inside the low-level mouse hook, which should
    // not touch a 64 KB stack buffer on every cache miss.
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());

    const BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &size);

    CloseHandle(process);

    if (!ok)
        return false;

    path.resize(size);

    const std::wstring exe = ToLower(BaseName(path));

    return std::find(whitelist.begin(), whitelist.end(), exe) != whitelist.end();
}


static HWND RootWindow(HWND hwnd)
{
    if (!hwnd)
        return nullptr;

    HWND root = GetAncestor(hwnd, GA_ROOT);

    return root ? root : hwnd;
}


bool IsTargetAllowed(TargetMode mode, POINT cursor, const std::vector<std::wstring>& whitelist)
{
    HWND root = mode == TargetMode::Hover ? RootWindow(WindowFromPoint(cursor))
                                          : RootWindow(GetForegroundWindow());

    if (!root)
        return false;

    DWORD pid = 0;

    GetWindowThreadProcessId(root, &pid);

    // Compare the PID as well: a destroyed window's HWND value can be reused
    // by a window of a different process.
    if (g_targetCache.valid && g_targetCache.root == root && g_targetCache.pid == pid)
        return g_targetCache.allowed;

    g_targetCache.valid = true;
    g_targetCache.root = root;
    g_targetCache.pid = pid;
    g_targetCache.allowed = IsAllowedProcess(pid, whitelist);

    return g_targetCache.allowed;
}
