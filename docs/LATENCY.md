# Latency and recording alignment

## What the current code does

MPC Looper reports initialDelay = 0 to the VST2 host. The dry monitor reads
and writes the same input sample in processReplacing; it does not add a FIFO
or wait for disk writes. Read-ahead caches prepare stored loop audio in RAM.

Warp uses circular source look-ahead, Signalsmith input/output latency values
and a preroll/crossfade path. This is intended to align stored loop playback
without delaying the live dry monitor. It is not an external I/O calibrator.

## Measured offline, 2026-11-27

The actual native VST2 binaries for runtime versions 601 and 602 were loaded
through VSTPluginMain. At 44.1/48 kHz and blocks of 32/128/512, with Warp off
and on and no recorded loops, both stereo dry impulses appeared at their
original input sample index. All 24 scenarios passed; reported latency was 0.
This tests dry passthrough and declared VST latency only. It does not establish
Warp transient phase, physical input/output latency or host delay compensation.

## Remaining measurements

- Internal MPC source click -> loop capture -> exported WAV: check transient
  position relative to the host beat/bar for all supported quantize modes.
- Stored percussive material with Warp off/on and tempo changes: check attacks,
  seams and startup/crossfade alignment, not only frequency or duration.
- A separate output-to-input loopback path with return monitoring disabled:
  measure the complete hardware route, different host buffer/rate settings
  and latency-producing upstream insert effects. Avoid a feedback route.
- Establish what MPC already compensates before applying a recording offset,
  so compensation is not counted twice.

No automatic round-trip measurement or user recording-offset parameter is
currently implemented. If a systematic offset is demonstrated, add per-route
recording calibration with the required pre/post-roll handling and verify
overdub alignment and WAV exports. Do not delay every track with a made-up
VST initialDelay value to compensate for an external input route.
