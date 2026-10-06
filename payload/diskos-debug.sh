#!/bin/sh
# diskOS debug access. Two independent channels, both OFF unless explicitly enabled:
#   - SSH  (dropbear over WiFi) with a RANDOM per-enable password. We NEVER use or expose the
#     stock root password: the caller (mq_ui) passes a fresh sha512 crypt hash, which we place in a
#     private shadow file bind-mounted over /etc/shadow (the on-disk stock /etc/shadow is untouched).
#   - SERIAL (a root shell on the USB gadget /dev/ttyGS0). Local USB only; no network exposure.
# State + keys live under /usr/data/sshd (persists across rootfs flashes). Idempotent; every op is
# best-effort so a missing tool or busy resource never wedges the caller.
#
# Usage:
#   diskos-debug.sh ssh-on  <root_sha512_hash>   # start dropbear with this password hash
#   diskos-debug.sh ssh-off                      # stop dropbear + drop the shadow overlay
#   diskos-debug.sh serial-on                    # bind ONE root shell to /dev/ttyGS0
#   diskos-debug.sh serial-off                   # stop the serial shell
#   diskos-debug.sh status                       # print: SSH=on/off SERIAL=on/off

SELF=/usr/data/sshd
DB="$SELF/dropbearmulti"
KEYS="$SELF/keys"
SHADOW="$SELF/shadow"          # our private shadow, bind-mounted over /etc/shadow while SSH is on
SERIALPID="$SELF/serial.pid"   # pid of the single ttyGS0 shell supervisor
BUNDLED=/usr/project/dropbearmulti   # read-only rootfs copy shipped in the image

# FAIL-CLOSED test for the private shadow overlay. Returns 0 (present) if it is bind-mounted over
# /etc/shadow OR if we cannot tell (/proc/mounts unreadable); returns 1 (absent) ONLY when a readable
# /proc/mounts definitively shows no such mount. `grep` exit codes: 0=match, 1=no match, >=2=error;
# we must treat "no match" as absent but any error as still-present, so we never report a clean state
# on uncertainty.
overlay_present() {
    grep -q ' /etc/shadow ' /proc/mounts 2>/dev/null
    _g=$?
    [ "$_g" -eq 1 ] && return 1   # readable, definitively no overlay
    return 0                       # matched (0) or read error (>=2) -> present/unknown, fail closed
}

ensure_db() {
    mkdir -p "$KEYS" 2>/dev/null
    # Provision/refresh the dropbear binary from the shipped rootfs copy. The copy on /usr/data survives
    # flashes, so a copy left by an older image must be REPLACED whenever it differs from the shipped one
    # (not only when missing). Copy to a temp name, prove it matches, then rename over the old one: a
    # failed copy leaves the old binary in place, and a running daemon keeps its own (old) file.
    if [ -f "$BUNDLED" ] && ! cmp -s "$BUNDLED" "$DB" 2>/dev/null; then
        rm -f "$DB.new" 2>/dev/null
        if cp "$BUNDLED" "$DB.new" 2>/dev/null && chmod 755 "$DB.new" 2>/dev/null && cmp -s "$BUNDLED" "$DB.new"; then
            mv -f "$DB.new" "$DB" 2>/dev/null
        fi
        rm -f "$DB.new" 2>/dev/null
    fi
    [ -f "$DB" ] && [ ! -x "$DB" ] && chmod 755 "$DB" 2>/dev/null   # identical copy that lost its exec bit
    [ -x "$DB" ] || return 1
    [ -s "$KEYS/ed25519_host_key" ] || "$DB" dropbearkey -t ed25519       -f "$KEYS/ed25519_host_key" >/dev/null 2>&1
    [ -s "$KEYS/rsa_host_key" ]     || "$DB" dropbearkey -t rsa -s 2048   -f "$KEYS/rsa_host_key"     >/dev/null 2>&1
    return 0
}

# FAIL-CLOSED: SSH starts only once the private-password overlay is PROVEN active. Any failure (no
# hash, stuck overlay, empty/garbled shadow, bind failure, dropbear not up) returns non-zero WITHOUT
# leaving dropbear authenticating against the stock /etc/shadow.
ssh_on() {
    hash="$1"
    [ -n "$hash" ] || { echo "err: no hash"; return 1; }
    ensure_db      || { echo "err: no dropbear binary"; return 1; }
    # STOP any already-running dropbear BEFORE touching the overlay. Otherwise, in the window between
    # unmounting the old overlay and binding the new one, a running daemon would authenticate against
    # the STOCK /etc/shadow (and any later failure would leave it there). No daemon runs during the swap.
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill "$p" 2>/dev/null; done
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 5 ]; do sleep 1; i=$((i+1)); done
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && { echo "err: could not stop existing dropbear"; return 1; }
    # Drop any existing overlay and CONFIRM it is gone - reading /etc/shadow while it still aliases
    # $SHADOW would let `>` truncate it to empty. If it will not unmount, refuse.
    if overlay_present; then
        umount /etc/shadow 2>/dev/null
        overlay_present && { echo "err: shadow overlay stuck"; return 1; }
    fi
    [ -f /etc/shadow ] || { echo "err: no /etc/shadow"; return 1; }
    # Build the private shadow (root line = our random hash) and PROVE it holds that hash before
    # trusting it - never bind an empty or malformed shadow.
    awk -v h="$hash" -F: 'BEGIN{OFS=":"} $1=="root"{$2=h} {print}' /etc/shadow > "$SHADOW" 2>/dev/null \
        || { echo "err: shadow gen failed"; return 1; }
    chmod 600 "$SHADOW" 2>/dev/null
    awk -F: -v h="$hash" '$1=="root" && $2==h{ok=1} END{exit ok?0:1}' "$SHADOW" 2>/dev/null \
        || { echo "err: shadow bad"; rm -f "$SHADOW"; return 1; }
    mount -o bind "$SHADOW" /etc/shadow 2>/dev/null || mount --bind "$SHADOW" /etc/shadow 2>/dev/null
    # CONFIRM the overlay is live BEFORE starting SSH; if the bind failed, dropbear would authenticate
    # against the STOCK /etc/shadow (stock root password reachable over the network) - refuse.
    grep -q ' /etc/shadow ' /proc/mounts 2>/dev/null || { echo "err: overlay failed, refusing SSH"; return 1; }
    pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 || \
        "$DB" dropbear -p 22 -r "$KEYS/ed25519_host_key" -r "$KEYS/rsa_host_key" >/dev/null 2>&1
    if ! pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1; then
        # dropbear failed to start: tear the overlay back down and CONFIRM it is gone (retry once).
        # Report which state we ended in so the caller never assumes a clean OFF while it is still mounted.
        umount /etc/shadow 2>/dev/null
        overlay_present && { sleep 1; umount /etc/shadow 2>/dev/null; }
        if overlay_present; then
            echo "err: dropbear did not start AND overlay stuck"; return 2
        fi
        echo "err: dropbear did not start"; return 1
    fi
    echo on
}

