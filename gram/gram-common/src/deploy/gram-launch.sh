#!/bin/bash
# Gram's launcher for Steam, installed on the device as ~/gram-launch.sh by every `gram deploy` (embedded in gram:
# gram-common/src/deploy/gram-launch.sh). A Non-Steam Game shortcut points here, since Game Mode (gamescope) only
# shows what Steam starts; the same shortcut works in Desktop Mode.
#
# What to run comes from ~/.gram-launch, also written by gram deploy: the last deployed build, in the configuration
# the command chose. The session (DISPLAY and the rest) is Steam's, never overridden: gamescope's differs from
# Desktop Mode's. Everything goes to ~/gram-launch.log, which gram deploy follows from the other machine.

LOG="$HOME/gram-launch.log"
exec >"$LOG" 2>&1

# GRAM_DIR (the executable's folder), GRAM_EXECUTABLE, GRAM_CONTAINER (a distrobox, or empty), GRAM_ENV (a file to
# source first, or empty), GRAM_DEBUG (gram deploy --debug: the port lldb-server waits on, the game stopped before
# its first instruction until a debugger connects; empty otherwise).
if ! source "$HOME/.gram-launch"; then
    echo "gram-launch: no ~/.gram-launch: run gram deploy to this device first"
    exit 1
fi

echo "gram-launch: $(date '+%F %T'), $GRAM_DIR/$GRAM_EXECUTABLE"
echo "gram-launch: DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR XAUTHORITY=$XAUTHORITY"
echo "gram-launch: SteamAppId=$SteamAppId SteamGameId=$SteamGameId"
if [ -n "$GRAM_DEBUG" ]; then
    echo "gram-launch: under lldb-server, waiting for a debugger on port $GRAM_DEBUG"
fi

# The values travel as arguments ($1..$4), so nothing in them is read as shell code a second time.
run='if [ -n "$1" ]; then source "$1"; fi; cd "$2" || exit 1; if [ -n "$4" ]; then exec lldb-server gdbserver "localhost:$4" -- "./$3"; fi; exec "./$3"'
if [ -n "$GRAM_CONTAINER" ]; then
    exec distrobox enter "$GRAM_CONTAINER" -- bash -lc "$run" gram-launch "$GRAM_ENV" "$GRAM_DIR" "$GRAM_EXECUTABLE" "$GRAM_DEBUG"
else
    exec bash -lc "$run" gram-launch "$GRAM_ENV" "$GRAM_DIR" "$GRAM_EXECUTABLE" "$GRAM_DEBUG"
fi
