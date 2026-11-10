#!/bin/sh
set -eu
workspace=$(readlink -f "$1")
test -f "$workspace/source/tests/test_am09_native.py"
test -d "$workspace/build-am09"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-am09-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
ip link set lo up
suites="am01 am02 am03 am06 am07 am08 am09"
if [ "${2:-}" = "am09" ]; then suites="am09"; fi
for suite in $suites; do
    sudo -u "$SUDO_USER" env HOME="$view/home-am09" \
        python3 "$view/source/tests/test_${suite}_native.py" "$view/build-am09"
done
