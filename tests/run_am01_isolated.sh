#!/bin/sh
# Run as: sudo -n unshare -m sh run_am01_isolated.sh /absolute/test/workspace
# The workspace must contain source/ and build/ from an isolated Debug build.
set -eu
workspace=$(readlink -f "$1")
test -f "$workspace/source/tests/test_am01_native.py"
test -d "$workspace/build"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-am01-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
exec sudo -u "$SUDO_USER" env HOME="$view/home" \
    python3 "$view/source/tests/test_am01_native.py" "$view/build"
