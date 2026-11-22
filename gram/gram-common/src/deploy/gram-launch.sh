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

# A game stopped at a breakpoint takes no input and draws nothing: to the device it looks idle, and it dims, then
# sleeps, cutting the debugger's connection. While debugged, the game runs under an inhibitor. Only a process of
# the session on the screen may take one (logind asks polkit; over ssh it's refused): this launcher, which Steam
# started, is one. If it's refused anyway, the game still starts, just without it.
inhibit=()
if [ -n "$GRAM_DEBUG" ]; then
    if systemd-inhibit --what=idle:sleep --who=gram --why="check" true 2>/dev/null; then
        inhibit=(systemd-inhibit --what=idle:sleep --who=gram --why="gram deploy --debug: the game may be stopped at a breakpoint")
        echo "gram-launch: keeping the device awake while the game is debugged"
    else
        echo "gram-launch: systemd-inhibit refused, the device may sleep while the game is debugged"
    fi
fi

# The values travel as arguments ($1..$4), so nothing in them is read as shell code a second time.
run='if [ -n "$1" ]; then source "$1"; fi; cd "$2" || exit 1; if [ -n "$4" ]; then exec lldb-server gdbserver "localhost:$4" -- "./$3"; fi; exec "./$3"'
if [ -n "$GRAM_CONTAINER" ]; then
    exec "${inhibit[@]}" distrobox enter "$GRAM_CONTAINER" -- bash -lc "$run" gram-launch "$GRAM_ENV" "$GRAM_DIR" "$GRAM_EXECUTABLE" "$GRAM_DEBUG"
else
    exec "${inhibit[@]}" bash -lc "$run" gram-launch "$GRAM_ENV" "$GRAM_DIR" "$GRAM_EXECUTABLE" "$GRAM_DEBUG"
fi
