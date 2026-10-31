#include "Pinch.h"

#include "Touchpad.h"
#include "Util.h"

// ============================================================================
// Pinch state
// ============================================================================

namespace
{

struct PinchState
{
    bool active = false;
    bool softResetting = false;

    double targetLogZoom = 0.0;
    double appliedLogZoom = 0.0;

    double segmentBaseLogZoom = 0.0;
    double currentDistance = Const::BaseDistance;

    POINT lastP1{};
    POINT lastP2{};

    double lastWheelTime = 0.0;
    double lastTickTime = 0.0;
    double lastProgressTime = 0.0;
};

}  // namespace

static PinchState g_pinch;


static void FingerPositionsForDistance(double distance, POINT& p1, POINT& p2)
{
    distance = std::clamp(distance, Const::MinDistance, Const::MaxDistance);

    const LONG half = static_cast<LONG>(std::lround(distance / 2.0));

    p1.x = Const::CenterX - half;
    p1.y = Const::CenterY;

    p2.x = Const::CenterX + half;
    p2.y = Const::CenterY;
}


static double MaxLogZoom()
{
    return std::log(Const::MaxLogicalScale);
}


static void ClearActiveSegmentOnly()
{
    g_pinch.active = false;

    g_pinch.currentDistance = Const::BaseDistance;
}


static bool LiftActiveSegment()
{
    if (!g_pinch.active)
        return true;

    if (!InjectTouchpadFrame(g_pinch.lastP1, g_pinch.lastP2, TouchpadFrame::Up))
    {
        return false;
    }

    ClearActiveSegmentOnly();

    return true;
}


void EndActiveSegment()
{
    LiftActiveSegment();
}


static bool BeginSegment()
{
    if (g_pinch.active)
        return true;

    if (!HasTouchpadDevice() && !CreateTouchpadDevice())
    {
        return false;
    }

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(Const::BaseDistance, p1, p2);

    g_pinch.segmentBaseLogZoom = g_pinch.appliedLogZoom;

    g_pinch.currentDistance = Const::BaseDistance;

    if (!InjectTouchpadFrame(p1, p2, TouchpadFrame::Down))
    {
        return false;
    }

    g_pinch.lastP1 = p1;
    g_pinch.lastP2 = p2;
    g_pinch.active = true;

    const double now = NowSeconds();

    g_pinch.lastTickTime = now;
    g_pinch.lastProgressTime = now;

    return true;
}


// ============================================================================
// Reset
// ============================================================================

void RequestSoftReset()
{
    g_pinch.targetLogZoom = 0.0;
    g_pinch.softResetting = true;

    if (g_pinch.appliedLogZoom <= Const::LogZoomEpsilon)
    {
        g_pinch.appliedLogZoom = 0.0;
        g_pinch.targetLogZoom = 0.0;

        if (g_pinch.active)
            EndActiveSegment();

        g_pinch.softResetting = false;
    }
}


// Per-window state (defined below); a hard reset forgets all of it.
static void ForgetAllWindowZooms();


bool HardResetDevice()
{
    if (g_pinch.active)
        EndActiveSegment();

    DestroyTouchpadDevice();

    g_pinch = PinchState{};
    ForgetAllWindowZooms();

    return CreateTouchpadDevice();
}


// ============================================================================
// Per-window logical zoom
// ============================================================================
//
// The tool cannot read a page's real pinch scale, so it tracks a logical one
// (log zoom, 0 = 100%) and never zooms out below it. With a single value for
// all windows, zooming window A to 200% and then zooming out in window B
// (really at 100%) wasted wheel travel, and back in A the tool believed A
// was at 100% and refused to zoom it out. Each top-level window now has its
// own value. Tabs inside one window still share one -- a tab switch cannot
// be observed from here.
//
// g_pinch always describes the current window. The others are parked in
// g_savedZooms; windows at 100% are not stored.

namespace
{

struct WindowZoom
{
    HWND window = nullptr;
    DWORD pid = 0;  // guards against HWND reuse by another process
    double logZoom = 0.0;
};

}  // namespace

static HWND g_zoomWindow = nullptr;
static DWORD g_zoomWindowPid = 0;
static std::vector<WindowZoom> g_savedZooms;

static constexpr size_t MaxSavedZooms = 64;


static DWORD WindowProcessId(HWND window)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(window, &pid);

    return pid;
}


static void ForgetAllWindowZooms()
{
    g_savedZooms.clear();
    g_zoomWindow = nullptr;
    g_zoomWindowPid = 0;
}


