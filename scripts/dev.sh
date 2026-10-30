#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=nebula-dev:debian13
command_name=${1:-help}
compiler=${2:-gcc}

usage() {
    cat <<'HELP'
Usage: ./scripts/dev.sh build-image
       ./scripts/dev.sh build-desktop-image
       ./scripts/dev.sh test-desktop
       ./scripts/dev.sh test [gcc|clang|core|asan]
       ./scripts/dev.sh stress [REPEATS]
       ./scripts/dev.sh fuzz [SECONDS_PER_TARGET]
       ./scripts/dev.sh format [--check]
       ./scripts/dev.sh benchmark [single|separate|workspace]
       ./scripts/dev.sh test-native
       ./scripts/dev.sh backup SOURCE_FOLDER NEW_BACKUP_FOLDER
       ./scripts/dev.sh restore BACKUP_FOLDER NEW_WORKSPACE_FOLDER
       ./scripts/dev.sh export-md WORKSPACE_FOLDER NEW_MARKDOWN_FOLDER
       ./scripts/dev.sh import-md MARKDOWN_FOLDER WORKSPACE_FOLDER DATABASE_ID
       ./scripts/dev.sh export-json WORKSPACE_FOLDER NEW_EXPORT_FOLDER
       ./scripts/dev.sh import-json EXPORT_FOLDER NEW_WORKSPACE_FOLDER
       ./scripts/dev.sh run [--import-root HOST_FOLDER] [prototype options]
       ./scripts/dev.sh shell

Qt and development packages are installed inside the image only.
Source is mounted read-only; build output uses build-container-<compiler>/.
Tests and benchmarks run with networking disabled and an offscreen Qt backend.
Run shares the X11 display socket and a single authorization file explicitly.
Run asks for a workspace folder; --workspace HOST_FOLDER selects one directly.
The normal workspace GUI has network access for text previews on explicit URL paste.
Builds, tests, the chooser, demo and smoke runs, and data commands remain offline.
--import-root exposes one existing host folder read-only at /imports for file import.
The chooser sees your home read-only. Only the chosen folder is mounted writable.
Run rebuilds the executable automatically. --demo opens the editor without a workspace.
Data commands write into a private staging folder beside the destination, then rename it.
They refuse uncommitted source changes unless NEBULA_ALLOW_DIRTY=1 is set.
HELP
}

