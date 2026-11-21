# Experimental disk backend — 0.4.1

Production playback uses DiskAudio.h; AudioMemory.h remains only for legacy
reference tests. Per active loop, one 4 MiB aligned allocation holds 12 read
pages (1.5 MiB), first/last pages for three immutable source versions (0.75 MiB),
and 14 SPSC write pages (1.75 MiB). Pages are 128 KiB / 16384 stereo frames.
Production always prepares four loops: 16 MiB of audio pools. The two-loop
backend remains available only to reference tests. No full-loop heap arrays
are allocated during project serialization, disk playback or WAV export.

This is not total RSS: Signalsmith Warp, cache metadata, shared C++/host state,
page tables, Linux filesystem cache and skin decoding add memory. The 167
unique UI PNGs have about 31.87 MiB of decoded RGBA pixels; host behavior can
change actual use. Old/new engines coexist briefly during project recall or a sample-rate change.
No total-RSS or physical MPC One performance claim has been measured yet.

The audio producer writes preallocated SPSC packets and onset windows. A
normal-priority worker performs short-I/O/EINTR-safe pread/pwrite, fdatasync and
rename. Completed files have a validated 4 KiB header. Retired read pages use
an audio-block epoch before recycling; audio never waits for the worker.
64-frame preflight covers FIR/Warp lookaround. Missing pages cause a 5 ms fade,
phase continues, and playback fades back when ready. Incomplete DUB is aborted
on a cache/write failure; the last completed snapshot remains recoverable.
A full write queue rejects further input rather than blocking or growing RAM.

Each version has an immutable unique file. Saves pin a committed source and
serialize relative paths. Collected backups copy sources in bounded chunks.
The source reader for WAV export has four pages (512 KiB) and runs separately
from realtime disk I/O. Legacy embedded chunks migrate off the audio thread.
Saved files are retained; unsaved superseded versions are removed on reuse or
teardown. Deleting saved histories safely requires a future reference-aware GC.

The plugin Content directory is the only data root. Paths reject absolute and
parent traversal and resolve symlinks before use. The user must install on a
writable SD/USB/SSD; the plugin does not silently fall back to /tmp. It does not
promise power-loss durability of a filesystem directory transaction.

mlock is attempted outside audio on these pools only. Failure is allowed and
reported as PREFILLED; pages are touched beforehand. No mlockall, altered
RLIMIT, realtime priority changes, mmap disk faults in audio or firmware edits.
Static assertions require lock-free 32-bit atomics and 64-bit off_t. RT code
has no file calls or allocation; tests trap both. File-worker exceptions set
an error rather than escaping std::thread and terminating the host.

Four pools and Warp renderers are prepared; empty loops skip spectral processing.
Multiple instances multiply memory costs. SD latency/throughput, ongoing
MPC project workload and CPU headroom must be tested on the actual hardware.
