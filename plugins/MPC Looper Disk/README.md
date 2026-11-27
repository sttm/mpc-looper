# MPC Looper 0.6.1

A four-loop stereo looper for live performance and production on MPC Standalone.
Record on the MPC beat/bar grid, build layers with overdub, and take each loop
into your arrangement as a separate WAV.

- **Grid-synced recording:** Off / Beat / Bar quantization; Free or 1, 2, 4, 8,
  16 and 32 bars, with 4 bars selected by default.
- **Tempo control:** Sync Tempo follows the MPC BPM; Warp preserves pitch.
- **Live performance:** four independent loops, REC/OVERDUB/PLAY operation,
  undo/redo, grouped launch/stop, PHASE/MEASURE launch and per-loop stop timing.
- **Production:** individual or all-loop separate stereo PCM24 WAV exports.
- **Bounded audio caches:** 16 MiB for four loops; audio sources stream from
  the plugin drive. Warp, UI and other allocations add memory.

MPC OS **3.10.1+** with External Plugin Support. Gen1 ARMv7 and Gen2 AArch64
builds are provided. **Version 0.6.1 tested on MPC Key 61 / MPC OS 3.10.1:**
recording, overdub and project save/reload, confirmed by the author on
2026-11-27. Gen2 and 2 GB MPC One hardware validation remain pending.

## Install

Extract `mpc-looper-0.6.1-mpc.zip` at SD/USB root, keeping the complete folder:

`VST2 Plugins/sttm - VST - MPC Looper/`

Enable Preferences -> Enhancements (Beta) -> Enable Enhancements and External
Plugin Support, then restart MPC. Find External Plugins -> sttm -> MPC Looper
as an insert on an instrument/audio track or submix. Keep the drive connected.
When updating, close the project, preserve the ENTIRE existing Content folder (recording sources, exports and
sessions), replace the plugin files and restart to reload the skin.

## Four independent loops

All four columns are always available, including after recording and when loading
an old two-loop project. There is no 2/4 switch or mode-change engine rebuild.
The historical loop_mode parameter remains in the state schema for compatibility
but does not disable columns or affect recording.

Slots share this instance's stereo input. They are not four separate MPC input
tracks. Route different instruments to separate instances when needed. MPC
inserts AFTER an instance affect its summed loops; separate exported stems can
have separate effects on their later MPC audio tracks.

## Pedal controls

Each column has CLEAR, UNDO, a vertical volume fader, a 90 x 90 STOP button with its unchanged 16-pixel square icon
and a large triangle/circle REC/PLAY control. There are no FX/TRACK controls or
text captions inside the round transport controls. Play and Rec symbols have nominal 22-pixel dimensions; Stop is reduced
to 16 pixels within its smaller button. Q1/LEVEL captions are removed.

The ring is red during capture, amber during overdub, green during playback,
dim during local stop and blue-grey during global pause. These are cached PNG
states, not runtime lighting effects. Static markings use Barlow Semi Condensed;
native value labels use the MPC-supported Titillium Web font.

Default REC ACTION is REC > DUB for each slot:

1. Tap the large button on an empty slot to arm/start capture.
2. Tap again to end capture at the selected grid and immediately enter overdub
   on that recording boundary. Fixed-length capture transitions automatically.
3. Tap again to stop adding live input immediately and enter playback.
4. Further taps alternate PLAY / DUB without resetting the current phase.
5. On a locally stopped loop, the large button launches ordinary playback using PLAYBACK settings;
   it never starts overdub directly from that stopped state.

SETUP -> REC ACTION can instead choose REC > PLAY independently for each slot.
Then the first capture ends in playback and the next tap enters overdub. The
universal Record Selected parameter follows this same sequence.

STOP silences one loop; ALL PLAY / ALL STOP are local plugin controls. UNDO
restores the previous recording/completed overdub/clear and toggles redo.
CLEAR removes the slot and remains undoable until a subsequent edit replaces
history. The old direct Overdub parameter is retained for existing host mappings.

Leaving DUB stops adding input immediately. The remaining unedited tail is
filled incrementally until that pass completes, avoiding a bulk audio-thread
copy. Wait for this completion before export/save or global pause. Starting
capture/dub on a different slot stops adding input to the previous target.

