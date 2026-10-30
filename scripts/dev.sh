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
       ./scripts/dev.sh backup SOURCE_FOLDER NEW_BACKUP_FOLDER
       ./scripts/dev.sh restore BACKUP_FOLDER NEW_WORKSPACE_FOLDER
       ./scripts/dev.sh export-md WORKSPACE_FOLDER NEW_MARKDOWN_FOLDER
       ./scripts/dev.sh import-md MARKDOWN_FOLDER WORKSPACE_FOLDER DATABASE_ID
       ./scripts/dev.sh run [--import-root HOST_FOLDER] [prototype options]
       ./scripts/dev.sh shell

Qt and development packages are installed inside the image only.
Source is mounted read-only; build output uses build-container-<compiler>/.
Tests and benchmarks run with networking disabled and an offscreen Qt backend.
Run shares the X11 display socket and a single authorization file explicitly.
Run asks for a workspace folder; --workspace HOST_FOLDER selects one directly.
--import-root exposes one existing host folder read-only at /imports for file import.
The chooser sees your home read-only. Only the chosen folder is mounted writable.
Run rebuilds the executable automatically. --demo opens the editor without a workspace.
HELP
}

mount_spec() {
    local source_path=${1//\"/\"\"}
    local target_path=${2//\"/\"\"}
    printf 'type=bind,"source=%s","target=%s"' "$source_path" "$target_path"
    [[ ${3:-} != readonly ]] || printf ',readonly'
    return 0
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
    run|test-native|shell|backup|restore|export-md|import-md) compiler=gcc ;;
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
     --mount "$(mount_spec "$project_dir" /workspace readonly)"
     --mount "$(mount_spec "$build_dir" /build)"
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
    backup|restore|export-md)
        if (( $# != 3 )); then usage >&2; exit 2; fi
        source_folder=$(CDPATH= cd -- "$2" && pwd -P)
        destination_parent=$(dirname -- "$3")
        destination_name=$(basename -- "$3")
        if [[ "$destination_name" == . || "$destination_name" == .. ]]; then printf 'Choose a new destination folder.\n' >&2; exit 2; fi
        mkdir -p -- "$destination_parent"
        destination_parent=$(CDPATH= cd -- "$destination_parent" && pwd -P)
        case "$destination_parent/$destination_name" in
            "$source_folder"|"$source_folder"/*)
                printf 'Choose a destination outside the source workspace.\n' >&2; exit 2 ;;
        esac
        "${run[@]}" "$image" sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON && cmake --build /build --parallel 4'
        exec "${run[@]}" --mount "$(mount_spec "$source_folder" /source readonly)" \
            --mount "$(mount_spec "$destination_parent" /output)" "$image" \
            /build/nebula_workspace "$command_name" /source "/output/$destination_name"
        ;;
    import-md)
        if (( $# != 4 )); then usage >&2; exit 2; fi
        source_folder=$(CDPATH= cd -- "$2" && pwd -P)
        workspace_folder=$(CDPATH= cd -- "$3" && pwd -P)
        "${run[@]}" "$image" sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON && cmake --build /build --parallel 4'
        exec "${run[@]}" --mount "$(mount_spec "$source_folder" /source readonly)" \
            --mount "$(mount_spec "$workspace_folder" /data)" "$image" \
            /build/nebula_workspace import-md /source /data "$4"
        ;;
    run|test-native)
        auth_file=${XAUTHORITY:-${HOME}/.Xauthority}
        if [[ -z ${DISPLAY:-} || ! -d /tmp/.X11-unix || ! -r "$auth_file" ]]; then
            printf 'GUI launch needs DISPLAY, the X11 socket, and a readable XAUTHORITY file.\n' >&2
            exit 1
        fi
        # Rebuild here too: running a stale binary must not hide source changes.
        "${run[@]}" "$image" sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON && cmake --build /build --parallel 4'
        gui=("${run[@]}" --env QT_QPA_PLATFORM=xcb --env "DISPLAY=$DISPLAY"
            --env XAUTHORITY=/run/nebula-xauthority
            --mount type=bind,source=/tmp/.X11-unix,target=/tmp/.X11-unix,readonly
            --mount "$(mount_spec "$auth_file" /run/nebula-xauthority readonly)")
        if [[ "$command_name" == test-native ]]; then
            exec "${gui[@]}" "$image" sh -ec '/build/editor_tests && /build/desktop_tests'
        fi
        host_workspace=
        host_import_root=
        skip_picker=false
        app_args=()
        shift
        while (( $# )); do
            case "$1" in
                --import-root)
                    if (( $# < 2 )); then printf '%s\n' '--import-root needs an existing host folder.' >&2; exit 2; fi
                    host_import_root=$2
                    [[ -n "$host_import_root" ]] || { printf '%s\n' '--import-root needs an existing host folder.' >&2; exit 2; }
                    shift 2 ;;
                --import-root=*)
                    host_import_root=${1#--import-root=}
                    [[ -n "$host_import_root" ]] || { printf '%s\n' '--import-root needs an existing host folder.' >&2; exit 2; }
                    shift ;;
                --workspace)
                    if (( $# < 2 )); then printf '%s\n' '--workspace needs a host folder.' >&2; exit 2; fi
                    host_workspace=$2
                    [[ -n "$host_workspace" ]] || { printf '%s\n' '--workspace needs a host folder.' >&2; exit 2; }
                    shift 2 ;;
                --workspace=*)
                    host_workspace=${1#--workspace=}
                    [[ -n "$host_workspace" ]] || { printf '%s\n' '--workspace needs a host folder.' >&2; exit 2; }
                    shift ;;
                --demo|--smoke-test|--screenshot|--benchmark|--help|-h)
                    skip_picker=true; app_args+=("$1"); shift ;;
                *) app_args+=("$1"); shift ;;
            esac
        done
        if [[ -z "$host_workspace" && "$skip_picker" == false ]]; then
            host_workspace=$("${gui[@]}" --env "NEBULA_USER_HOME=$HOME" \
                --mount "$(mount_spec "$HOME" "$HOME" readonly)" \
                "$image" /build/nebula_prototype --select-workspace)
            [[ -n "$host_workspace" ]] || exit 0
        fi
        if [[ -n "$host_import_root" ]]; then
            [[ "$host_import_root" != '~' ]] || host_import_root=$HOME
            [[ "$host_import_root" != '~/'* ]] || host_import_root="$HOME/${host_import_root:2}"
            host_import_root=$(CDPATH= cd -- "$host_import_root" && pwd -P)
            gui+=(--mount "$(mount_spec "$host_import_root" /imports readonly)")
        fi
        if [[ -n "$host_workspace" ]]; then
            [[ "$host_workspace" != '~' ]] || host_workspace=$HOME
            [[ "$host_workspace" != '~/'* ]] || host_workspace="$HOME/${host_workspace:2}"
            mkdir -p -- "$host_workspace"
            host_workspace=$(CDPATH= cd -- "$host_workspace" && pwd -P)
            exec "${gui[@]}" --mount "$(mount_spec "$host_workspace" /data)" "$image" \
                /build/nebula_prototype --workspace /data --workspace-display "$host_workspace" "${app_args[@]}"
        fi
        exec "${gui[@]}" "$image" /build/nebula_prototype "${app_args[@]}"
        ;;
esac
