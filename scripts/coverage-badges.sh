#!/bin/bash
# Turn the summary that gcovr prints (see scripts/coverage.sh) into the JSON files that
# shields.io reads for its endpoint badges: coverage-lines.json and coverage-branches.json.
# Usage: scripts/coverage-badges.sh <summary log> <output directory>
set -euo pipefail

log=${1:?usage: coverage-badges.sh <summary log> <output directory>}
out=${2:?usage: coverage-badges.sh <summary log> <output directory>}

percent_of() {
    sed -n "s/^$1: \([0-9.]*\)%.*/\1/p" "$log" | head -1
}

color_of() {
    awk -v p="$1" 'BEGIN { print (p >= 100) ? "brightgreen" : (p >= 90) ? "green" : (p >= 80) ? "yellow" : "red" }'
}

write_badge() {
    local file=$1 label=$2 percent=$3
    [ -n "$percent" ] || { echo "no $label in $log" >&2; exit 1; }
    printf '{"schemaVersion":1,"label":"%s","message":"%s%%","color":"%s"}\n' \
        "$label" "$percent" "$(color_of "$percent")" >"$out/$file"
}

mkdir -p "$out"
write_badge coverage-lines.json "line coverage" "$(percent_of lines)"
write_badge coverage-branches.json "branch coverage" "$(percent_of branches)"
