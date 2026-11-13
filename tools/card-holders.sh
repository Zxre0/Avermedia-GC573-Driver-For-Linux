# SPDX-License-Identifier: GPL-2.0-only
# Sourced by system-start.sh (root) before a mode change reloads the driver.
# The desktop's WirePlumber and Fedora's alsa-state daemon (alsactl rdaemon)
# keep the card's audio control device open, so rmmod fails with "in use".
# Pause those two around the reload. Any other program (OBS, GC573 Preview)
# means someone is capturing: never interrupt it, report and retry later.

paused_alsa=
paused_wireplumber=()

# Print "PID USER COMMAND" for each process with a GC573 node open.
card_holders() {
    local device=/sys/bus/pci/devices/$1 path match=() fd pid
    for path in "$device"/video4linux/video* "$device"/sound/card*/controlC* \
            "$device"/sound/card*/pcmC* "$device"/sound/card*/hwC*; do
        [[ -e $path ]] || continue
        case $path in
            */video4linux/*) match+=(-o -lname "/dev/${path##*/}") ;;
            *) match+=(-o -lname "/dev/snd/${path##*/}") ;;
        esac
    done
    ((${#match[@]})) || return 0
    # Processes can exit during the scan; find then reports an error.
    { find /proc/[0-9]*/fd -mindepth 1 -maxdepth 1 \( "${match[@]:1}" \) -printf '%h\n' 2>/dev/null || true; } |
        sort -u | while read -r fd; do
            pid=${fd%/fd}
            pid=${pid##*/}
            echo "$pid $(stat -c %U -- "/proc/$pid" 2>/dev/null) $(cat -- "/proc/$pid/comm" 2>/dev/null)"
        done
}

# Returns 75 (retry later) while a capture application has the card open.
release_card() {
    local holders others user
    holders=$(card_holders "$1")
    others=$(grep -vE '^[0-9]+ [^ ]+ (wireplumber|alsactl)$' <<< "$holders" || true)
    if [[ -n $others ]]; then
        echo "GC573 mode change waiting: the card is open in" \
            "$(awk '{printf "%s%s (pid %s)", (NR > 1 ? ", " : ""), $3, $1}' <<< "$others")." \
            'Close it to apply the saved mode (temporary, exit 75).' >&2
        return 75
    fi
    if grep -qE ' alsactl$' <<< "$holders" && systemctl is-active --quiet alsa-state.service; then
        systemctl stop alsa-state.service && paused_alsa=1
    fi
    for user in $(awk '$3 == "wireplumber" {print $2}' <<< "$holders" | sort -u); do
        if systemctl --user -M "$user@" is-active --quiet wireplumber.service &&
                systemctl --user -M "$user@" stop wireplumber.service; then
            paused_wireplumber+=("$user")
        fi
    done
}

resume_card_holders() {
    local user
    for user in "${paused_wireplumber[@]}"; do
        systemctl --user -M "$user@" start wireplumber.service ||
            echo "Could not restart WirePlumber for $user." >&2
    done
    paused_wireplumber=()
    if [[ -n $paused_alsa ]]; then
        paused_alsa=
        systemctl start alsa-state.service || echo 'Could not restart alsa-state.service.' >&2
    fi
}
