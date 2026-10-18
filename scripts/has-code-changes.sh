#!/bin/bash
# Print "code=true" unless every file changed between BASE and HEAD is documentation.
# Unknown or unreachable BASE counts as a code change, so CI errs on the side of running.
set -uo pipefail

base="${1:-}"
head="${2:-HEAD}"

is_docs_path() {
    case "$1" in
        *.md | docs/* | .claude/*) return 0 ;;
        *) return 1 ;;
    esac
}

changed="$(git diff --name-only "$base" "$head" 2>/dev/null)" || changed=""
code=true
if [ -n "$base" ] && [ -n "$changed" ]; then
    code=false
    while IFS= read -r path; do
        is_docs_path "$path" || code=true
    done <<<"$changed"
fi

echo "code=$code"