## Length: up to 32 bars

SETUP defaults to 4 bars and offers Free / 1 / 2 / 4 / 8 / 16 / 32 bars. Free lets the next tap define
the length, capped at 32 bars. The cap uses BPM and time signature at the actual
capture start. In 4/4, 32 bars are 64 seconds at 120 BPM or 128 seconds at 60 BPM.
Quantization Off / Beat / Bar is in the header; default Bar.

Audio playback and recording use a background disk worker and fixed prefilled
RAM pools: **16 MiB for four loops**, independent of loop length.
These numbers cover audio caches and write queues, not total plugin memory.
Warp, metadata and MPC's decoded UI textures add memory. Project recall can briefly hold old and new engines. Empty loops skip audio
rendering and do not run spectral Warp processing.

Sources are stored on the plugin's SD/USB/SSD under Content/Sessions. Keep that
drive connected and writable. No source audio is placed in /tmp or the system
partition by the plugin. A 32-bar stereo float source at 120 BPM / 44.1 kHz is
about 21.5 MiB on disk; working versions, undo and saved history take extra space.
A slow or failing drive causes BUFFERING/fade or DISK ERROR rather than blocking
the audio callback. An incomplete overdub is discarded; keep ordinary backups.
The fixed write queue cannot hide an arbitrarily long storage stall.

See MEMORY-DESIGN.md for the actual memory layout and failure behavior. This
build is not yet certified for realtime playback or Warp on a physical MPC One.

## Global MPC transport

Global STOP pauses loop phases and recording clocks. PLAY resumes them. A
return to zero on a stopped-to-playing transition is now distinguished from
late stopped timestamp updates near the held position. With valid host cycle
metadata, an early running return to cycle start restarts phases, while a
natural cycle crossing does not. Recording time is monotonic and does not
reset when host PPQ wraps; launch/record deadlines use distance to the host grid.

VST2 does not identify the physical PLAY START key. A key press exactly at a
natural wrap or an unchanged zero position may be indistinguishable from a
normal transport update. Without cycle metadata, ambiguous running jumps are
kept non-destructive. Confirm the hardware signatures on MPC; GROUP -> RESTART
LOOPS provides an explicit fallback. It resets completed-loop phases without
enabling locally stopped loops or erasing an unfinished take. Tempo sync and
Warp behavior are retained. Changing tempo during an edit retains the existing
finish/abort policy documented below; it is not a transparent live edit stretch.

## Playback modes and stop timing

The PLAYBACK tab contains per-loop settings:

- **Loop Sync ON** (default): PHASE resumes at the current running loop phase
  immediately, without resetting it. The global **Sync Resume** default is PHASE.
  MEASURE instead queues phrase zero for the next MPC measure boundary.
- **Loop Sync OFF**: ordinary playback launches immediately from phrase zero,
  independently of the recording Quantize setting. This is launch/phase sync;
  global Sync BPM/Warp remains a separate audio tempo-processing setting.
- **Play Mode MULTI** (default): loops can play together. **SINGLE** loops exclude
  only other SINGLE loops; MULTI loops remain audible. A queued MEASURE switch
  stops the previous SINGLE only when its replacement actually launches.
- **Stop Mode IMMEDIATE / BEAT / MEASURE**, independently per loop. IMMEDIATE
  is the default, with the existing short anti-click fade. BEAT targets the next
  quarter-note grid line; MEASURE targets the next host bar using its meter.
  Recording Quantize no longer determines ordinary stop timing.

Only one loop takes NEW live input at a time. Switching record/overdub to another
loop closes the previous input overdub and leaves it playing in MULTI. Finishing
its immutable tail can continue internally without adding new input. Returning
to that finishing overdub now closes the other live writer first, preventing
accidental two-loop input recording. SINGLE can additionally silence other
SINGLE playback as described above; this is separate from input ownership.

An unfinished immutable overdub must not be repositioned: a phrase-zero launch
(Loop Sync OFF / MEASURE) can report BUSY until its remaining tail is complete.
It stays stopped and retains its recorded layer; retry after BUSY clears. PHASE
can resume at its current position during closure. This is a disk-engine limit,
not a claim of exact RC-505mkII hardware behavior. Export/undo data stays intact.

