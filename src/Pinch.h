#pragma once

// Pinch gesture state machine: wheel input, smoothing, segments, reset.

#include "Common.h"

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

    DWORD syntheticTime = 1;

    double lastWheelTime = 0.0;
    double lastTickTime = 0.0;
    double lastProgressTime = 0.0;
};

extern PinchState g_pinch;

bool EndActiveSegment();
void RequestSoftReset();
bool HardResetDevice();
void ApplyWheelDelta(short wheelDelta);
void TickPinch();

UINT CurrentModifierMaskForState();