void SelectZoomWindow(HWND window)
{
    if (!window)
        return;

    const DWORD pid = WindowProcessId(window);

    if (window == g_zoomWindow && pid == g_zoomWindowPid)
        return;

    // Leave the previous window where it actually is: lift its contacts and
    // drop the rest of its animation.
    if (g_pinch.active)
        EndActiveSegment();

    if (g_pinch.active)  // lifting failed; the device is in trouble anyway
        ClearActiveSegmentOnly();

    const double previousLogZoom = std::max(0.0, g_pinch.appliedLogZoom);

    if (g_zoomWindow && previousLogZoom > Const::LogZoomEpsilon)
        g_savedZooms.push_back({g_zoomWindow, g_zoomWindowPid, previousLogZoom});

    // Enter the new window with its own value (100% if never zoomed).
    double logZoom = 0.0;

    for (auto it = g_savedZooms.begin(); it != g_savedZooms.end(); ++it)
    {
        if (it->window == window && it->pid == pid)
        {
            logZoom = it->logZoom;
            g_savedZooms.erase(it);
            break;
        }
    }

    // Forget closed windows, and HWNDs that now belong to another process.
    g_savedZooms.erase(
        std::remove_if(
            g_savedZooms.begin(), g_savedZooms.end(), [](const WindowZoom& entry)
            { return !IsWindow(entry.window) || WindowProcessId(entry.window) != entry.pid; }),
        g_savedZooms.end());

    if (g_savedZooms.size() > MaxSavedZooms)
        g_savedZooms.erase(g_savedZooms.begin());  // oldest

    g_pinch.targetLogZoom = logZoom;
    g_pinch.appliedLogZoom = logZoom;
    g_pinch.segmentBaseLogZoom = logZoom;
    g_pinch.softResetting = false;

    g_zoomWindow = window;
    g_zoomWindowPid = pid;
}


// ============================================================================
// Wheel input
// ============================================================================

void ApplyWheelDelta(short wheelDelta, double sensitivityLog)
{
    if (wheelDelta == 0)
    {
        return;
    }

    double notches = static_cast<double>(wheelDelta) / static_cast<double>(WHEEL_DELTA);

    notches = std::clamp(notches, -Const::MaxAbsNotchesPerEvent, Const::MaxAbsNotchesPerEvent);

    const double oldTarget = g_pinch.targetLogZoom;

    g_pinch.targetLogZoom += sensitivityLog * notches;

    g_pinch.targetLogZoom = std::clamp(g_pinch.targetLogZoom, 0.0, MaxLogZoom());

    g_pinch.softResetting = false;
    g_pinch.lastWheelTime = NowSeconds();

    if (std::abs(g_pinch.targetLogZoom - oldTarget) <= Const::LogZoomEpsilon
        && g_pinch.targetLogZoom <= Const::LogZoomEpsilon && notches < 0.0)
    {
        return;
    }

    if (std::abs(g_pinch.targetLogZoom - g_pinch.appliedLogZoom) > Const::LogZoomEpsilon)
    {
        BeginSegment();
    }
}


// ============================================================================
// Smoothing
// ============================================================================

static double CalculateSmoothedDistance(double current, double desired, double dt, bool resetting,
                                        SmoothingCurve curve)
{
    const double diff = desired - current;

    if (std::abs(diff) <= Const::DistanceEpsilon)
    {
        return desired;
    }

    if (curve == SmoothingCurve::Direct)
    {
        return desired;
    }

    const double speedLimit = resetting ? Const::ResetFingerSpeed : Const::MaxFingerSpeed;

    if (curve == SmoothingCurve::Linear)
    {
        const double speed = resetting ? Const::ResetFingerSpeed : Const::LinearFingerSpeed;

        const double step = std::clamp(diff, -speed * dt, speed * dt);

        return current + step;
    }

    double rate = Const::ExponentialRate;

    if (curve == SmoothingCurve::Adaptive)
    {
        const double normalized = std::clamp(std::abs(diff) / Const::AdaptiveFullDiff, 0.0, 1.0);

        rate = Const::AdaptiveRateNear
               + (Const::AdaptiveRateFar - Const::AdaptiveRateNear) * normalized;
    }

    const double alpha = 1.0 - std::exp(-rate * dt);

    double step = diff * alpha;

    step = std::clamp(step, -speedLimit * dt, speedLimit * dt);

    double result = current + step;

    if ((diff > 0.0 && result > desired) || (diff < 0.0 && result < desired))
    {
        result = desired;
    }

    return result;
}


// ============================================================================
// Animation
// ============================================================================

static void RecoverAfterInjectionFailure()
{
    const double preserveApplied = std::max(0.0, g_pinch.appliedLogZoom);

    const double preserveTarget = std::max(0.0, g_pinch.targetLogZoom);

    const bool preserveSoftReset = g_pinch.softResetting;

    DestroyTouchpadDevice();

    PinchState recovered{};

    recovered.appliedLogZoom = preserveApplied;
    recovered.targetLogZoom = preserveTarget;
    recovered.softResetting = preserveSoftReset;

    g_pinch = recovered;

    CreateTouchpadDevice();
}


bool IsPinchActive()
{
    return g_pinch.active;
}