# FAIL-CLOSED: kill dropbear and CONFIRM it is gone BEFORE dropping the overlay (a surviving process
# would otherwise fall back to the stock /etc/shadow once the bind is removed). Report the REAL state.
ssh_off() {
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill "$p" 2>/dev/null; done
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 4 ]; do sleep 1; i=$((i+1)); done
    for p in $(pgrep -f "dropbearmulti dropbear" 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    # brief settle, then drop the overlay. Whole path stays under ~6s so the UI's bounded call
    # (12s, see debug_ui.c disable_dbg) always lets us finish the umount before it can kill us -
    # otherwise the UI could report OFF while the overlay is still mounted.
    i=0; while pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && [ "$i" -lt 2 ]; do sleep 1; i=$((i+1)); done
    # NEVER drop the overlay while a daemon might still be alive: if a process somehow survived SIGKILL,
    # unmounting would drop it back onto the STOCK /etc/shadow. Keep the overlay (so it stays bound to
    # OUR shadow) and fail instead. Only umount once dropbear is CONFIRMED gone.
    if pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1; then
        echo "err: dropbear still running - overlay kept, refusing to expose stock shadow"; return 1
    fi
    overlay_present && umount /etc/shadow 2>/dev/null
    if overlay_present; then
        echo "err: overlay stuck"; return 1
    fi
    echo off
}

# True only if $SERIALPID names a LIVE process that is actually OUR supervisor. A bare `kill -0` would
# be fooled by PID reuse (some unrelated process now holding that number) - so we also confirm the
# process's cmdline still contains the ttyGS0 supervisor marker before trusting/signalling it.
serial_alive() {
    [ -f "$SERIALPID" ] || return 1
    _sp=$(cat "$SERIALPID" 2>/dev/null); [ -n "$_sp" ] || return 1
    kill -0 "$_sp" 2>/dev/null || return 1
    tr '\0' ' ' < "/proc/$_sp/cmdline" 2>/dev/null | grep -q 'ttyGS0'
}

serial_on() {
    [ -c /dev/ttyGS0 ] || { echo "no ttyGS0"; return 1; }
    # Exactly ONE supervisor owns ttyGS0. A plain `fuser` guard is NOT enough: between shell respawns
    # the supervisor sleeps 1s without holding the tty, so a repeated serial-on (e.g. S99's loop) would
    # see no owner and start a SECOND supervisor = the two-shell wedge. Guard on the SUPERVISOR pid
    # (which stays alive across that gap, validated as really ours) via a pidfile instead.
    if serial_alive; then echo on; return 0; fi
    rm -f "$SERIALPID" 2>/dev/null   # stale/reused pid - clear it so a real supervisor can start
    setsid sh -c 'echo $$ > '"$SERIALPID"'; while true; do /bin/sh </dev/ttyGS0 >/dev/ttyGS0 2>&1; sleep 1; done' </dev/null >/dev/null 2>&1 &
    # Confirm the supervisor actually came up (setsid/fork/pidfile-write can all fail) before claiming
    # success - otherwise the caller would show "serial on" with no shell behind it. Bounded to ~2s
    # (portable whole-second sleep) so it stays well under the UI's serial-on timeout.
    serial_alive || sleep 1
    serial_alive || sleep 1
    if serial_alive; then echo on; return 0; fi
    echo "err: serial supervisor did not start"; return 1
}

serial_off() {
    # Only signal the pidfile's process if it is verifiably OUR supervisor (never a reused PID).
    if serial_alive; then kill "$(cat "$SERIALPID" 2>/dev/null)" 2>/dev/null; fi
    rm -f "$SERIALPID" 2>/dev/null
    fuser -k /dev/ttyGS0 2>/dev/null
    echo off
}

status() {
    s=off; pgrep -f "dropbearmulti dropbear" >/dev/null 2>&1 && s=on
    r=off; serial_alive && r=on
    echo "SSH=$s SERIAL=$r"
}

case "$1" in
    ssh-on)     ssh_on "$2" ;;
    ssh-off)    ssh_off ;;
    serial-on)  serial_on ;;
    serial-off) serial_off ;;
    status)     status ;;
    *) echo "usage: $0 {ssh-on <hash>|ssh-off|serial-on|serial-off|status}"; exit 1 ;;
esac
