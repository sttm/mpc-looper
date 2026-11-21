MPC Plugin Builder SDK: inMusic Software, MIT.
Pinned vendored SDK commit: febd2ca5bd9eb16c4a412837196e145566bc981b.
https://github.com/inmusic-software/mpc-plugin-builder-sdk
The SDK implements the VST2 ABI and MPC native JSON UI. SDK-LICENSE.txt is included.

Research reference: Slow Ops Looper, Copyright 2026 Slow Ops, MIT.
https://github.com/drj-io/SlowOps.MPC.Looper
Reviewed commit: 3b53eadb385a74dae215646d1f49a8e4c226b884.
The new four-slot engine, immutable snapshot/state/export mechanism and UI are
original code. No Slow Ops source files or skin artwork are copied into this plugin.

Signalsmith Stretch 1.3.2, MIT, Signalsmith Audio Ltd.
https://github.com/Signalsmith-Audio/signalsmith-stretch
Pinned commit: 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da.
Modification: peak scratch-vector capacity is reserved to bands instead of
bands/2 at configuration, preventing growth for pathological spectra in audio.
SIGNALSMITH-STRETCH-LICENSE.txt is included.

Signalsmith Linear 0.6.6, MIT, Signalsmith Audio Ltd.
https://github.com/Signalsmith-Audio/linear
Pinned commit: 7bb1c8f36794d6980080ce817c91f8da8fe56a66.
Used for FFT/STFT by Stretch. SIGNALSMITH-LINEAR-LICENSE.txt is included.

Physical skin source textures (2026-11-20): generated using the built-in OpenAI
image_gen tool. No external product photographs or BOSS skin assets are copied.
Original images and generation prompts are preserved in assets/physical-v1/.
Only sized UI PNGs are included in the installed package.

Barlow Semi Condensed, Copyright 2017 The Barlow Project Authors, SIL OFL 1.1.
https://github.com/google/fonts/tree/main/ofl/barlowsemicondensed
SemiBold and Medium are used offline to print the skin markings. No TTF
binaries are installed. ARTWORK-FONT-LICENSE.txt contains the Barlow notice.


0.6.0 LOOPER wordmark: original project geometric outline drawing code, derived
from the AIRLINK wordmark in this workspace. No proprietary font or logo is used.
Playback terminology was checked against the public Roland parameter guide;
no firmware, artwork or DSP implementation from BOSS/Roland is included.
