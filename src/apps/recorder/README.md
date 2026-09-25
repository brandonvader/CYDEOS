# Recorder app (built-in)

Not yet implemented (M1). Will hold the recorder UI, free-space bar,
spectrum visualizer, the Recordings browser, and transcription upload
(Scriberr first, with room for other backends later) as screens/tabs of
one app — including that app's own transcription-service settings screen,
which intentionally does **not** live in OS-level Settings (see the
rationale note in `CYDEOS Spec.md`'s "Built-in apps" section).

Ported from CYD-Voice-Recorder's `src/cyd/main.cpp`, restructured against
the app-lifecycle interface (`on_start`/`on_stop`/`on_tick`/`on_touch`/
`on_draw`) defined for the native SD app system, so built-in and
SD-loaded apps share one internal contract.
