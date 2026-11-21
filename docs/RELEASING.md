# Releasing MPC Looper

The local source build is `./build.sh` (Docker linux/amd64). It builds native,
ARMv7 hard-float and AArch64, runs the tests and host simulator, generates the
static skin and validates the complete plugin folder. `./build.sh sanitize`
runs ASan/UBSan checks. Build outputs stay ignored.

For 0.6.1, official catalog tools are pinned to
https://github.com/open-mpc/mpc-vst-plugins/tree/eca4e515ba0f7e16ac57cfd41d9af791bcce32ad

After a clean build, from the catalog tools checkout:

```sh
python3 tools/sdk_release.py '<source>/plugins/MPC Looper Disk/build/sttm - VST - MPC Looper' --version 0.6.1 --id mpc-looper --repo sttm/mpc-looper --license MIT -o <assets> -- --asset-name MPC-Looper --user-data Content --requires 'MPC OS 3.10.1+; writable SD/USB/SSD; preserve the entire Content folder with saved projects.'
python3 tools/sdk_zip.py <assets>/MPC-Looper-0.6.1-mpc-sdk.zip <assets>/MPC-Looper-0.6.1-mpc-armv7.zip <assets>/MPC-Looper-0.6.1-mpc-aarch64.zip
python3 tools/catalog_check.py <assets>/MPC-Looper-0.6.1-mpc-armv7.zip --catalog --require-portable
python3 tools/catalog_check.py <assets>/MPC-Looper-0.6.1-mpc-aarch64.zip --catalog --require-portable
```

Attach the original combined `mpc-looper-0.6.1-mpc.zip`, the three catalog
archives and SHA256SUMS.txt to tag v0.6.1. Never change an already published
archive or reuse its version for a code change. Keep UID SnYq and the original
parameter IDs/order. No user audio, Content files or private device details
belong in source commits or release archives.

MPC OS 3.10.1+ users copy the plugin folder into VST2 Plugins on their drive
and enable Enhancements and External Plugin Support in Preferences. The
legacy scripted installer is optional and is not required for this route.
`--user-data Content` protects existing recordings during catalog upgrades.

Final 0.6.1 and Gen2 hardware testing is pending. Do not add tested.json
until a tester reports that exact version, device, firmware and date.