bool PinchNeedsTicks()
{
    return g_pinch.active || g_pinch.softResetting
           || std::abs(g_pinch.targetLogZoom - g_pinch.appliedLogZoom) > Const::LogZoomEpsilon;
}


void TickPinch(SmoothingCurve curve, bool zoomModifierHeld, bool targetAllowed)
{
    const double now = NowSeconds();

    if (!g_pinch.active
        && std::abs(g_pinch.targetLogZoom - g_pinch.appliedLogZoom) <= Const::LogZoomEpsilon)
    {
        if (g_pinch.softResetting)
        {
            g_pinch.targetLogZoom = 0.0;
            g_pinch.appliedLogZoom = 0.0;
            g_pinch.softResetting = false;
        }

        return;
    }

    if (!g_pinch.active)
    {
        if (!BeginSegment())
        {
            // The touchpad could not be (re)created or the first frame was
            // rejected. Drop the pending animation instead of retrying at
            // frame rate; the next wheel event will try again.
            g_pinch.targetLogZoom = g_pinch.appliedLogZoom;
            g_pinch.softResetting = false;
            return;
        }
    }

    double dt = now - g_pinch.lastTickTime;

    g_pinch.lastTickTime = now;

    dt = std::clamp(dt, 0.0, 0.050);

    const double desiredSegmentLogDelta = g_pinch.targetLogZoom - g_pinch.segmentBaseLogZoom;

    const double desiredDistance =
        std::clamp(Const::BaseDistance * std::exp(desiredSegmentLogDelta), Const::MinDistance,
                   Const::MaxDistance);

    const double newDistance = CalculateSmoothedDistance(g_pinch.currentDistance, desiredDistance,
                                                         dt, g_pinch.softResetting, curve);

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(newDistance, p1, p2);

    const bool moved = p1.x != g_pinch.lastP1.x || p1.y != g_pinch.lastP1.y
                       || p2.x != g_pinch.lastP2.x || p2.y != g_pinch.lastP2.y;

    if (moved)
    {
        if (!InjectTouchpadFrame(p1, p2, TouchpadFrame::Update))
        {
            RecoverAfterInjectionFailure();
            return;
        }

        g_pinch.currentDistance = newDistance;

        g_pinch.lastP1 = p1;
        g_pinch.lastP2 = p2;

        const double segmentLog = std::log(g_pinch.currentDistance / Const::BaseDistance);

        g_pinch.appliedLogZoom = std::max(0.0, g_pinch.segmentBaseLogZoom + segmentLog);

        g_pinch.lastProgressTime = now;
    }
    else
    {
        g_pinch.currentDistance = newDistance;
    }

    const bool targetReached =
        std::abs(g_pinch.targetLogZoom - g_pinch.appliedLogZoom) <= Const::LogZoomEpsilon;

    const bool atUpperLimit =
        g_pinch.currentDistance >= (Const::MaxDistance - Const::DistanceEpsilon);

    const bool atLowerLimit =
        g_pinch.currentDistance <= (Const::MinDistance + Const::DistanceEpsilon);

    const bool stillNeedsIn =
        g_pinch.targetLogZoom > g_pinch.appliedLogZoom + Const::LogZoomEpsilon;

    const bool stillNeedsOut =
        g_pinch.targetLogZoom < g_pinch.appliedLogZoom - Const::LogZoomEpsilon;

    if ((atUpperLimit && stillNeedsIn) || (atLowerLimit && stillNeedsOut))
    {
        EndActiveSegment();
        return;
    }

    if (targetReached)
    {
        g_pinch.appliedLogZoom = g_pinch.targetLogZoom;

        // Slow-wheel behavior:
        //
        // While the configured zoom modifier is still held, keep the two
        // synthetic touchpad contacts DOWN even if there has been no wheel
        // event for a long time. Therefore:
        //
        //   hold Alt
        //   wheel one notch
        //   wait 500 ms / 1 s
        //   wheel another notch
        //
        // remains ONE continuous precision-touchpad pinch session.
        //
        // We release when:
        //   - soft reset has completed, or
        //   - the zoom modifier is released/changed, or
        //   - the selected target is no longer a whitelisted application.
        if (g_pinch.softResetting || !zoomModifierHeld || !targetAllowed)
        {
            EndActiveSegment();

            if (g_pinch.targetLogZoom <= Const::LogZoomEpsilon)
            {
                g_pinch.targetLogZoom = 0.0;
                g_pinch.appliedLogZoom = 0.0;
                g_pinch.softResetting = false;
            }

            return;
        }
    }

    // Watchdog is only a fallback for a genuinely orphaned gesture. Do not
    // interrupt a gesture the user is intentionally holding with Alt.
    if (g_pinch.active && !zoomModifierHeld
        && (now - g_pinch.lastProgressTime) > Const::WatchdogSec)
    {
        EndActiveSegment();
    }
}
