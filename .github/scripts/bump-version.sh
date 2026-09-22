#!/bin/bash
# bump-version.sh — increment moonormot.version.inc if code changed but version did not.
# Arguments: BEFORE_SHA  AFTER_SHA
# Exits 0 always; prints what it did.

set -euo pipefail

BEFORE="$1"
AFTER="$2"
VERSION_FILE="moonormot.version.inc"

# Code extensions that count as "code changed"
CODE_PATTERN='\.(pas|inc|dfm|res|a|o|lib|dll)$'

# If BEFORE is the zero SHA or unreachable, fall back to HEAD~1
if [ "$BEFORE" = "0000000000000000000000000000000000000000" ] || ! git cat-file -e "$BEFORE" 2>/dev/null; then
  BEFORE="$AFTER~1"
  if ! git cat-file -e "$BEFORE" 2>/dev/null; then
    echo "Initial commit, no parent — skipping."
    exit 0
  fi
fi

# Get list of changed files in the push range
CHANGED_FILES=$(git diff --name-only "$BEFORE".."$AFTER")

# Check if any code file changed (excluding version.inc itself)
CODE_CHANGED=false
VERSION_CHANGED=false

while IFS= read -r f; do
  [ -z "$f" ] && continue
  if [ "$f" = "$VERSION_FILE" ]; then
    VERSION_CHANGED=true
  elif echo "$f" | grep -qE "$CODE_PATTERN"; then
    CODE_CHANGED=true
  fi
done <<< "$CHANGED_FILES"

if [ "$CODE_CHANGED" = true ] && [ "$VERSION_CHANGED" = false ]; then
  # Read current version, increment, write back
  CURRENT=$(cat "$VERSION_FILE" | tr -d '[:space:]')
  NEW=$((CURRENT + 1))
  printf '%d' "$NEW" > "$VERSION_FILE"

  git config user.name "github-actions[bot]"
  git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
  git add "$VERSION_FILE"
  git commit -m "MoonORMot: версия $NEW (автоподъём)"
  git push

  echo "Version bumped: $CURRENT -> $NEW"
elif [ "$VERSION_CHANGED" = true ]; then
  echo "Version was bumped manually — nothing to do."
elif [ "$CODE_CHANGED" = false ]; then
  echo "No code files changed — nothing to do."
fi
