#include "Pinch.h"

#include "Config.h"
#include "Target.h"
#include "Touchpad.h"
#include "Util.h"

// ============================================================================
// Pinch state
// ============================================================================

PinchState g_pinch;


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


static bool InjectTouchpadFrame(const POINT& p1, const POINT& p2, bool contactsActive)
{
    if (!g_touchpad)
        return false;

    POINTER_TYPE_INFO infos[2]{};
    const POINT points[2] = {p1, p2};

    for (UINT32 i = 0; i < 2; ++i)
    {
        auto& info = infos[i];

        auto& pointerInfo = info.touchInfo.pointerInfo;

        info.type = PT_TOUCHPAD;

        pointerInfo.pointerId = i;

        pointerInfo.pointerFlags =
            contactsActive
                ? (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_CONFIDENCE)
                : POINTER_FLAG_CONFIDENCE;

        pointerInfo.ptHimetricLocation = points[i];

        pointerInfo.dwTime = g_pinch.syntheticTime;
    }

    if (!InjectSyntheticPointerInput(g_touchpad, infos, 2))
    {
        return false;
    }

    g_pinch.syntheticTime += Const::AnimationIntervalMs;

    return true;
}


static void ClearActiveSegmentOnly()
{
    g_pinch.active = false;

    g_pinch.currentDistance = Const::BaseDistance;
}


bool EndActiveSegment()
{
    if (!g_pinch.active)
        return true;

    if (!InjectTouchpadFrame(g_pinch.lastP1, g_pinch.lastP2, false))
    {
        return false;
    }

    ClearActiveSegmentOnly();

    return true;
}


static bool BeginSegment()
{
    if (g_pinch.active)
        return true;

    if (!g_touchpad && !CreateTouchpadDevice())
    {
        return false;
    }

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(Const::BaseDistance, p1, p2);

    g_pinch.segmentBaseLogZoom = g_pinch.appliedLogZoom;

    g_pinch.currentDistance = Const::BaseDistance;

    if (!InjectTouchpadFrame(p1, p2, true))
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


bool HardResetDevice()
{
    if (g_pinch.active)
        EndActiveSegment();

    DestroyTouchpadDevice();

    g_pinch = PinchState{};

    return CreateTouchpadDevice();
}


// ============================================================================
// Wheel input
// ============================================================================

void ApplyWheelDelta(short wheelDelta)
{
    if (wheelDelta == 0 || !g_enabled.load(std::memory_order_relaxed))
    {
        return;
    }

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    double notches = static_cast<double>(wheelDelta) / static_cast<double>(WHEEL_DELTA);

    notches = std::clamp(notches, -Const::MaxAbsNotchesPerEvent, Const::MaxAbsNotchesPerEvent);

    const double oldTarget = g_pinch.targetLogZoom;

    g_pinch.targetLogZoom += cfg.sensitivityLog * notches;

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
// Current modifier state (also used by the animation state machine)
// ============================================================================

UINT CurrentModifierMaskForState()
{
    UINT mask = MM_NONE;

    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mask |= MM_ALT;

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mask |= MM_CTRL;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mask |= MM_SHIFT;

    return mask;
}


static bool ZoomModifierStillHeld()
{
    UINT expected = MM_ALT;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);
        expected = g_cfg.zoomModifiers;
    }

    return CurrentModifierMaskForState() == expected;
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


void TickPinch()
{
    // Refresh the selected target mode (hover by default, or foreground).
    // Root HWND caching prevents repeated process-name lookups at 125 Hz.
    RefreshTargetAllowed();

    const double now = NowSeconds();

    if (!g_enabled.load(std::memory_order_relaxed))
    {
        if (g_pinch.active)
            EndActiveSegment();

        return;
    }

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
            return;
    }

    double dt = now - g_pinch.lastTickTime;

    g_pinch.lastTickTime = now;

    dt = std::clamp(dt, 0.0, 0.050);

    const double desiredSegmentLogDelta = g_pinch.targetLogZoom - g_pinch.segmentBaseLogZoom;

    const double desiredDistance =
        std::clamp(Const::BaseDistance * std::exp(desiredSegmentLogDelta), Const::MinDistance,
                   Const::MaxDistance);

    AppConfig cfg;

    {
        std::lock_guard<std::mutex> lock(g_cfgMutex);

        cfg = g_cfg;
    }

    const double newDistance = CalculateSmoothedDistance(g_pinch.currentDistance, desiredDistance,
                                                         dt, g_pinch.softResetting, cfg.curve);

    POINT p1{};
    POINT p2{};

    FingerPositionsForDistance(newDistance, p1, p2);

    const bool moved = p1.x != g_pinch.lastP1.x || p1.y != g_pinch.lastP1.y
                       || p2.x != g_pinch.lastP2.x || p2.y != g_pinch.lastP2.y;

    if (moved)
    {
        if (!InjectTouchpadFrame(p1, p2, true))
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

        const bool zoomModifierHeld = ZoomModifierStillHeld();

        const bool targetStillAllowed = g_targetAllowed.load(std::memory_order_relaxed);

        // V3.1 slow-wheel behavior:
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
        if (g_pinch.softResetting || !zoomModifierHeld || !targetStillAllowed)
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
    if (g_pinch.active && !ZoomModifierStillHeld()
        && (now - g_pinch.lastProgressTime) > Const::WatchdogSec)
    {
        EndActiveSegment();
    }
}
