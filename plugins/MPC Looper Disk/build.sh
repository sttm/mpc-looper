#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
case "${1:-all}" in all|native|arm32|arm64|test|sanitize|skin|package) target=${1:-all};; *) echo 'Usage: ./build.sh [all|native|arm32|arm64|test|sanitize|skin|package]' >&2; exit 2;; esac
project_root=$(CDPATH= cd -- ../.. && pwd)
docker build --platform linux/amd64 -t mpclooper-build:bookworm "$project_root"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -v "$project_root:/work" -w '/work/plugins/MPC Looper Disk' mpclooper-build:bookworm make "$target"