The LOOPER wordmark uses the same original outlined geometric lettering family
as AIRLINK. The VST identity remains MPC Looper / sttm / SnYq.

## Grouped launch and stop

GROUP selects any mask of loops 1..4 (initially 1 and 2). Group Play uses one
shared deadline from LOOPS OFF/BEAT/BAR; MEASURE Sync Resume forces a bar when
selected synced loops require it. Free loops in a group use the same explicit
group boundary, then start from zero. Group Stop / All Stop use the latest stop
boundary required by the selected loop settings, so the group stops together.
Default IMMEDIATE stops all selected loops now, including while global transport
is paused. A pending musical boundary waits while the global clock is paused.

Independent touches use native Instant buttons. Real multi-finger dispatch
still depends on the MPC host. A group mask is the reliable single-touch option.
MULTI permits concurrent group playback; selecting several SINGLE loops does
not override their exclusivity (the first selected SINGLE takes priority).
RESTART LOOPS resets completed-loop phases separately. Pending playback shows
ARMED; active record/dub data is not silently discarded on a host sequence wrap.

## Sync Tempo and Warp

Both switches default Off. Sync alone follows host BPM with bandlimited
varispeed, changing pitch with tempo. Warp enables Sync and preserves pitch with
linked-stereo Signalsmith Stretch. Warp Off leaves Sync On; Sync Off disables
both. Both are true tap-to-toggle controls.

Tempo/mode changes close a partial capture and cancel an unfinished overdub
pass, retaining the most recent completed pass. Finish editing before changing
BPM. Completed source samples are not rewritten by tempo changes; a committed
overdub produces a new source at the current tempo and remains undoable.

Warp has a short priming/crossfade transition at start/source change; PRIMING is
shown in SETUP. Dry monitoring has no added Warp latency. Transients, seams and
extreme ratios need listening tests; start around 0.75–1.5 times capture BPM and
one or two playing loops. No commercial DAW Warp equivalence is claimed. See TEMPO-DESIGN.md.

## Latency and recording alignment

The plugin reports zero added VST latency. Dry input monitoring is passed
through at the same sample position; Warp does not delay the dry monitor.
Known loop audio uses circular look-ahead and preroll for the stretcher.
This does not measure or cancel hardware input/output latency, upstream effect
latency, or monitoring delay. No automatic hardware round-trip calibration
or manual recording-offset control is currently implemented. Loop transient
alignment and external-input loopback measurements are 1.0 acceptance work.

## Q-Link and host MIDI Learn

LOOPS/SETUP and Program mode supply four native Q-Link banks, one per loop:

- Q1: output level.
- Q2: next recording length.
- Q3: Rec Action, REC > DUB or REC > PLAY.
- Q4: unassigned. Record is controlled by the large on-screen REC/PLAY button.

Moving level/length or touching the heading also selects the universal target.

CONTROL exposes Selected Loop, Record Selected, Selected Level and Next Loop.
Use MPC's MIDI Learn to map a momentary CC button (127 press / 0 release) and a
fader to these parameters. The effect itself receives no direct MIDI. Absolute
faders may jump on selection changes; no custom pickup mode is provided.

The physical bank button changes the native bank/highlight; its press alone has
no documented VST callback into Selected Loop. The green heading identifies the
universal recording target. Use that heading, Next Loop or a level/length edit
to set the target. Verify host hardware mapping on MPC.

## Export, Arrangement and project recall

Finish REC/DUB first. FILES exports one slot or all non-empty slots as separate
stereo PCM24 WAVs. Original preserves capture timing; Current Tempo (default)
uses current Sync/Warp settings. With Sync Off it exports the original. Each
file is one cycle, with local loop downbeat and its own duration/BPM, not a full
recording of the live performance. Short playback seam fades are baked in.
Post-plugin MPC effects are not included. PCM24 clips outside -1..1.

Files are under Content/Exports/<export-folder>/ in the plugin folder. Browse
there on MPC, load the WAV and place it on an Audio Track in Arrangement, then
repeat/chop/move and add MPC effects. Automatic Arrangement insertion is not
implemented. Very large rendered files beyond standard RIFF size are refused.

