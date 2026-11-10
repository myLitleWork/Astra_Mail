#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# ZIP extracted on Windows may not retain Unix executable bits.
chmod u+x "$app_dir/AstraMail"
LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$app_dir/AstraMail" "$@"
