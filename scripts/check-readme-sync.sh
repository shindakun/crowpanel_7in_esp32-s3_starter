#!/usr/bin/env bash
# Warn when source/structure changed but README.md was not updated in the same
# commit. This is a guard against docs drifting behind the code. It is advisory:
# if the change genuinely needs no doc update, re-run the commit with
# SKIP_README_SYNC=1 (or `git commit --no-verify`).
set -euo pipefail

if [ "${SKIP_README_SYNC:-0}" = "1" ]; then
    exit 0
fi

# Files staged for this commit (added/copied/modified/renamed).
staged="$(git diff --cached --name-only --diff-filter=ACMR)"

# Paths whose changes usually imply a README update is due.
code_changed="$(printf '%s\n' "$staged" | grep -E '^(components/|main/)' || true)"
readme_changed="$(printf '%s\n' "$staged" | grep -E '^README\.md$' || true)"

if [ -n "$code_changed" ] && [ -z "$readme_changed" ]; then
    echo "WARNING: code under components/ or main/ changed but README.md was not." >&2
    echo "Changed files:" >&2
    printf '  %s\n' $code_changed >&2
    echo >&2
    echo "If the README needs updating (component list, API, layout, demo" >&2
    echo "behavior, pin/switch notes), update it and re-stage. If this change" >&2
    echo "genuinely needs no doc update, commit with SKIP_README_SYNC=1 or" >&2
    echo "git commit --no-verify." >&2
    exit 1
fi

exit 0
