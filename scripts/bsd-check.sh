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
cmake -S . -B build-bsd-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DNEBULA_BUILD_EDITOR=OFF -DNEBULA_WERROR=ON
cmake --build build-bsd-core --parallel 2
ctest --test-dir build-bsd-core --output-on-failure
cmake -S . -B build-bsd -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/local -DNEBULA_WERROR=ON
cmake --build build-bsd --parallel 2
ctest --test-dir build-bsd --output-on-failure
