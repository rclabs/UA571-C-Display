#!/usr/bin/env bash
# Stop this user's ESP-IDF monitor processes, even after USB disconnection.
# Deliberately do not kill idf.py itself: it may still be building/flashing.
set -euo pipefail

signal=TERM
dry_run=false
for arg in "$@"; do
    case "$arg" in
        --force) signal=KILL ;;
        --dry-run) dry_run=true ;;
        -h|--help)
            printf 'Usage: %s [--dry-run] [--force]\n' "$0"
            printf 'Stops all ESP-IDF monitors owned by your user, across projects.\n'
            printf 'Restores stopped monitors\x27 terminals with stty sane.\n'
            printf 'Default: SIGTERM. Use --force only if a monitor remains stuck.\n'
            exit 0 ;;
        *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

pids=()
declare -A monitor_ttys=()
declare -A terminals=()
for proc in /proc/[0-9]*; do
    # Linux /proc ownership restricts this to the current effective user.
    [[ -O "$proc" && -r "$proc/cmdline" ]] || continue
    argv=()
    mapfile -d '' -t argv < "$proc/cmdline" 2>/dev/null || continue
    ((${#argv[@]} >= 2)) || continue
    executable=${argv[0]##*/}
    [[ "$executable" =~ ^python([0-9]+(\.[0-9]+)*)?$ ]] || continue

    # Match the executed program, not mentions inside a shell command or the
    # monitor's embedded idf.py restart command. These are IDF's usual forms.
    if [[ "${argv[1]##*/}" == idf_monitor.py ]] ||
       [[ "${argv[1]}" == -m && "${argv[2]:-}" == esp_idf_monitor ]]; then
        pid=${proc##*/}
        pids+=("$pid")
        # Save the controlling terminal BEFORE the process exits. Using ps
        # also handles redirected stdin; never apply stty to the USB serial port.
        tty_name=$(ps -p "$pid" -o tty= 2>/dev/null || true)
        tty_name=${tty_name//[[:space:]]/}
        if [[ "$tty_name" =~ ^(pts/[0-9]+|tty[0-9]+)$ ]]; then
            monitor_ttys[$pid]="/dev/$tty_name"
            terminals["/dev/$tty_name"]=1
        fi
        printf 'Monitor PID %s:' "$pid"
        printf ' %q' "${argv[@]}"
        printf '\n'
    fi
done

if ((${#pids[@]} == 0)); then
    printf 'No ESP-IDF monitors found for your user.\n'
    exit 0
fi
if "$dry_run"; then
    printf 'Dry run: would send SIG%s to %d monitor process(es).\n' "$signal" "${#pids[@]}"
    for tty_path in "${!terminals[@]}"; do
        printf 'Would restore terminal: %s\n' "$tty_path"
    done
    exit 0
fi

failed=false
for pid in "${pids[@]}"; do
    if kill -s "$signal" "$pid" 2>/dev/null; then
        printf 'Sent SIG%s to PID %s.\n' "$signal" "$pid"
    elif kill -0 "$pid" 2>/dev/null; then
        printf 'Could not stop PID %s.\n' "$pid" >&2
        failed=true
    fi
    # A wrapper may already have exited after its child stopped: harmless.
done
# Allow signal handlers to finish before restoring settings; otherwise their
# own teardown could overwrite ours. Zombies have already released their TTY.
monitor_alive() {
    local state
    state=$(ps -p "$1" -o stat= 2>/dev/null || true)
    state=${state//[[:space:]]/}
    [[ -n "$state" && "$state" != Z* ]]
}
for ((attempt=0; attempt<20; attempt++)); do
    remaining=false
    for pid in "${pids[@]}"; do
        if monitor_alive "$pid"; then remaining=true; break; fi
    done
    if ! "$remaining"; then break; fi
    sleep 0.1
done

for tty_path in "${!terminals[@]}"; do
    busy=false
    for pid in "${pids[@]}"; do
        if [[ "${monitor_ttys[$pid]:-}" == "$tty_path" ]] && monitor_alive "$pid"; then
            busy=true
            break
        fi
    done
    if "$busy"; then
        printf 'Monitor still running on %s; rerun with --force to stop and restore it.\n' "$tty_path" >&2
        failed=true
    elif stty -F "$tty_path" sane 2>/dev/null; then
        printf 'Restored input echo and normal terminal settings: %s\n' "$tty_path"
    else
        printf 'Could not restore %s (it may have closed). In that terminal, type stty sane and press Ctrl+J.\n' "$tty_path" >&2
        failed=true
    fi
done
if "$failed"; then exit 1; fi
if [[ "$signal" == TERM ]]; then
    printf 'If a monitor remains stuck, rerun with --force.\n'
fi
