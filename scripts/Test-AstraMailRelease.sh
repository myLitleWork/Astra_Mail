#!/bin/sh
set -eu
workspace=$(readlink -f "$1")
test -d "$workspace/build"
test -n "${SUDO_USER:-}"
view=$(mktemp -d /var/tmp/astramail-release-view.XXXXXXXX)
mount --make-rprivate /
mount --bind "$workspace" "$view"
mount -t tmpfs -o mode=1777 tmpfs /tmp
ip link set lo up
if [ "$2" = installer ]; then
    mount --bind "$view/package" "$view/package"
    mount -o remount,bind,ro,noexec "$view/package"
    sudo -u "$SUDO_USER" env HOME="$view/home-test" ASTRAMAIL_TEST_NOEXEC=1 \
        python3 "$view/source/tests/test_installer.py" "$view/package"
elif [ "$2" = installed ]; then
    sudo -u "$SUDO_USER" env HOME="$view/home-test" PYTHONPATH="$view/source/tests" \
        python3 "$view/source/scripts/Test-AstraMailPackaged.py" "$view/build" "$view/package" "$view/installed-tests.json" --installed
elif [ "$2" = package ]; then
    sudo -u "$SUDO_USER" env HOME="$view/home-test" PYTHONPATH="$view/source/tests" \
        python3 "$view/source/scripts/Test-AstraMailPackaged.py" "$view/build" "$view/package" "$view/packaged-tests.json"
else
    for suite in am01 am02 am03 am06 am07 am08 am09 am11; do
        sudo -u "$SUDO_USER" env HOME="$view/home-test" \
            python3 "$view/source/tests/test_${suite}_native.py" "$view/build"
    done
fi
