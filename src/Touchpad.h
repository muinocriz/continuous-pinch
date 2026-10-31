#pragma once

// Windows 11 synthetic precision touchpad device.

#include "Common.h"

extern HSYNTHETICPOINTERDEVICE g_touchpad;

bool CreateTouchpadDevice();
void DestroyTouchpadDevice();