mount_spec() {
    local source_path=${1//\"/\"\"}
    local target_path=${2//\"/\"\"}
    printf 'type=bind,"source=%s","target=%s"' "$source_path" "$target_path"
    [[ ${3:-} != readonly ]] || printf ',readonly'
    return 0
}

# Data commands rebuild the tool from this checkout. Refuse uncommitted source
# changes so user data is not processed by unreviewed code (override for development).
require_reviewed_tree() {
    if git -C "$project_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        if [[ -n $(git -C "$project_dir" status --porcelain --untracked-files=no -- src include CMakeLists.txt 2>/dev/null) ]] \
            && [[ ${NEBULA_ALLOW_DIRTY:-} != 1 ]]; then
            printf 'Source files have uncommitted changes. Commit them, or set NEBULA_ALLOW_DIRTY=1 to process data with this build.\n' >&2
            exit 2
        fi
    else
        printf 'Warning: not a Git checkout; data commands use this source tree as is.\n' >&2
    fi
}

if [[ "$command_name" == help || "$command_name" == --help || "$command_name" == -h ]]; then
    usage
    exit 0
fi

if [[ "$command_name" == build-image ]]; then
    exec docker build --file "$project_dir/Dockerfile.dev" --tag "$image" "$project_dir"
fi
if [[ "$command_name" == build-desktop-image ]]; then
    exec docker build --file "$project_dir/Dockerfile.dev" --target desktop-check \
        --build-arg "TEST_UID=$(id -u)" --build-arg "TEST_GID=$(id -g)" \
        --tag nebula-desktop-check:debian13 "$project_dir"
fi

case "$command_name" in
    test)
        build_type=Release; sanitize=OFF
        case "$compiler" in
            gcc) cxx=g++; editor=ON ;;
            clang) cxx=clang++; editor=ON ;;
            core) cxx=g++; editor=OFF ;;
            asan) cxx=g++; editor=ON; build_type=Debug; sanitize=ON ;;
            *) usage >&2; exit 2 ;;
        esac
        ;;
    stress)
        repeats=${2:-20}
        [[ "$repeats" =~ ^[0-9]+$ && "$repeats" -gt 0 ]] || { usage >&2; exit 2; }
        compiler=gcc
        ;;
    benchmark)
        mode=${2:-single}
        [[ "$mode" == single || "$mode" == separate || "$mode" == workspace ]] || { usage >&2; exit 2; }
        compiler=gcc
        ;;
    fuzz)
        seconds=${2:-30}
        [[ "$seconds" =~ ^[0-9]+$ && "$seconds" -gt 0 ]] || { usage >&2; exit 2; }
        compiler=fuzz
        ;;
    format)
        [[ $# == 1 || ( $# == 2 && $2 == --check ) ]] || { usage >&2; exit 2; }
        compiler=gcc
        ;;
    test-desktop)
        [[ $# == 1 ]] || { usage >&2; exit 2; }
        compiler=gcc; image=nebula-desktop-check:debian13
        ;;
    run|test-native|shell|backup|restore|export-md|import-md|export-json|import-json) compiler=gcc ;;
    *) usage >&2; exit 2 ;;
esac

if ! docker image inspect "$image" >/dev/null 2>&1; then
    printf 'Development image unavailable. Check Docker access and run ./scripts/dev.sh build-image\n' >&2
    exit 1
fi

build_dir="$project_dir/build-container-$compiler"
mkdir -p "$build_dir"
pid_limit=256; tmp_size=256m
if [[ "$command_name" == test-desktop ]]; then pid_limit=512; tmp_size=512m; fi
run=(docker run --rm --init --network=none --cap-drop=ALL
     --security-opt=no-new-privileges --pids-limit="$pid_limit"
     --user "$(id -u):$(id -g)" --read-only
     --tmpfs "/tmp:rw,nosuid,nodev,size=$tmp_size,mode=1777"
     --mount "$(mount_spec "$project_dir" /workspace readonly)"
     --mount "$(mount_spec "$build_dir" /build)"
     --workdir /workspace)

case "$command_name" in
    test-desktop)
        exec "${run[@]}" "$image" sh -ec '
            cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
                -DNEBULA_BUILD_EDITOR=ON -DNEBULA_WERROR=ON -DNEBULA_SANITIZE=OFF -DNEBULA_LIBFUZZER=OFF
            cmake --build /build --parallel 4 --target desktop_tests editor_tests
            mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
            chmod 700 "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
            exec xvfb-run -a -s "-screen 0 1280x900x24 -nolisten tcp" \
                dbus-run-session -- sh /workspace/scripts/desktop-check.sh'
        ;;
    format)
        format_args=(-i)
        source_mode=
        if [[ ${2:-} == --check ]]; then format_args=(--dry-run --Werror); source_mode=readonly; fi
        mapfile -d '' -t sources < <(git -C "$project_dir" ls-files -z --cached --others --exclude-standard -- '*.cpp' '*.hpp')
        (( ${#sources[@]} > 0 )) || { printf 'No Git-listed C++ source files found.\n' >&2; exit 2; }
        exec docker run --rm --init --network=none --cap-drop=ALL \
            --security-opt=no-new-privileges --user "$(id -u):$(id -g)" --read-only \
            --mount "$(mount_spec "$project_dir" /workspace "$source_mode")" \
            --workdir /workspace "$image" clang-format "${format_args[@]}" "${sources[@]}"
        ;;
    test)
        # Leak checking needs ptrace, which the restricted container does not grant.
        exec "${run[@]}" --env "NEBULA_CXX_COMPILER=$cxx" --env "NEBULA_BUILD_EDITOR=$editor" \
            --env "NEBULA_BUILD_TYPE=$build_type" --env "NEBULA_SANITIZE=$sanitize" \
            --env "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1" --env "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1" "$image" \
            sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE="$NEBULA_BUILD_TYPE" \
                -DCMAKE_CXX_COMPILER="$NEBULA_CXX_COMPILER" -DNEBULA_BUILD_EDITOR="$NEBULA_BUILD_EDITOR" \
                -DNEBULA_SANITIZE="$NEBULA_SANITIZE" -DNEBULA_WERROR=ON \
                && cmake --build /build --parallel 4 \
                && ctest --test-dir /build --output-on-failure'
        ;;
    stress)
        # Repeat the interaction suites to expose timing-dependent failures (BUG-014).
        exec "${run[@]}" --env "NEBULA_REPEATS=$repeats" "$image" \
            sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON \
                && cmake --build /build --parallel 4 \
                && ctest --test-dir /build --output-on-failure -R "editor|desktop|pasted_link_preview|trash_safety" --repeat until-fail:"$NEBULA_REPEATS"'
        ;;
    fuzz)
        exec "${run[@]}" --env "NEBULA_FUZZ_SECONDS=$seconds" \
            --env ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
            --env UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$image" sh -ec '
                cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Debug \
                    -DCMAKE_CXX_COMPILER=clang++ -DNEBULA_LIBFUZZER=ON -DNEBULA_WERROR=ON
                cmake --build /build --parallel 4 --target nebula_libfuzzer nebula_fuzz
                /build/nebula_fuzz --seed-dir /build/corpus
                mkdir -p /build/fuzz-logs /build/fuzz-artifacts
                for target in wiki codec manifest json markdown; do
                    NEBULA_FUZZ_TARGET=$target /build/nebula_libfuzzer /build/corpus/$target \
                        -max_total_time="$NEBULA_FUZZ_SECONDS" -timeout=10 -max_len=65536 \
                        -rss_limit_mb=1024 -artifact_prefix=/build/fuzz-artifacts/ \
                        > /build/fuzz-logs/$target.log 2>&1 || { cat /build/fuzz-logs/$target.log; exit 1; }
                    tail -n 2 /build/fuzz-logs/$target.log
                done'
        ;;
    benchmark)
        "${run[@]}" "$image" sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON && cmake --build /build --parallel 4' >&2
        exec "${run[@]}" "$image" /build/nebula_prototype --benchmark "$mode" --blocks 1000
        ;;
    shell)
        exec "${run[@]}" -it "$image" /bin/bash
        ;;
    backup|restore|export-md|export-json|import-json)
        if (( $# != 3 )); then usage >&2; exit 2; fi
        require_reviewed_tree
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
        if [[ -e "$destination_parent/$destination_name" || -L "$destination_parent/$destination_name" ]]; then
            printf 'Choose a new destination folder; %s already exists.\n' "$destination_parent/$destination_name" >&2; exit 2
        fi
        "${run[@]}" "$image" sh -ec 'cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++ -DNEBULA_BUILD_EDITOR=ON && cmake --build /build --parallel 4'
        # Mount only a new private staging folder, never the destination's parent
        # (often the home directory). Rename the result into place on success.
        staging=$(mktemp -d "$destination_parent/.nebula-staging.XXXXXX")
        chmod 700 "$staging"
        status=0
        "${run[@]}" --mount "$(mount_spec "$source_folder" /source readonly)" \
            --mount "$(mount_spec "$staging" /output)" "$image" \
            /build/nebula_workspace "$command_name" /source "/output/$destination_name" || status=$?
        if (( status == 0 )) && [[ -d "$staging/$destination_name" && ! -e "$destination_parent/$destination_name" ]]; then
            mv -- "$staging/$destination_name" "$destination_parent/$destination_name"
            rmdir -- "$staging"
            exit 0
        fi
        rm -rf -- "$staging"
        (( status != 0 )) || { printf 'The workspace tool produced no output folder.\n' >&2; status=1; }
        exit "$status"
        ;;
    import-md)
        if (( $# != 4 )); then usage >&2; exit 2; fi
        require_reviewed_tree
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
            exec "${gui[@]}" "$image" sh -ec '/build/editor_tests && /build/desktop_tests && /build/review_desktop_tests \
                && /build/pasted_link_preview_tests \
                && NEBULA_TEST_TMPDIR=/build /build/reliability_tests \
                && { [ ! -x /build/trash_safety_tests ] || /build/trash_safety_tests; }'
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
            if [[ "$skip_picker" == false ]]; then
                for gui_index in "${!gui[@]}"; do
                    if [[ "${gui[$gui_index]}" == --network=none ]]; then gui[$gui_index]=--network=bridge; fi
                done
            fi
            exec "${gui[@]}" --mount "$(mount_spec "$host_workspace" /data)" "$image" \
                /build/nebula_prototype --workspace /data --workspace-display "$host_workspace" "${app_args[@]}"
        fi
        exec "${gui[@]}" "$image" /build/nebula_prototype "${app_args[@]}"
        ;;
esac
