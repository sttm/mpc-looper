# MPC Looper 1.0 acceptance plan

Current public version: 0.6.1. Local optimization build: 0.6.2. The version
number alone does not certify stability or hardware coverage.

## Existing foundation

- Four independent loops; beat/bar quantized or free capture, up to 32 bars.
- Host tempo sync/Warp, launch/stop modes, group transport and overdub/undo.
- Separate stereo WAV stems, disk-backed audio and bounded cache pools.
- 0.6.1 recording/overdub/project save/reload confirmed on Key 61 / OS 3.10.1.
- 0.6.2 optimizations passed native/ARM builds, regression tests and sanitizers
  offline; that does not extend 0.6.1's hardware confirmation to 0.6.2.

## Required before a 1.0 release

1. Measure internal capture timing, physical loopback offsets and Warp transient
   alignment. Add recording-offset calibration only if the measured routes need
   it. See LATENCY.md. Preserve attacks at loop seams.
2. Verify global Stop/Play/Play Start, natural sequence wraps and long loops on
   MPC. Document host cases VST2 cannot distinguish and retain explicit Restart.
3. Set and test a predictable policy for BPM/mode changes during REC/OVERDUB.
   Currently they can finish a partial capture and discard an unfinished overdub
   pass while preserving the last completed version.
4. Verify persistence after restart, multiple instances, undo, exports and a
   collected session copied to another drive. Keep all referenced audio.
5. Stress-test four loops, recording plus playback/export, UI open/closed, slow
   media, and 30-60 minute sessions. Measure peak block time, RAM trend, read
   misses and errors on Key 61; obtain separate Gen2 and 2 GB device results.
6. Complete final UI/install documentation and publish an exact-version device
   record. Preserve plugin UID, parameter IDs/order and existing projects.

## Agreed next feature

Per-loop menu: Normal / Half / Double speed, plus an independent Reverse
toggle. Disable changes during REC/OVERDUB; block entering those edit modes
until direction is forward and speed is x1. Specify which processed playback
modes WAV export renders. Test switching, phase, seams, transport and undo.
This feature is planned and is not present in 0.6.1 or the local 0.6.2 build.

New effects do not substitute for the timing, persistence and stress checks.
