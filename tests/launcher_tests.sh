#!/usr/bin/env bash
# Exercise host-folder selection/mounting without a Docker daemon or GUI.
set -euo pipefail
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fixture=$(mktemp -d "${NEBULA_TEST_TMPDIR:-/tmp}/nebula-launcher.XXXXXX")
created_socket_dir=false
cleanup() {
    rm -rf -- "$fixture"
    if [[ "$created_socket_dir" == true ]]; then rmdir /tmp/.X11-unix; fi
}
trap cleanup EXIT
# The launcher checks the socket directory; the mock never connects to a display.
if [[ ! -d /tmp/.X11-unix ]]; then
    mkdir /tmp/.X11-unix
    created_socket_dir=true
fi
mkdir -p "$fixture/project/scripts" "$fixture/bin" "$fixture/calls"
cp "$source_dir/scripts/dev.sh" "$fixture/project/scripts/dev.sh"
touch "$fixture/authority"
cat > "$fixture/bin/docker" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == image ]]; then exit 0; fi
shopt -s nullglob
calls=("$NEBULA_TEST_CALLS"/*)
printf '%s\0' "$@" > "$NEBULA_TEST_CALLS/${#calls[@]}"
# Emulate the workspace tool creating its destination inside the staging mount.
output_source=
for arg in "$@"; do
    case "$arg" in *'"target=/output"'*) output_source=${arg#*\"source=}; output_source=${output_source%%\",*} ;; esac
done
if [[ -n "$output_source" ]]; then
    for arg in "$@"; do [[ "$arg" != /output/* ]] || mkdir -p -- "$output_source/${arg#/output/}"; done
fi
for arg in "$@"; do
    if [[ "$arg" == --select-workspace ]]; then
        printf '%s' "${NEBULA_TEST_SELECTION:-}"
        exit "${NEBULA_TEST_PICKER_STATUS:-0}"
    fi
done
MOCK
chmod +x "$fixture/bin/docker"
export PATH="$fixture/bin:$PATH" DISPLAY=:99 XAUTHORITY="$fixture/authority"
export NEBULA_TEST_CALLS="$fixture/calls"
launcher="$fixture/project/scripts/dev.sh"
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
load_call() {
    call=()
    while IFS= read -r -d '' argument; do call+=("$argument"); done < "$NEBULA_TEST_CALLS/$1"
}
has_argument() {
    local arg
    for arg in "${call[@]}"; do [[ "$arg" != "$1" ]] || return 0; done
    return 1
}
reset_calls() { rm -f -- "$NEBULA_TEST_CALLS"/*; }
count_calls() { local files=("$NEBULA_TEST_CALLS"/*); [[ ${#files[@]} == "$1" ]] || fail "expected $1 Docker calls"; }

# Canceling the chooser creates no workspace and never launches the editor.
export NEBULA_TEST_SELECTION=
bash "$launcher" run
count_calls 2
load_call 0
[[ "${call[*]}" == *'cmake --build'* ]] || fail 'run must rebuild first'
has_argument --network=none || fail 'build must stay offline'
load_call 1
has_argument --select-workspace || fail 'missing chooser'
has_argument --network=none || fail 'chooser must stay offline'
has_argument "NEBULA_USER_HOME=$HOME" || fail 'chooser must suggest host home'
has_argument "type=bind,\"source=$HOME\",\"target=$HOME\",readonly" || fail 'chooser home must be read-only'

# Spaces, commas and quotes remain one Docker CSV mount and one display argument.
reset_calls
export NEBULA_TEST_SELECTION="$fixture/Diary 日本語, \"notes\""
[[ ! -e "$NEBULA_TEST_SELECTION" ]] || fail 'bad fixture'
bash "$launcher" run
count_calls 3
[[ -d "$NEBULA_TEST_SELECTION" ]] || fail 'chosen host folder was not created'
load_call 2
escaped=${NEBULA_TEST_SELECTION//\"/\"\"}
has_argument "type=bind,\"source=$escaped\",\"target=/data\"" || fail 'invalid chosen-folder mount'
has_argument "$NEBULA_TEST_SELECTION" || fail 'lost host display path'
has_argument /data || fail 'missing container workspace'
has_argument --select-workspace && fail 'editor must run separately from chooser'
has_argument --network=bridge || fail 'workspace GUI needs paste-preview network access'
has_argument --network=none && fail 'workspace GUI must replace the offline network flag'
has_argument "type=bind,\"source=$HOME\",\"target=$HOME\",readonly" && fail 'editor must not mount home'

reset_calls
bash "$launcher" run --workspace "$fixture/explicit" --smoke-test
count_calls 2
load_call 1
has_argument --smoke-test || fail 'lost prototype argument'
has_argument --network=none || fail 'workspace smoke must stay offline'
has_argument --select-workspace && fail 'explicit workspace must skip chooser'

reset_calls
bash "$launcher" run --demo
count_calls 2
load_call 1
has_argument --demo || fail 'demo must bypass workspace'
has_argument --workspace && fail 'demo should not open storage'
has_argument --network=none || fail 'demo must stay offline'

reset_calls
bash "$launcher" run --workspace "$fixture/preview-workspace"
count_calls 2
load_call 0
has_argument --network=none || fail 'explicit workspace build must stay offline'
load_call 1
has_argument --network=bridge || fail 'explicit normal workspace needs paste-preview network access'
has_argument --network=none && fail 'normal workspace must not retain network=none'

reset_calls
bash "$launcher" test-native
count_calls 2
load_call 1
has_argument --network=none || fail 'native tests must stay offline'

reset_calls
import_folder="$fixture/Import 日本語, \"files\""
mkdir -p -- "$import_folder"
bash "$launcher" run --workspace "$fixture/import-workspace" --import-root "$import_folder" --smoke-test
count_calls 2
load_call 1
escaped_import=${import_folder//\"/\"\"}
has_argument "type=bind,\"source=$escaped_import\",\"target=/imports\",readonly" || fail 'imports must be one explicit read-only mount'
has_argument --import-root && fail 'host import flag must not reach the app'
has_argument "type=bind,\"source=$HOME\",\"target=$HOME\",readonly" && fail 'import must not implicitly expose home'

reset_calls
if bash "$launcher" run --workspace "$fixture/invalid-import" --import-root '' 2>/dev/null; then fail 'empty import root must fail'; fi
count_calls 1
reset_calls
if bash "$launcher" run --workspace "$fixture/missing-import-workspace" --import-root "$fixture/no-such-import-root" 2>/dev/null; then fail 'missing import root must fail'; fi
[[ ! -e "$fixture/missing-import-workspace" ]] || fail 'invalid import root must not create the workspace'
count_calls 1

reset_calls
if bash "$launcher" run --workspace '' 2>/dev/null; then fail 'empty workspace must fail'; fi
count_calls 1
reset_calls
export NEBULA_TEST_PICKER_STATUS=1
if bash "$launcher" run 2>/dev/null; then fail 'chooser failure must propagate'; fi
count_calls 2
reset_calls
unset NEBULA_TEST_PICKER_STATUS
mkdir -p "$fixture/source"
bash "$launcher" backup "$fixture/source" "$fixture/backup"
count_calls 2
load_call 1
has_argument /build/nebula_workspace || fail 'backup must use the headless workspace tool'
has_argument "type=bind,\"source=$fixture/source\",\"target=/source\",readonly" || fail 'backup source must be read-only'
has_argument /output/backup || fail 'backup destination was lost'
has_argument "type=bind,\"source=$fixture\",\"target=/output\"" && fail 'the destination parent must not be mounted'
[[ -d "$fixture/backup" ]] || fail 'staged backup was not moved into place'
compgen -G "$fixture/.nebula-staging.*" >/dev/null && fail 'staging folder left behind'
reset_calls
if bash "$launcher" backup "$fixture/source" "$fixture/backup" 2>/dev/null; then fail 'an existing destination must be refused'; fi
compgen -G "$NEBULA_TEST_CALLS/*" >/dev/null && fail 'an existing destination must be refused before Docker runs'
reset_calls
if bash "$launcher" backup "$fixture/source" "$fixture/source/inside" 2>/dev/null; then fail 'nested backup must fail'; fi
printf 'PASS: launcher rebuild, cancel, selection, quoting, mounts, demo, and failures\n'
reset_calls
bash "$launcher" export-md "$fixture/source" "$fixture/markdown"
count_calls 2
load_call 1
has_argument export-md || fail 'Markdown export command lost'
has_argument /output/markdown || fail 'Markdown export destination lost'
reset_calls
bash "$launcher" import-md "$import_folder" "$fixture/import-workspace" 'database-id'
count_calls 2
load_call 1
has_argument "type=bind,\"source=$escaped_import\",\"target=/source\",readonly" || fail 'Markdown source must be read-only'
has_argument "type=bind,\"source=$fixture/import-workspace\",\"target=/data\"" || fail 'Markdown destination workspace must be explicit'
has_argument database-id || fail 'destination database ID lost'
printf 'PASS: Markdown import/export commands and mount scopes\n'
for command in export-json import-json; do
    reset_calls
    bash "$launcher" "$command" "$import_folder" "$fixture/$command"
    count_calls 2
    load_call 1
    has_argument "$command" || fail 'JSON command lost'
    has_argument "type=bind,\"source=$escaped_import\",\"target=/source\",readonly" || fail 'JSON source must be read-only'
    has_argument "/output/$command" || fail 'JSON destination lost'
done
printf 'PASS: lossless JSON command routing and mount scopes\n'

reset_calls
bash "$launcher" fuzz 1
count_calls 1
load_call 0
has_argument --network=none || fail 'fuzzing must stay offline'
has_argument NEBULA_FUZZ_SECONDS=1 || fail 'fuzz duration lost'
[[ "${call[*]}" == *NEBULA_LIBFUZZER=ON* ]] || fail 'parsers must be instrumented'
if bash "$launcher" fuzz 0 >/dev/null 2>&1; then fail 'zero fuzz duration accepted'; fi

cat > "$fixture/bin/git" <<'MOCK'
#!/usr/bin/env bash
case "$*" in *ls-files*) printf 'sample.cpp\0' ;; *) exit 1 ;; esac
MOCK
chmod +x "$fixture/bin/git"
for mode in check write; do
    reset_calls
    if [[ $mode == check ]]; then bash "$launcher" format --check; else bash "$launcher" format; fi
    count_calls 1
    load_call 0
    has_argument --network=none || fail 'formatting must stay offline'
    has_argument clang-format || fail 'formatting must use container tools'
    has_argument sample.cpp || fail 'source list lost'
    mount="type=bind,\"source=$fixture/project\",\"target=/workspace\""
    if [[ $mode == check ]]; then
        has_argument --dry-run || fail 'format check must not write'
        has_argument "$mount,readonly" || fail 'format check source must be read-only'
    else
        has_argument -i || fail 'format must rewrite sources'
        has_argument "$mount" || fail 'format source must be writable'
    fi
done
printf 'PASS: isolated fuzzing and formatting commands\n'

for mode in bundle verify; do
    reset_calls
    bash "$launcher" portability "$mode"
    count_calls 1
    load_call 0
    has_argument --network=none || fail 'portability checks must stay offline'
    has_argument "NEBULA_PORTABILITY_MODE=$mode" || fail 'portability mode lost'
    has_argument "type=bind,\"source=$fixture/project\",\"target=/workspace\",readonly" || fail 'portability source must be read-only'
    has_argument "type=bind,\"source=$fixture/project/build-container-portability\",\"target=/build\"" || fail 'portability build must be isolated'
done
reset_calls
if bash "$launcher" portability invalid >/dev/null 2>&1; then fail 'invalid portability mode accepted'; fi
compgen -G "$NEBULA_TEST_CALLS/*" >/dev/null && fail 'invalid mode must fail before Docker'
printf 'PASS: offline portability fixtures and mode validation\n'
