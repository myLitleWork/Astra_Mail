#!/bin/sh
set -eu
export LC_ALL=C.UTF-8
run=$(readlink -f "$1")
case "$run" in /tmp/astramail-release.*) ;; *) exit 2 ;; esac
test -f "$run/build-request.json"
mkdir "$run/build" "$run/home-build"
export HOME="$run/home-build"
on_exit() {
    status=$?
    if [ "$status" != 0 ]; then
        for log in "$run/build/configure.log" "$run/build/build.log" "$run/native-tests.log" "$run/packaged-tests.log"; do
            if [ -f "$log" ]; then printf '\n%s\n' "$log"; tail -n 35 "$log"; fi
        done
    fi
    exit "$status"
}
trap on_exit EXIT
python3 "$run/source/scripts/AstraMailRelease.py" verify "$run"
printf 'Configuring Release\n'
(cd "$run/build" && cmake ../source -DCMAKE_BUILD_TYPE=Release -DWITH_SODIUM=ON > configure.log 2>&1)
printf 'Building Release\n'
cmake --build "$run/build" -- -j2 > "$run/build/build.log" 2>&1
printf 'Running isolated native/TCP/PTY regressions\n'
sudo -n unshare -m -n sh "$run/source/scripts/Test-AstraMailRelease.sh" "$run" regression > "$run/native-tests.log" 2>&1
grep 'NATIVE_CHECKS=' "$run/native-tests.log"
printf 'Assembling portable package and checking CSV\n'
python3 "$run/source/scripts/AstraMailRelease.py" prepare "$run"
sudo -n unshare -m -n sh "$run/source/scripts/Test-AstraMailRelease.sh" "$run" package > "$run/packaged-tests.log" 2>&1
cat "$run/packaged-tests.log"
python3 "$run/source/scripts/AstraMailRelease.py" finalize "$run"
printf 'Remote Release completed\n'
