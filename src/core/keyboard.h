#pragma once

#include <cstddef>

// Generic on-screen QWERTY keyboard - appends into whichever buffer the
// caller points it at via openKeyboardFor(). Not tied to any one screen
// (WiFi password entry today; any future app's own text-entry screen can
// reuse it the same way) - that's the whole point of pulling it into core.
//
// onRedraw() is called after every keystroke/backspace/shift/symbols
// toggle, so the hosting screen can redraw its own text display - the
// keyboard itself doesn't know how the caller wants that text shown
// (masked, revealed, etc.). onDone() is called when "Done" is tapped,
// instead of the keyboard appending a character - the hosting screen
// decides what "done" means (e.g. attempt a WiFi connection).
typedef void (*KeyboardRedrawFn)();
typedef void (*KeyboardDoneFn)();

void openKeyboardFor(char *target, size_t targetSize, int *revealIndex,
                      KeyboardRedrawFn onRedraw, KeyboardDoneFn onDone);

void drawKeyboard();
void handleKeyboardTouch(int x, int y);
