#!/bin/sh
# A private network namespace: packet-loss rules cannot affect SSH or other apps.
set -eu
workspace=$(readlink -f "$1")
test -f "$workspace/source/tests/test_am03_native.py"
test -d "$workspace/build-am03"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-am03-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
ip link set lo up
for suite in am01 am02 am03; do
    sudo -u "$SUDO_USER" env HOME="$view/home-am03" \
        python3 "$view/source/tests/test_${suite}_native.py" "$view/build-am03"
done
