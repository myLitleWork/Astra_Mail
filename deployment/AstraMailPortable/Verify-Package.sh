#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$app_dir"
sha256sum -c package-checksums.sha256
expected=$(cat version.txt)
actual=$(sh ./Run-AstraMail.sh --version)
test "$actual" = "$expected"
printf 'AstraMail %s: package verified\n' "$actual"
