#pragma once

// Minimal app-lifecycle interface for built-in (and, eventually, SD-loaded)
// apps. A plain struct of function pointers, not a C++ class/vtable - a
// future native SD-loaded app (see CYDEOS Spec.md's "Native SD app
// system") will need to expose exactly this shape across a dynamic-loader
// boundary, where C++ vtables aren't a portable ABI.
//
// No separate on_draw callback: every screen in this codebase is
// hand-rolled immediate-mode TFT_eSPI drawing (screens redraw inline from
// onStart/onTouch/a background tick, not a distinct draw phase) - forcing
// a split here would fight that existing style for no benefit yet.
//
// Deliberately does NOT model "keep running while another app is in the
// foreground" - see CYDEOS Spec.md's "Execution model": that stays an
// OS-level special case (the Recorder app's background audio capture),
// not a general capability every app gets. An app that needs it exposes
// its own always-on tick function directly, called unconditionally by the
// shell, separately from this interface.
struct CydeosApp {
  const char *name;
  void (*onStart)();             // entered - draw the app's initial screen
  void (*onStop)();              // leaving - app-specific cleanup, if any (may be null)
  void (*onTick)();              // called every loop() iteration while this app is the foreground app (may be null)
  void (*onTouch)(int x, int y); // a touch-down landed inside the content area
};