**Project storage changed in 0.4.0:** MPC project/preset chunks store parameters
and small relative references to completed audio files, not embedded audio.
Save after finished edits and Disk READY. A save during DUB uses the last
completed version. Undo history is not saved. Do not delete Content/Sessions:
your MPC project needs those source files even after a reboot.

COLLECT SESSION copies completed sources into
Content/Sessions/<instance-id>/Collected/<collection-id>/ and writes
SessionDisk.loop4 beside them as a plugin backup. After collecting, save the MPC
project again to update its references. To move or back up the project, copy the
MPC project **and the complete plugin Content/Sessions folder**, preserving
relative paths. Collect does not make an MPC project self-contained or insert
samples into MPC's project folder. Copying only the project file loses access
to these external recordings.

LOAD BACKUP prefers SessionDisk.loop4 and can still read the old Session.loop4.
Old embedded project/backup audio migrates to files outside the audio callback.
The old backup file is retained for rollback; 0.3.1 cannot read new disk manifests.
Duplicated live instances retain independent backup identities. Sources used
by saved states are retained; there is no automatic saved-history cleanup.

Disk status/cache counters are visible in FILES. mlock is attempted only for
these cache pools; PREFILLED indicates unavailable locking and is not a failure.
The plugin does not change system memory-lock limits, thread priorities or
firmware. This architecture does not by itself prove low overall CPU usage.

## Build and verification

Source: https://github.com/sttm/mpc-looper

With Docker Desktop running, from the repository root:

```
./build.sh
./build.sh sanitize
```

Uses the shared vendored SDK, fonts and header-only Signalsmith dependencies.
Outputs are plugins/MPC Looper Disk/build/sttm - VST - MPC Looper/ and
plugins/MPC Looper Disk/dist/. All required source, SDK, fonts and textures are
vendored. The build does not publish releases automatically.

Core/tempo/pedal/memory/integration tests and the real VST ABI simulator run
before packaging. Hardware CPU, touch/Q-Link behavior, long sessions, transport
flags, audio routing and browser import still require MPC testing. See
VALIDATION.md. First-party code is MIT; dependency/font notices are in
THIRD_PARTY.md. Physical texture originals and prompts are under
assets/physical-v1/; only sized UI sprites are installed.

## Q-Link selection trial (0.6.1)

On LOOPS, each loop header now wraps its native selector button with a
Mouse Down -> Q-Link action targeting that loop's level parameter. Its Data
handle remains Selected Loop; its Bank handle selects the existing host row.
This is the documented touch-to-bank route and needs device confirmation.
Turning a bank's level or length already updates the internal selected loop.

The standalone hardware bank-select press is not a documented callback in this
SDK; changing banks without turning a knob is NOT claimed to select the loop.
Orange bank bounds are deliberately retained until this reverse direction is
confirmed. Do not infer active-loop selection from unrelated host parameter polls.
STOP touch bounds are now 90 x 90, with no overlap with Undo or REC/PLAY; its
white icon keeps the old size. Transport and stop timing are unchanged.

## Fader maximum (0.6.1)

SETUP FADER MAX selects 0 dB (default) or +6 dB as the top of each fader.
The whole fader travel follows this range. Raising the limit preserves current
gains; lowering it clamps gains above 0 dB. It is not a master output boost.
The four native bank level controls use appended UI parameters 90..93; legacy
level and Selected Level MIDI mappings remain available and obey the ceiling.
CONTROL Q2 retains the legacy Selected Level range, so its last portion is
clamped at unity with the 0 dB ceiling. The on-screen fader spans the full range.
Original 89 parameter IDs/order are preserved; five parameters are appended.
On a fresh instance, older projects without a saved ceiling use the 0 dB maximum.

## Release downloads

https://github.com/sttm/mpc-looper/releases/tag/v0.6.1

For copying to MPC OS 3.10.1+ use the combined `mpc-looper-0.6.1-mpc.zip`
(VST2 Plugins layout) or `MPC-Looper-0.6.1-mpc-sdk.zip` (one plugin folder).
The armv7 and aarch64 archives are the per-CPU Open MPC catalog packages.
Preserve the full Content folder during every upgrade or uninstall.
