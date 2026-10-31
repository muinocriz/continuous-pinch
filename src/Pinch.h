#pragma once

// Pinch gesture state machine: wheel input, smoothing, segments, reset.
//
// All functions run on the input thread. The engine does not read the
// global configuration or the keyboard itself: the input thread passes in
// what it needs, so the engine stays single-threaded and testable.

#include "Common.h"

// Adds wheel notches to the target zoom and starts a segment if needed.
void ApplyWheelDelta(short wheelDelta, double sensitivityLog);

// Smoothly returns to the logical 100% baseline.
void RequestSoftReset();

// Lifts any active contacts and recreates the synthetic touchpad.
bool HardResetDevice();

// Lifts the contacts of the active segment, if any.
void EndActiveSegment();

// Advances the animation by one frame.
//   zoomModifierHeld : the configured zoom modifiers are currently held
//   targetAllowed    : the current target window is whitelisted
void TickPinch(SmoothingCurve curve, bool zoomModifierHeld, bool targetAllowed);

// True while contacts are down.
bool IsPinchActive();

// True while TickPinch() still has work to do (contacts down, an animation
// in progress, or a soft reset finishing). When false the frame timer can
// be stopped.
bool PinchNeedsTicks();
