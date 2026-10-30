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
load_call 1
has_argument --select-workspace || fail 'missing chooser'
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
has_argument "type=bind,\"source=$HOME\",\"target=$HOME\",readonly" && fail 'editor must not mount home'

reset_calls
bash "$launcher" run --workspace "$fixture/explicit" --smoke-test
count_calls 2
load_call 1
has_argument --smoke-test || fail 'lost prototype argument'
has_argument --select-workspace && fail 'explicit workspace must skip chooser'

reset_calls
bash "$launcher" run --demo
count_calls 2
load_call 1
has_argument --demo || fail 'demo must bypass workspace'
has_argument --workspace && fail 'demo should not open storage'

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
reset_calls
if bash "$launcher" backup "$fixture/source" "$fixture/source/inside" 2>/dev/null; then fail 'nested backup must fail'; fi
printf 'PASS: launcher rebuild, cancel, selection, quoting, mounts, demo, and failures\n'
