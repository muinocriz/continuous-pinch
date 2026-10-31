#pragma once

// Windows 11 synthetic precision touchpad device.
//
// Used only on the input thread.

#include "Common.h"

enum class TouchpadFrame
{
    Down,    // first frame of a gesture segment: both contacts touch down
    Update,  // contacts moved
    Up       // both contacts lift
};

bool CreateTouchpadDevice();
void DestroyTouchpadDevice();
bool HasTouchpadDevice();

bool InjectTouchpadFrame(const POINT& p1, const POINT& p2, TouchpadFrame frame);
