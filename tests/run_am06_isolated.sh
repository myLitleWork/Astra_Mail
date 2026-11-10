#!/bin/sh
set -eu
workspace=$(readlink -f "$1")
test -f "$workspace/source/tests/test_am06_native.py"
test -d "$workspace/build-am06"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-am06-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
ip link set lo up
for suite in am01 am02 am03 am06; do
    sudo -u "$SUDO_USER" env HOME="$view/home-am06" \
        python3 "$view/source/tests/test_${suite}_native.py" "$view/build-am06"
done
