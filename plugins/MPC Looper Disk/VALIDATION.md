# MPC Looper 0.6.1 validation

Release for MPC OS 3.10.1+ with External Plugin Support.
Same MPC Looper / sttm / SnYq identity. First 76 parameter IDs/order unchanged.
New indices: Loop Sync 76..79 (ON), Play Mode 80..83 (MULTI), Resume 84 (PHASE),
Stop Timing 85..88 (IMMEDIATE). Q-Link bank roles remain level/length/REC ACTION; level now maps to 90..93.
PLAYBACK is the sixth native tab; LOOPER wordmark shares the AIRLINK code family.

Input overdub handoff closes other live writers even when returning to a
previous slot whose immutable tail is still closing. Tests inspect samples
from both versions to prove new input does not leak into the other loop.
Only NEW input ownership is exclusive; old tails may continue copying without
input and must not be confused with two active overdubs.

PHASE is the user-selected default and starts current-phase playback now.
MEASURE starts zero on the next MPC bar. Free Loop Sync OFF starts zero now
when the immutable source is ready. SINGLE excludes other SINGLE loops only,
leaving MULTI playback intact. Queued launches commit before mixing any slot.

Stop IMMEDIATE / BEAT (quarter note) / MEASURE is per loop and independent of
record Quantize. Timed stops close ongoing recording/overdub on that boundary;
new input continues until then and is excluded after the exact target sample. Immediate group/all stop commits while global transport is
paused; beat/bar deadlines freeze. Group stops share the latest selected stop
boundary, and grouped SINGLE loops still honor exclusivity.

Safety limit: a requested phrase-zero seek during unfinished immutable DUB
returns BUSY instead of changing the copy cursor and corrupting the layer.
The stopped loop can be retried after closure; PHASE can resume during closure.
The sample-level test verifies preserved layer data after this condition.
This is not a full hardware emulation of every RC-505mkII option.

Transport detection and protection of four-bar recordings across two-bar host
cycles remain from 0.5.0. VST2 does not identify a Play Start key; exact-boundary
or zero-position cases still need hardware confirmation. Explicit Restart Loops
is available. No realtime allocation, lock, host notification or disk I/O is
added; allocation guards and existing disk tests remain active.

Release checks are retained in the public repository docs/ directory.
Host simulation does not certify actual MPC CPU/touch/multitouch/transport.
The final host simulation passed 20 checks with 0 warnings and 0 failures.
Parameter restore and restored audio are checked independently.

Preserve the ENTIRE Content folder and test a copy of an MPC project. Looper is
validated on MPC Key 61 / OS 3.10.1 for recording, overdub and project
save/reload (author confirmation, 2026-11-27). Gen2 remains offline-only.
The root tested.json records only this 0.6.1 / Key 61 confirmation.

Behavior reference: Roland RC-505mkII Parameter Guide, pages 2-4:
https://static.roland.com/assets/media/pdf/RC-505mk2_Parameter_eng04_W.pdf
Stop BEAT/MEASURE and default PHASE are project/user choices, not copied source.

0.6.1 UI trial: loop headers use native custom-component Mouse Down -> Q-Link
actions, Bank mapped to the loop's level while Data remains parameter 51.
Hardware touch propagation through the native child button must be verified.
Rotating level/length selects the internal loop; pressing bank select alone
is not detectable via the currently documented SDK contract. Orange bounds
remain as the actual bank indicator. No fabricated focus/bank state is shown.

Stop art and touch bounds are 90x90. The icon is drawn at the same 2x source
size as 0.6.0 before scaling, preserving its 16px nominal size. No per-loop
DSP/transport/stop-timing behavior changed. Original 89 parameters are intact.

Recording length defaults to 4 bars in all four slots. Parameter 89 is FADER MAX
(default 0 dB, alternative +6 dB); 90..93 are normalized native UI faders.
Tests cover gain preservation when raising the ceiling, clamp when reducing,
full-range native values and project restore including the selected loop.

Release packaging reduces the fader strip to 96 frames at 150px (14400px tall),
below the catalog 16384px texture recommendation. Control parameter precision
and 90x90 STOP geometry are unchanged. The final skin is offline-previewed.
