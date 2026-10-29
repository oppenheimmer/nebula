#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=nebula-dev:debian13
command_name=${1:-help}
compiler=${2:-gcc}

usage() {
    cat <<'HELP'
Usage: ./scripts/dev.sh build-image
       ./scripts/dev.sh test [gcc|clang|core]
       ./scripts/dev.sh benchmark [single|separate]
       ./scripts/dev.sh test-native
       ./scripts/dev.sh run [prototype options]
       ./scripts/dev.sh shell

Qt and development packages are installed inside the image only.
Source is mounted read-only; build output uses build-container-<compiler>/.
Tests and benchmarks run with networking disabled and an offscreen Qt backend.
Run shares the X11 display socket and a single authorization file explicitly.
HELP
}

if [[ "$command_name" == help || "$command_name" == --help || "$command_name" == -h ]]; then
    usage
    exit 0
fi

if [[ "$command_name" == build-image ]]; then
    exec docker build --file "$project_dir/Dockerfile.dev" --tag "$image" "$project_dir"
fi

case "$command_name" in
    test)
        case "$compiler" in
            gcc) cxx=g++; editor=ON ;;
            clang) cxx=clang++; editor=ON ;;
            core) cxx=g++; editor=OFF ;;
            *) usage >&2; exit 2 ;;
        esac
        ;;
    benchmark)
        mode=${2:-single}
        [[ "$mode" == single || "$mode" == separate ]] || { usage >&2; exit 2; }
        compiler=gcc
        ;;
    run|test-native|shell) compiler=gcc ;;
    *) usage >&2; exit 2 ;;
esac

if ! docker image inspect "$image" >/dev/null 2>&1; then
    printf 'Development image unavailable. Check Docker access and run ./scripts/dev.sh build-image\n' >&2
    exit 1
fi

build_dir="$project_dir/build-container-$compiler"
mkdir -p "$build_dir"
run=(docker run --rm --init --network=none --cap-drop=ALL
     --security-opt=no-new-privileges --pids-limit=256
     --user "$(id -u):$(id -g)" --read-only
     --tmpfs /tmp:rw,nosuid,nodev,size=256m,mode=1777
     --mount "type=bind,source=$project_dir,target=/workspace,readonly"
     --mount "type=bind,source=$build_dir,target=/build"
     --workdir /workspace)

case "$command_name" in
    test)
        exec "${run[@]}" --env "NEBULA_CXX_COMPILER=$cxx" --env "NEBULA_BUILD_EDITOR=$editor" "$image" \
            sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_COMPILER="$NEBULA_CXX_COMPILER" -DNEBULA_BUILD_EDITOR="$NEBULA_BUILD_EDITOR" \
                && cmake --build /build --parallel 4 \
                && ctest --test-dir /build --output-on-failure'
        ;;
    benchmark)
        exec "${run[@]}" "$image" /build/nebula_prototype --benchmark "$mode" --blocks 1000
        ;;
    shell)
        exec "${run[@]}" -it "$image" /bin/bash
        ;;
    run|test-native)
        auth_file=${XAUTHORITY:-${HOME}/.Xauthority}
        if [[ -z ${DISPLAY:-} || ! -d /tmp/.X11-unix || ! -r "$auth_file" ]]; then
            printf 'GUI launch needs DISPLAY, the X11 socket, and a readable XAUTHORITY file.\n' >&2
            exit 1
        fi
        executable=/build/nebula_prototype
        [[ "$command_name" != test-native ]] || executable=/build/editor_tests
        exec "${run[@]}" --env QT_QPA_PLATFORM=xcb --env "DISPLAY=$DISPLAY" \
            --env XAUTHORITY=/run/nebula-xauthority \
            --mount type=bind,source=/tmp/.X11-unix,target=/tmp/.X11-unix,readonly \
            --mount "type=bind,source=$auth_file,target=/run/nebula-xauthority,readonly" \
            "$image" "$executable" "${@:2}"
        ;;
esac
