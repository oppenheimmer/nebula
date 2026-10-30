#!/bin/sh
# Private OS desktop services inside the optional test container only.
set -eu
export NEBULA_OS_DESKTOP=1
export NEBULA_ORCA_LOG=/build/desktop-orca.log
ibus version
orca --version
speech-dispatcher --version
dpkg-query -W ibus-anthy anthy at-spi2-core openbox xvfb pulseaudio
openbox >/build/desktop-openbox.log 2>&1 &
ibus-daemon --replace --desktop=openbox --emoji-extension=disable >/build/desktop-ibus.log 2>&1 &
for attempt in $(seq 1 100); do
    if ibus list-engine 2>/dev/null | grep -q anthy; then break; fi
    sleep .1
done
ibus list-engine | grep -q anthy
pulseaudio --daemonize --exit-idle-time=-1 --load="module-null-sink sink_name=nebula_test"
pactl set-default-sink nebula_test
mkdir -p /tmp/speech-conf /build/desktop-speech
cat > /tmp/speech-conf/speechd.conf <<'CONF'
AddModule "espeak-ng" "sd_espeak-ng" "/etc/speech-dispatcher/modules/espeak-ng.conf"
DefaultModule espeak-ng
DefaultLanguage en
LogLevel 2
CONF
speech-dispatcher -d -C /tmp/speech-conf -S "$XDG_RUNTIME_DIR/speechd.sock" -L /build/desktop-speech
export SPEECHD_ADDRESS="unix_socket:$XDG_RUNTIME_DIR/speechd.sock"
timeout 120 /build/desktop_tests real_ibus_candidates_commit_cancel_and_preserve_block_boundaries
python3 - <<'ENGINE'
import gi
gi.require_version('IBus', '1.0')
from gi.repository import IBus
assert IBus.Bus.new().set_global_engine('xkb:us::eng')
ENGINE
# Run the full interaction suites on the private native desktop, then repeat
# the activation-sensitive workflows (BUG-014/BUG-019) before starting Orca.
NEBULA_OS_DESKTOP=0 timeout 120 /build/editor_tests
NEBULA_OS_DESKTOP=0 timeout 120 /build/desktop_tests
for attempt in $(seq 1 20); do
    printf 'Native focus repetition %s/20\n' "$attempt"
    NEBULA_OS_DESKTOP=0 timeout 30 /build/desktop_tests \
        search_shortcut_opens_matching_block_and_preserves_history \
        unresolved_existing_choice_replaces_only_the_selected_reference \
        outgoing_and_backlink_lists_navigate_exact_blocks_with_keyboard
done
: > "$NEBULA_ORCA_LOG"
# Orca opens its debug file with block buffering. Configure only that private
# test log so live assertions observe actual speech without waiting for exit.
mkdir -p "$HOME/orca"
cat > "$HOME/orca/orca-customizations.py" <<'READER'
from orca import debug
if debug.debugFile is not None:
    debug.debugFile.reconfigure(line_buffering=True)
READER
PYTHONUNBUFFERED=1 orca --enable speech --disable braille --user-prefs "$HOME/orca" \
    --debug-file "$NEBULA_ORCA_LOG" >/build/desktop-orca-console.log 2>&1 &
reader=$!
trap 'kill -INT "$reader" 2>/dev/null || true' EXIT
for attempt in $(seq 1 100); do
    if grep -q 'SPEECH OUTPUT.*Screen reader on' "$NEBULA_ORCA_LOG" 2>/dev/null; then break; fi
    sleep .1
done
grep -q 'SPEECH OUTPUT.*Screen reader on' "$NEBULA_ORCA_LOG"
timeout 240 /build/desktop_tests real_orca_reads_block_details_and_selection
