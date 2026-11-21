#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "./plugins/MPC Looper Disk/build.sh" "$@"
