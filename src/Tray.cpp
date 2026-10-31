#include "Tray.h"

#include "App.h"
#include "Config.h"

static NOTIFYICONDATAW g_nid{};

// ============================================================================
// Tray
// ============================================================================

void UpdateTrayTooltip()
{
    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    const bool zh = cfg.language == UiLanguage::Chinese;

    const wchar_t* state =
        cfg.enabled ? (zh ? L"已启用" : L"Enabled") : (zh ? L"已禁用" : L"Disabled");

    swprintf_s(g_nid.szTip, L"Continuous Pinch V3 - %s", state);

    if (g_nid.hWnd)
    {
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}


bool AddTrayIcon()
{
    ZeroMemory(&g_nid, sizeof(g_nid));

    g_nid.cbSize = sizeof(g_nid);

    g_nid.hWnd = g_mainWindow;

    g_nid.uID = Const::TrayIconId;

    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

    g_nid.uCallbackMessage = Const::WM_TRAY;

    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));

    wcscpy_s(g_nid.szTip, L"Continuous Pinch V3");

    return Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}


void RemoveTrayIcon()
{
    if (g_nid.hWnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);

        g_nid.hWnd = nullptr;
    }
}


void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();

    if (!menu)
        return;

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    AppendMenuW(menu, MF_STRING | (cfg.enabled ? MF_CHECKED : MF_UNCHECKED), Const::IDM_TRAY_ENABLE,
                L10n(L"启用", L"Enabled"));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, Const::IDM_TRAY_SETTINGS, L10n(L"设置...", L"Settings..."));

    AppendMenuW(menu, MF_STRING, Const::IDM_TRAY_RESET,
                L10n(L"回到 100% 基准", L"Return to 100% baseline"));

    AppendMenuW(menu, MF_STRING, Const::IDM_TRAY_HARDRESET,
                L10n(L"硬复位触控板", L"Hard reset touchpad"));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (cfg.startWithWindows ? MF_CHECKED : MF_UNCHECKED),
                Const::IDM_TRAY_STARTUP, L10n(L"开机启动", L"Start with Windows"));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, Const::IDM_TRAY_EXIT, L10n(L"退出", L"Exit"));

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(g_mainWindow);

    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_mainWindow, nullptr);

    // Documented workaround (KB135788): make sure the menu closes correctly
    // and a second right-click shows it again.
    PostMessageW(g_mainWindow, WM_NULL, 0, 0);

    DestroyMenu(menu);
}
