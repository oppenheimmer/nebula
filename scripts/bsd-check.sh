#!/bin/sh
# Run only inside an isolated native BSD machine/VM, with its dependencies ready.
set -eu
case "$(uname -s)" in
    FreeBSD|OpenBSD) ;;
    *) printf '%s\n' 'BSD checks require an isolated native FreeBSD or OpenBSD environment.' >&2; exit 2 ;;
esac
export PATH="/usr/local/bin:/usr/local/sbin:$PATH"
export LANG=en_US.UTF-8
export QT_QPA_PLATFORM=offscreen
export XDG_CACHE_HOME="${TMPDIR:-/tmp}/nebula-cache"
uname -a
c++ --version
cmake --version
sqlite3 -version
case "$(uname -s)" in
    FreeBSD) pkg info qt6-base ;;
    OpenBSD) pkg_info | grep 'qt6-qtbase' ;;
esac
mode=${1:-test}
case "$mode" in test|transfer) ;; *) exit 2 ;; esac
if [ "$mode" = test ]; then
    cmake -S . -B build-bsd-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DNEBULA_BUILD_EDITOR=OFF -DNEBULA_WERROR=ON
    cmake --build build-bsd-core --parallel 2
    ctest --test-dir build-bsd-core --output-on-failure
fi
cmake -S . -B build-bsd -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/local -DNEBULA_WERROR=ON
if [ "$mode" = test ]; then
    cmake --build build-bsd --parallel 2
    ctest --test-dir build-bsd --verbose
    rm -rf build-bsd/portability-output
    build-bsd/nebula_package_fixture bundle "$PWD/build-bsd/portability-output"
else
    cmake --build build-bsd --parallel 2 --target nebula_package_fixture
    stage=$(mktemp -d "$PWD/build-bsd/portability-check.XXXXXX")
    trap 'rm -rf "$stage"' EXIT
    count=0
    for source in "$PWD"/build-portability-inputs/portability-*; do
        test -d "$source"
        build-bsd/nebula_package_fixture transfer "$source" "$stage/$(basename "$source")"
        count=$((count + 1))
    done
    test "$count" -eq 3
fi
