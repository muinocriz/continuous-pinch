#include "MouseHook.h"

#include "InputThread.h"
#include "Target.h"

static HHOOK g_mouseHook = nullptr;

// Set when a middle-button DOWN was swallowed, so the matching UP is
// swallowed too.
static bool g_swallowMiddleUp = false;

// ============================================================================
// Mouse hook
// ============================================================================

// Every mouse event in the system passes through this callback, including
// every mouse move of a 1000 Hz mouse, so it must stay cheap:
//   - anything that is not wheel / middle button returns immediately;
//   - config is read from the input thread's snapshot (same thread, no lock,
//     no copies);
//   - the target lookup only runs for events that already match the
//     configured modifiers, and is cached per window;
//   - the actual work is posted to the input thread's queue.
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    if (wParam != WM_MOUSEWHEEL && wParam != WM_MBUTTONDOWN && wParam != WM_MBUTTONUP)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    // If we swallowed a middle-button DOWN, always swallow the matching UP,
    // even if the cursor has since left the whitelisted window or the tool
    // was disabled in between. Otherwise another window would receive an
    // orphaned WM_MBUTTONUP.
    if (wParam == WM_MBUTTONUP)
    {
        if (g_swallowMiddleUp)
        {
            g_swallowMiddleUp = false;
            return 1;
        }

        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
    }

    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    const InputConfig& cfg = InputThreadConfig();

    if (!info || (info->flags & LLMHF_INJECTED) != 0 || !cfg.enabled)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    const UINT mods = CurrentModifierMask();

    const bool isWheel = wParam == WM_MOUSEWHEEL;

    const bool modifiersMatch =
        isWheel ? mods == cfg.zoomModifiers
                : (mods == cfg.hardResetModifiers || mods == cfg.resetModifiers);

    if (!modifiersMatch)
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    // Hover mode checks the window under this event's own position.
    if (!IsTargetAllowed(cfg.targetMode, info->pt, cfg.whitelist))
        return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

    const LPARAM altHeld = (mods & MM_ALT) ? 1 : 0;
    const DWORD self = GetCurrentThreadId();

    if (isWheel)
    {
        const short delta = GET_WHEEL_DELTA_WPARAM(info->mouseData);

        PostThreadMessageW(self, Const::WM_INPUT_WHEEL,
                           static_cast<WPARAM>(static_cast<USHORT>(delta)), altHeld);

        return 1;
    }

    // Middle button: hard reset takes precedence if both use the same keys.
    g_swallowMiddleUp = true;

    PostThreadMessageW(self,
                       mods == cfg.hardResetModifiers ? Const::WM_INPUT_HARD_RESET
                                                      : Const::WM_INPUT_SOFT_RESET,
                       0, altHeld);

    return 1;
}


bool InstallMouseHook(HINSTANCE hInstance)
{
    if (g_mouseHook)
        return true;

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

    return g_mouseHook != nullptr;
}


void UninstallMouseHook()
{
    if (!g_mouseHook)
        return;

    UnhookWindowsHookEx(g_mouseHook);

    g_mouseHook = nullptr;
}
