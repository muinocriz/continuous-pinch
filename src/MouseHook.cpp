#include "MouseHook.h"

#include "App.h"
#include "Config.h"
#include "Pinch.h"
#include "Target.h"

HHOOK g_mouseHook = nullptr;

static std::atomic<bool> g_swallowMiddleUp{false};

// ============================================================================
// Modifier handling
// ============================================================================

static UINT CurrentModifierMask()
{
    return CurrentModifierMaskForState();
}


static bool ExactModifiersDown(UINT expected)
{
    return CurrentModifierMask() == expected;
}


// ============================================================================
// Mouse hook
// ============================================================================

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
    {
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
    }

    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

    // If we swallowed a middle-button DOWN, always swallow the matching UP,
    // even if the cursor has since left the whitelisted window or the tool
    // was disabled in between. Otherwise another window would receive an
    // orphaned WM_MBUTTONUP.
    if (nCode == HC_ACTION && wParam == WM_MBUTTONUP
        && g_swallowMiddleUp.exchange(false, std::memory_order_relaxed))
    {
        return 1;
    }

    if (nCode == HC_ACTION && info && (info->flags & LLMHF_INJECTED) == 0
        && g_enabled.load(std::memory_order_relaxed)
        && g_targetAllowed.load(std::memory_order_relaxed))
    {
        AppConfig cfg;

        {
            std::lock_guard<std::mutex> lock(g_cfgMutex);

            cfg = g_cfg;
        }

        if (wParam == WM_MOUSEWHEEL && ExactModifiersDown(cfg.zoomModifiers))
        {
            const short delta = GET_WHEEL_DELTA_WPARAM(info->mouseData);

            PostMessageW(g_mainWindow, Const::WM_APP_WHEEL,
                         static_cast<WPARAM>(static_cast<USHORT>(delta)), 0);

            return 1;
        }

        if (wParam == WM_MBUTTONDOWN)
        {
            const UINT current = CurrentModifierMask();

            if (current == cfg.hardResetModifiers)
            {
                g_swallowMiddleUp.store(true, std::memory_order_relaxed);

                PostMessageW(g_mainWindow, Const::WM_APP_HARD_RESET, 0, 0);

                return 1;
            }

            if (current == cfg.resetModifiers)
            {
                g_swallowMiddleUp.store(true, std::memory_order_relaxed);

                PostMessageW(g_mainWindow, Const::WM_APP_SOFT_RESET, 0, 0);

                return 1;
            }
        }
    }

    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}
