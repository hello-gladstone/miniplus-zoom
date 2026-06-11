#!/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# launch_zoom.sh — Auto-start script for zoom_daemon on Miyoo Mini / Onion OS
#
# WHAT THIS FILE IS (for laymen):
#   This is a "shell script" — a tiny text file full of instructions that
#   the Miyoo's Linux system reads and follows, like a checklist.
#   Its one job: make sure zoom_daemon is running in the background
#   whenever the Miyoo is on.
#
# WHERE THIS FILE LIVES ON YOUR SD CARD:
#   /mnt/SDCARD/.tmp_update/startup/launch_zoom.sh
#
#   Onion OS automatically runs every script it finds in that "startup"
#   folder when the device boots. That's the hook we use.
#
# HOW ONION OS STARTUP WORKS (the deep version):
#   When Onion boots, a master init script scans .tmp_update/startup/
#   and sources each .sh file it finds. Our script checks if zoom_daemon
#   is already running (so it never starts twice), then launches it as
#   a background process (&) so it doesn't block the boot sequence.
#   stdout goes to a log file so you can debug if anything goes wrong.
#
# ─────────────────────────────────────────────────────────────────────────────

# Where we installed the compiled zoom_daemon binary on the SD card
DAEMON_PATH="/mnt/SDCARD/.tmp_update/bin/zoom_daemon"

# Log file — written to RAM disk so it doesn't wear out the SD card
LOG_FILE="/tmp/zoom_daemon.log"

# ── Safety checks ─────────────────────────────────────────────────────────────

# Check the binary actually exists before trying to run it
if [ ! -f "$DAEMON_PATH" ]; then
    echo "zoom_daemon: binary not found at $DAEMON_PATH — skipping." >> "$LOG_FILE"
    exit 0
fi

# Check it is executable (has the right permissions)
if [ ! -x "$DAEMON_PATH" ]; then
    echo "zoom_daemon: binary not executable — fixing permissions." >> "$LOG_FILE"
    chmod +x "$DAEMON_PATH"
fi

# ── Check if already running ──────────────────────────────────────────────────
# "pidof" asks Linux: "is there already a process named zoom_daemon running?"
# If yes, we do nothing. This prevents running two copies by accident.
if pidof zoom_daemon > /dev/null 2>&1; then
    echo "zoom_daemon: already running — skipping." >> "$LOG_FILE"
    exit 0
fi

# ── Wait for the framebuffer to be ready ─────────────────────────────────────
# The framebuffer device (/dev/fb0) might not exist yet at the very
# beginning of boot. We wait up to 5 seconds for it to appear.
WAIT=0
while [ ! -e /dev/fb0 ] && [ $WAIT -lt 5 ]; do
    sleep 1
    WAIT=$((WAIT + 1))
done

if [ ! -e /dev/fb0 ]; then
    echo "zoom_daemon: /dev/fb0 never appeared — aborting." >> "$LOG_FILE"
    exit 1
fi

# ── Wait for the input device to be ready ────────────────────────────────────
# Same idea — /dev/input/event0 (the buttons) needs to exist first.
WAIT=0
while [ ! -e /dev/input/event0 ] && [ $WAIT -lt 5 ]; do
    sleep 1
    WAIT=$((WAIT + 1))
done

if [ ! -e /dev/input/event0 ]; then
    echo "zoom_daemon: /dev/input/event0 never appeared — aborting." >> "$LOG_FILE"
    exit 1
fi

# ── Launch the daemon ─────────────────────────────────────────────────────────
# The & at the end means "run in the background" — the boot process
# continues immediately without waiting for zoom_daemon to finish.
# All output (logs) go to our log file.
echo "zoom_daemon: starting at $(date)." >> "$LOG_FILE"
"$DAEMON_PATH" >> "$LOG_FILE" 2>&1 &

# Save the process ID (PID) so we can stop it cleanly later if needed
echo $! > /tmp/zoom_daemon.pid
echo "zoom_daemon: launched with PID $!." >> "$LOG_FILE"

exit 0
