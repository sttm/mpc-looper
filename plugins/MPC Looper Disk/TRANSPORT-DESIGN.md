# MPC Looper — transport and simultaneous loop control

Design baseline reviewed against 0.4.3; experimental implementation 0.5.0 now
implements held-position transport classification, host-grid deadlines,
quantized playback intent snapshots and grouped launch/stop. Physical-button
discrimination and native multitouch still require MPC verification.
Identity remains sttm / MPC Looper / SnYq; existing parameter IDs/order and
saved audio must remain compatible in the implementation.

## Previous implementation and observed symptoms

HostTimeline.h marks pending on ANY stopped cursor delta and consumes it on
resume. Its stop transition instead establishes a new baseline, even when the
host moves its cursor to zero on that transition. This can misclassify a late
stopped timestamp update as restart and can miss a return-to-start received on
the stop transition itself. It is a plausible explanation of the reported
inversion, not a proven MPC event trace.

MPCLooper.cpp derives beat_ from an initial host anchor, then integrates BPM.
Engine::restart() zeroes complete-loop positions while deliberately preserving
unfinished record/dub work. Running host jumps currently NEVER restart loops;
this protects recording across short song sequence cycles but also ignores
Play Start while already running. Native command inputs are separate atomic
masks per slot, but are consumed serially per audio block. UI uses native
Instant Button widgets; multi-finger dispatch is owned by MPC, not the VST.

## Required behavior

- Global Stop: freeze playback phase, recording time and pending quantized
  deadlines together; retain which loops are locally enabled.
- Global Play: resume from the frozen loop phases. No reset due to a cached or
  delayed stopped-position update. Resume follows host tempo and grid phase.
- Global Play Start: reset completed loop phases to zero together, without
  automatically enabling locally stopped/muted loops. Pending starts are
  realigned. Active record/dub data must not silently disappear.
- Natural sequence wrap: advance local loop time continuously. A four-bar
  capture/playback must survive repeated two-bar host cycles.
- Local Stop: mute that loop while the musical clock and phase keep advancing,
  preserving the existing phase-coherent live-loop workflow. Global Stop is
  the pause operation. Local launch/stop uses chosen Off/Beat/Bar quantization.
- Sync Tempo controls resampling speed with pitch change; Warp additionally
  preserves pitch. Tempo tracking and launch quantization use host BPM/meter
  regardless of whether audio time stretching is enabled.

## Timeline separation

1. Raw host observations: playing, PPQ/sample position, tempo, time signature,
   cycle bounds/active flag and transport-change flag when available (0.5.0 does not add this flag to the SDK).
2. Monotonic musical clock: advances only while global transport runs; unwraps
   normal cycles and uses host phase for calculating the next grid boundary.
   Pending recording deadlines must NEVER be compared to raw wrapping PPQ.
3. Per-loop phase: each loop has its own cursor and length. Host song position
   does not continuously overwrite this cursor. Explicit restart is a separate
   command, not an incidental consequence of grid synchronization.

At a launch request, calculate distance to the chosen future host grid line,
then express the deadline in monotonic local time. A cycle or tempo change must
not shorten a recording merely because raw PPQ became smaller. Any phase
correction must preserve already-recorded audio and pending edit intent.

## Restart classification and limits

Preserve last running/expected block-end position separately from observations
received while stopped. At resume, compare the actual start position with the
held position using a sample/block-scale tolerance, not arbitrary absolute
1e-6 PPQ movement. Running discontinuities require checking the expected cycle
wrap using cycle bounds and projected block progress before considering an
explicit reposition. A transport-changed flag alone is not restart evidence:
VST2 permits setting it for other transport changes too.

VST2 has no Play Start button identifier. A manual restart and a natural cycle
can generate indistinguishable timing observations, especially at a cycle
boundary. Likewise, pressing Play Start at a zero position while stopped may
produce the same data as plain Play there. These cases cannot be perfectly
classified by DSP alone. Preserve natural-cycle safety over an uncertain reset;
validate real MPC traces before claiming physical-key discrimination. Provide
an explicit parameter/UI restart command as an unambiguous fallback. Do not
promise guaranteed inference from raw VST timestamps.

## Two/three loops with one musical boundary

Snapshot all per-slot command intents before mutating any slot. Queue individual
loop launch/stop intents onto the same Off/Beat/Bar scheduler. Requests received
before one target boundary commit on the same audio sample. This supports real
multitouch IF the MPC host forwards the independent touches; separate native
buttons do not establish that support on their own.

A grouped launch/stop is required as a reliable single-touch alternative:
select a bit mask of loops 1..4, then Group Play or Group Stop queues one atomic
transaction containing that mask and a single target deadline. A mask supports
any pair/triple, not only all four. Group actions can also be exposed as host
parameters for host MIDI Learn. Existing all-loop actions remain compatible.
Do not attempt to infer a group by a delayed timer after the first touch: that
adds arbitrary latency and can put commands on different bar boundaries.

## Verification before a test package

- Play -> Stop -> Play at nonzero position preserves phase.
- Same sequence with stopped PPQ cached/late by one block does not reset.
- Stop -> Play Start resets once, including zero first reported on Stop.
- Play Start while running resets outside an expected natural wrap.
- Repeated host cycles shorter than a loop never reset/shorten it.
- Armed REC crosses a natural wrap and records the complete selected length.
- Sync and Warp at changing BPM, non-4/4 meter and changed cycle bounds.
- Two/three simultaneous launch/stop intents share one deadline and sample.
- Group mask touches use a single command; unselected loops are unchanged.
- Disk/Warp restart remains allocation-, lock- and filesystem-free in audio.
- State restoration does not trigger a false restart or erase captures.
- Real MPC confirms Play/Play Start timestamp signatures and native multitouch;
  automated host tests are not substitutes for these hardware observations.

Sources:
- Local SDK docs/host-contract.md and framework/mpc_plugin.h (current transport
  exposes no physical-button identity and currently omits transportChanged).
- Steinberg VST 2.0 specification, VstTimeInfo flags description:
  https://www.cosc.brocku.ca/Offerings/4P98/assignments/vst20spec.pdf
- Akai MPC manuals entry point: https://www.akaipro.com/mpc-manuals/
