# MPC One / 2 GiB baseline — proposed disk backend

Historical design proposal. The experimental 0.4.0 implementation is described
in MEMORY-DESIGN.md; its hardware acceptance tests remain outstanding.
Current 0.3.1 is growing-RAM playback with background export/backup. Its 320 MiB
sample-byte limit excludes temporary old/new engines and serialized audio chunks.
It is not a 2 GiB hardware compatibility certification.

## Verified facts and calculations

Akai lists MPC One/One+, Live/Live II, X and Key 37 as 2 GB Gen1 models; Key 61
and X SE as 4 GB. Live II Retro should not be assumed to have 4 GB.
https://support.akaipro.com/en/support/solutions/articles/69000866055-akai-pro-mpc-series-low-memory

Stereo float32 at 44.1 kHz consumes 352800 bytes/second. 2 MiB holds 5.94 s;
4 MiB holds 11.89 s. At 48 kHz the figures are 5.46/10.92 s. Cache budgets
must count input/write-behind and output/read-ahead separately.

The actual 0.3.1 ARM32 and ARM64 binaries require GLIBC 2.36 at most and have no
GLIBCXX requirements. The vendored SDK documents MPC OS 3 glibc >=2.39 and allows
bookworm GCC builds. Exact compiler identity is not necessary; the requested
symbol versions/ABI must be supported by the host.
https://github.com/inmusic-software/mpc-plugin-builder-sdk/blob/main/docs/building.md

Current skin PNGs total 32.86 MiB if every unique image is decoded once to RGBA.
This is a file-dimension estimate, not measured MPC renderer/RSS usage. Host
lazy loading, sharing and duplication must be measured on hardware.

## Proposed fixed working set

Keep the user's 2/4-loop mode and default 2. For each active loop, initially
budget 2 MiB read-ahead/cache plus 2 MiB write-behind. Total I/O audio cache:
8 MiB for two loops, 16 MiB for four. Loop length must not grow these caches.
Source head/tail and Warp seek/look-ahead pages must fit within the read budget.

Add measured Warp state, shared sinc kernels, queues/metadata and decoded UI.
Aim near 64 MiB added working memory per instance, subject to measurement;
64-96 MiB is an initial engineering envelope, not a current guarantee. Validate
save/recall, undo and mode changes as well as steady playback. No hidden complete
loop copies during snapshots or reconfiguration.

## Storage and real-time boundary

Use Content/Sessions/<session-id>/Working under the installed plugin folder on
the user's SD/USB/SSD. VST2 does not provide this plugin with an MPC project-path
API; do not guess a project folder. Do not use /tmp or firmware/system storage.

A dedicated I/O worker uses pread/pwrite and initially 128 KiB chunks; compare
64/128/256 KiB experimentally on real media. Handle short transfers, EINTR,
ENOSPC, read-only/removal and slow cards. Do not fsync each audio chunk; commit
immutable versions at appropriate non-audio boundaries. Sequential work has
priority over opportunistic backup copies.
https://man7.org/linux/man-pages/man2/pread.2.html

Use separate SPSC directions for dirty audio/write descriptors and ready cache
pages. Read request/ready queues carry loop identity, source generation, offset
and frame count. Audio reads only ready, pinned RAM pages. No disk mmap access
or synchronous cache-miss read in processReplacing. Frame counters/host transport
supply timing without a new per-sample clock query.

Warp/resampling needs coherent look-ahead and circular access, not just a FIFO.
Cache both ends across wrap, retain an immediately usable loop head, and reject
stale page generations after clear/undo/source replacement. Test rate changes
and source switching before enabling disk-backed Warp.

Queue overflow/cache starvation must never silently overwrite pending audio.
Stop accepting a new recording cleanly or fade a playback underrun, preserve
last committed versions, and report DISK SLOW/READ MISS/WRITE ERROR counters.
Do not block the audio thread while waiting for storage.

## Undo and project persistence

Undo is a previous immutable loop version on disk plus prefetched RAM windows.
A small ring cannot contain an entire long undo version. Publish a new source
only when its version is coherent; never overwrite the only good source.

For bounded peak RAM, project chunks should store a small durable manifest of
saved immutable audio versions, not embed all samples into multiple vectors.
This changes portability: provide Collect/Export Session to copy the manifest
and every referenced source. Existing embedded-audio chunks must remain readable
through non-audio migration. Saved references must not be garbage-collected
merely because the current live undo target changed.

## Memory locking and host behavior

Prefault owned buffers before audio use. Attempt mlock only on small owned RAM
ranges during initialization; inspect/report failure and operate safely without
claiming locked residency. RLIMIT_MEMLOCK/capabilities are host-process limits.
Never call mlockall, raise host limits, change scheduling globally or require
root/firmware modifications from a plugin.
https://man7.org/linux/man-pages/man2/mlock.2.html

Do not assume every target firmware uses PREEMPT_RT or a particular /tmp/swap
configuration. The audio boundary must remain nonblocking independently of those
assumptions. Filesystem block alignment alone does not certify SD latency.

## Acceptance tests before calling this One-compatible

- Fixed allocated audio-cache bytes with 2/4 loops and 1/8/32-bar sources.
- Artificial 50/250/1000+ ms read/write stalls, short I/O, full/read-only media.
- Loop wrap, first handoff, immediate overdub, undo/redo, clear and generation races.
- Global pause/resume, varispeed and Warp with editor open/closed.
- No audio allocation, file syscall, waiting lock or synchronous cache fill.
- Peak memory during project save/recall/migration and mode changes; collect/reopen
  a session from a different user drive without lost references.
- 30+ minutes on an actual 2 GB MPC One with an ordinary loaded project, real
  SD/USB media, multiple instances and downstream effects. Desktop benchmarks
  cannot establish this hardware result.
