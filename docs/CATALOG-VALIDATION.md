# MPC Looper 0.6.1 release validation — 2026-11-21

Final source and Gen2 hardware validation: **offline only**. Earlier Looper
iterations were exercised on MPC Key 61 / OS 3.10.1; this is not a final-version
verified record or a CPU guarantee for a 2 GB device. No tested.json is submitted.

- Clean Linux/amd64 Docker build from the standalone source: native, ARMv7
  hard-float and AArch64; all ordinary engine tests and real VST host simulation
  passed. Host simulation: 20 passed, 0 warnings, 0 failures.
- ASan/UBSan pedal and full plugin integration tests passed. The fixture now
  waits for worker completion for file jobs and uses parameter-only snapshots
  for parameter tests before starting the live engine. This removes races
  caused by rendering faster than real time around asynchronous empty loads.
- Source/DSP parity with the user's workspace checked. Fixed UID SnYq and
  first 89 parameter IDs/order retained; 94 parameters total.
- Default length 4 bars; FADER MAX 0 dB, alternative +6 dB. Orange bank bounds
  retained. STOP bounds 90x90, old 16px white icon.
- Final skin preview inspected offline. SDK validator: 0 errors, 0 warnings.
  Fader sprite strip: 96 frames, 60x14400, below 16384px.
- Catalog tools pinned to eca4e515ba0f7e16ac57cfd41d9af791bcce32ad.
  Linux catalog tests: 221 tests OK, 33 skipped; registry: 308 entries,
  6 collections, 0 problems including this entry.
- Both per-CPU archives pass catalog_check --catalog --require-portable.
  ARMv7 has one expected GLIBC 2.36 warning (OS 3.x only); AArch64 has
  0 warnings. No risky-call warnings or trust-field exemptions.
- user_data: [Content] protects recording sources during catalog upgrades.
  Both binaries' ELF machine IDs, archive CRCs, manifest identities, parameter
  defaults and absence of user audio checked. No installed font binaries.

See BUILD-CHECKS.log, PACKAGE-CHECKS.log, TEST-CHECKS.log, SANITIZER-CHECKS.log,
catalog-check-armv7.log/json and catalog-check-aarch64.log/json in this folder.

## Release checksums

```text
42f1aeb82a210657fcc9c017d89e7338af24b3b55c0f3c28a2cc6824d38db762  MPC-Looper-0.6.1-mpc-aarch64.zip
94646e8b350b148fdd8d33eb3bd1fa59404b15122ca2de55ec86da815507f9f2  MPC-Looper-0.6.1-mpc-armv7.zip
eb0da72f91727fa340dd3adfabf63e204f5b5b2df596d68bd9e7b15e034c20c2  MPC-Looper-0.6.1-mpc-sdk.zip
191461393af01ce09d47caea011ff46fb956b150f570134ac8c4c17f8c8192ca  mpc-looper-0.6.1-mpc.zip
```
