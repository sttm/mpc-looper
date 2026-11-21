# Tempo rendering design — 0.4.0

## Model

VST2 supplies host BPM through the SDK transport. It does not prescribe the
resampling or time-stretch algorithm. Each immutable captured source stores its
sample count N and capture BPM B0. At host BPM B, playback advances by r=B/B0
source frames per output frame; a cycle lasts N/r frames. Fractional phase is
carried across block and loop boundaries, rather than rounding each cycle.
Sync Off uses r=1. Missing host tempo falls back to 120 BPM.

## Two rendering paths

Sync alone uses a 32-tap Blackman-windowed sinc interpolator with 256 fractional
phases and 64 low-pass cutoff banks. Cutoff is lowered when downsampling. The
kernels are shared and prepared off the audio thread. This is deliberately
bounded computation; 32 taps do not guarantee high stop-band rejection at extreme
ratios. Varispeed pitch follows f_out=f_in*r. Exact unity integer reads bypass FIR.

Warp uses Signalsmith Stretch 1.3.2, a spectral phase-vocoder approach with linked
stereo channels and its default window configuration. Its splitComputation mode
and 64-output-frame work quanta distribute FFT work. All persistent DSP state and
worst-case peak scratch capacity are allocated during engine construction.
Circular source access supplies look-ahead from the already recorded loop.

On start/source discontinuity, the renderer seeks the spectral state, discards
output preroll incrementally, then crossfades for 256 samples. During priming it
plays source audio at original pitch. This avoids doing the complete preroll in
one real-time callback but creates a short timing/texture transition. It is not
zero-latency spectral analysis. Dry input monitoring has no inserted Warp delay.
Changed tempo updates the seek alignment at a work-quantum boundary. Unity Warp
output uses the original samples while retaining warm spectral state.

No algorithm is universally optimal for drums, sustained instruments and mixed
music. Phase-vocoder processing can smear transients or alter stereo texture;
large ratios need listening tests. The upstream author's suggested moderate
range, approximately 0.75–1.5, is a sensible first hardware test range. No promise
of equivalence to Ableton's proprietary Warp algorithms is made.

## Editing and storage

Tempo/mode changes never destructively render completed source loops. Export
can preserve the original or render exactly one cycle at current tempo; Warp
export performs its preroll off the audio thread and streams bounded chunks.

A changed tempo/mode closes a partial first recording and cancels the unfinished
portion of an overdub, preserving the most recent completed pass. An overdub
commits the rendered cycle plus live input at the current BPM, with the previous
source retained for undo. This conservative policy avoids re-indexing a mutable
partly written overdub across incompatible cycle lengths. Finish recording and
overdub before changing tempo. Source capture is capped at 32 bars; fixed audio caches and background disk I/O
are described in MEMORY-DESIGN.md. Slower playback/export may last longer.

Production prepares four buffer pools and four spectral renderers. Empty loops
skip rendering. Two-slot configurations remain only in reference tests. Project
recall can temporarily retain old and new engines.

## Evidence and limits

Automated tests render a 440 Hz stereo tone at ratios 0.75, 1.25 and 1.5. Varispeed
measures approximately 330, 550 and 660 Hz; Warp stays within 3 Hz of 440 Hz.
Tests also check stereo agreement, phase advancement, finite bounded output,
source immutability during tempo ramps, no heap allocations in rendering,
stopped-renderer inactivity, and exported frame counts/frequencies.

Docker x86_64 measurements for two/four simultaneously playing Warp loops show
approximately linear scaling. These are development-machine measurements,
including emulation/scheduling overhead, not MPC performance certification.
The recommended first test is two loops, then compare Sync and Warp on actual
Gen1 hardware with the editor both open and closed. Listen to transients and
loop seams, test rapid BPM changes, project recall, and long sessions. Four Warp
loops may require more CPU headroom than the device can provide.

## Primary references

- Signalsmith Stretch: https://github.com/Signalsmith-Audio/signalsmith-stretch
- Signalsmith Linear: https://github.com/Signalsmith-Audio/linear
- Signalsmith author, Four Ways To Write A Pitch-Shifter (ADC22):
  https://www.youtube.com/watch?v=fJUmmcGKZMI
- Driedger and Mueller, TSM toolbox / time-scale modification research:
  https://www.audiolabs-erlangen.de/resources/MIR/TSMtoolbox/
- Rubber Band technical comparison (not included in this plugin):
  https://www.breakfastquay.com/rubberband/technical.html

MIT third-party source pins and the small scratch-capacity patch are documented
in THIRD_PARTY.md. No Steinberg SDK or proprietary MPC firmware code is required.

## Pedal action and transport additions (0.3.0)

REC actions are independent per slot. Immediate overdub can start at a fractional
source phase without resetting it; the spare buffer is written circularly from
the corresponding output-cycle offset. Leaving DUB stops addition immediately;
remaining source frames fill the unpublished tail incrementally, then publish
atomically. The previous source stays available for undo. No bulk copy or
allocation is added to the audio callback. Natural capture handoff uses the
next sample for overdub, not the last sample of the first capture twice.

Local stop keeps advancing phase. Global host pause returns monitored input
plus a bounded fade of the last loop mix, without advancing source positions,
recording or overdub counters. Resume ramps the loop mix back in and retains
local intent. Full host arrangement seeking is not implemented.
