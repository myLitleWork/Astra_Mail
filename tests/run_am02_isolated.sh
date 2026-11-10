#!/bin/sh
# Run as sudo -n unshare -m sh run_am02_isolated.sh /absolute/test/workspace
set -eu
workspace=$(readlink -f "$1")
test -f "$workspace/source/tests/test_am02_native.py"
test -d "$workspace/build-am02"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-am02-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
sudo -u "$SUDO_USER" env HOME="$view/home-am02" \
    python3 "$view/source/tests/test_am01_native.py" "$view/build-am02"
exec sudo -u "$SUDO_USER" env HOME="$view/home-am02" \
    python3 "$view/source/tests/test_am02_native.py" "$view/build-am02"
